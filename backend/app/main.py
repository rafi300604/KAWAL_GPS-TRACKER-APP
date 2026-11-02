from datetime import datetime

from fastapi import Depends, FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import delete, select
from sqlalchemy.ext.asyncio import AsyncSession

from . import models, schemas, telemetry
from .auth import (
    create_access_token,
    get_current_user,
    hash_password,
    require_admin,
    require_admin_user,
    require_device,
    verify_password,
)
from .database import get_db
from .gps_mqtt_client import (
    clear_retained_command,
    start_mqtt_client,
    stop_mqtt_client,
    trigger_command_dispatch,
)

# Skema tabel dikelola lewat Alembic (alembic/versions/), dijalankan
# otomatis oleh docker-entrypoint.sh sebelum uvicorn start.
app = FastAPI(title="Kawal GPS Telemetry Ingest")

# Batas titik /telemetry/{sn}/history?since=... (±7 hari data per 30 detik).
HISTORY_SINCE_MAX = 20000


@app.on_event("startup")
async def startup_event():
    import asyncio
    loop = asyncio.get_running_loop()
    print("[startup] starting MQTT client...", flush=True)
    start_mqtt_client(loop=loop)


@app.on_event("shutdown")
async def shutdown_event():
    print("[shutdown] stopping MQTT client...", flush=True)
    stop_mqtt_client()


# Tanpa ini, browser (Flutter web) mengirim preflight OPTIONS sebelum
# request asli -- server balas 405 dan browser membatalkan sendiri
# request-nya (ClientException: Failed to fetch), padahal endpoint
# aslinya baik-baik saja (curl selalu sukses karena tidak preflight).
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/health")
async def health():
    return {"status": "ok"}


@app.post(
    "/telemetry",
    response_model=schemas.DeviceState,
    dependencies=[Depends(require_device)],
)
async def ingest_telemetry(payload: schemas.TelemetryIn, db: AsyncSession = Depends(get_db)):
    """Endpoint yang dipanggil device. Device tidak perlu registrasi
    terpisah -- baris di tabel `devices` otomatis dibuat/diupdate
    (upsert) berdasarkan `sn`, mirip pola satu dokumen Firestore per sn.

    Terima DUA format body (lihat FIRMWARE_CONTRACT.md): field `d` (biner
    hex-encode, format baru hemat kuota) ATAU field lat/lng/timestamp/
    dataSource lama langsung. `d` diprioritaskan kalau dua-duanya ada."""
    if payload.d is not None:
        decoded = telemetry.decode_compact_telemetry(payload.d)
        lat = decoded["lat"]
        lng = decoded["lng"]
        heading = decoded["heading"]
        speed_knots = decoded["speed_knots"]
        timestamp = decoded["timestamp"]
        data_source = decoded["data_source"]
        power_source = decoded["power_source"]
        tamper = decoded["tamper"]
        tamper_test = decoded["tamper_test"]
    else:
        if payload.lat is None or payload.lng is None or payload.timestamp is None or payload.data_source is None:
            raise HTTPException(
                status_code=400,
                detail="Body harus berisi field 'd' (format baru) atau "
                "lat/lng/timestamp/dataSource (format lama)",
            )
        lat = payload.lat
        lng = payload.lng
        heading = payload.heading if payload.heading is not None else 0.0
        speed_knots = payload.speed_knots if payload.speed_knots is not None else 0.0
        timestamp = payload.timestamp
        data_source = payload.data_source
        power_source = payload.power_source
        tamper = payload.tamper if payload.tamper is not None else False
        tamper_test = False

    return await telemetry.persist_telemetry(
        db,
        sn=payload.sn,
        lat=lat,
        lng=lng,
        heading=heading,
        speed_knots=speed_knots,
        timestamp=timestamp,
        data_source=data_source,
        power_source=power_source,
        tamper=tamper,
        tamper_test=tamper_test,
        name=payload.name,
        status=payload.status,
    )


@app.get(
    "/devices",
    response_model=list[schemas.DeviceState],
    dependencies=[Depends(require_admin)],
)
async def list_devices(db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(models.Device).order_by(models.Device.sn))
    return result.scalars().all()


@app.get(
    "/devices/{sn}",
    response_model=schemas.DeviceState,
    dependencies=[Depends(require_admin)],
)
async def get_device_state(sn: str, db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(models.Device).where(models.Device.sn == sn))
    device = result.scalar_one_or_none()
    if device is None:
        raise HTTPException(status_code=404, detail="Device tidak ditemukan")
    return device


@app.get(
    "/devices/{sn}/alerts",
    response_model=list[schemas.DeviceAlertOut],
    dependencies=[Depends(require_admin)],
)
async def list_device_alerts(sn: str, limit: int = 20, db: AsyncSession = Depends(get_db)):
    """Riwayat notifikasi otomatis device ini (mis. kuota SIM menipis) --
    sama seperti /devices dan /telemetry/*/history, dibaca pakai X-Admin-Key
    (bukan JWT), karena ini data device bukan data milik user tertentu."""
    result = await db.execute(
        select(models.DeviceAlert)
        .where(models.DeviceAlert.device_sn == sn)
        .order_by(models.DeviceAlert.created_at.desc())
        .limit(min(limit, 100))
    )
    return result.scalars().all()


@app.post(
    "/devices/{sn}/commands",
    response_model=schemas.DeviceCommandOut,
    status_code=201,
    dependencies=[Depends(require_admin)],
)
async def create_device_command(
    sn: str, payload: schemas.DeviceCommandIn, db: AsyncSession = Depends(get_db)
):
    """Kirim perintah ke device lewat MQTT `cmd/<SN>`, mis. percepat interval
    kirim sementara: {"cmd":"interval","interval_s":30,"duration_min":60},
    atau batalkan: {"cmd":"reset"}. Hanya pemegang X-Admin-Key. Status
    (pending/sent/applied/rejected/expired) dicek lewat GET di bawah."""
    result = await db.execute(select(models.Device.id).where(models.Device.sn == sn))
    if result.scalar_one_or_none() is None:
        raise HTTPException(status_code=404, detail="Device tidak ditemukan")
    command = models.DeviceCommand(
        device_sn=sn,
        cmd=payload.cmd,
        interval_s=payload.interval_s if payload.cmd == "interval" else None,
        duration_min=payload.duration_min if payload.cmd == "interval" else None,
        duration_s=payload.duration_s if payload.cmd == "buzzer" else None,
        status="pending",
    )
    db.add(command)
    await db.commit()
    await db.refresh(command)
    trigger_command_dispatch()
    return command


@app.get(
    "/devices/{sn}/commands",
    response_model=list[schemas.DeviceCommandOut],
    dependencies=[Depends(require_admin)],
)
async def list_device_commands(sn: str, limit: int = 20, db: AsyncSession = Depends(get_db)):
    result = await db.execute(
        select(models.DeviceCommand)
        .where(models.DeviceCommand.device_sn == sn)
        .order_by(models.DeviceCommand.id.desc())
        .limit(min(limit, 100))
    )
    return result.scalars().all()


@app.delete(
    "/devices/{sn}",
    status_code=204,
    dependencies=[Depends(require_admin)],
)
async def delete_device(sn: str, db: AsyncSession = Depends(get_db)):
    """Hapus device beserta seluruh data terkait. FK ke devices.sn TIDAK
    ON DELETE CASCADE (lihat alembic/versions/), jadi baris anak
    (telemetry, device_alerts, waypoints) harus dihapus dulu sebelum baris
    device sendiri, kalau tidak Postgres akan menolak dengan
    ForeignKeyViolation."""
    result = await db.execute(select(models.Device).where(models.Device.sn == sn))
    device = result.scalar_one_or_none()
    if device is None:
        raise HTTPException(status_code=404, detail="Device tidak ditemukan")

    await db.execute(delete(models.Waypoint).where(models.Waypoint.device_sn == sn))
    await db.execute(delete(models.DeviceCommand).where(models.DeviceCommand.device_sn == sn))
    await db.execute(delete(models.DeviceAlert).where(models.DeviceAlert.device_sn == sn))
    await db.execute(delete(models.Telemetry).where(models.Telemetry.device_sn == sn))
    await db.delete(device)
    await db.commit()
    clear_retained_command(sn)  # jangan tinggalkan perintah retained di cmd/<SN>


@app.get(
    "/telemetry/{sn}/history",
    response_model=list[schemas.TelemetryOut],
    dependencies=[Depends(require_admin)],
)
async def telemetry_history(
    sn: str, limit: int = 50, since: datetime | None = None, db: AsyncSession = Depends(get_db)
):
    """Tanpa `since`: `limit` titik terbaru (maks 500). Dengan `since` (ISO-8601):
    titik sejak waktu itu, maks HISTORY_SINCE_MAX -- dipakai app supaya lintasan
    rentang waktu yang dipilih tergambar utuh, tidak terpotong di 500 titik."""
    query = select(models.Telemetry).where(models.Telemetry.device_sn == sn)
    if since is not None:
        query = query.where(models.Telemetry.ts >= since)
    cap = HISTORY_SINCE_MAX if since is not None else 500
    result = await db.execute(query.order_by(models.Telemetry.ts.desc()).limit(min(limit, cap)))
    return result.scalars().all()


# ===================================================================
# Auth / users -- menggantikan Firebase Auth + Firestore users/{uid}
# ===================================================================


@app.post("/auth/login", response_model=schemas.TokenOut)
async def login(payload: schemas.UserLogin, db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(models.User).where(models.User.email == payload.email))
    user = result.scalar_one_or_none()
    if user is None or not verify_password(payload.password, user.password_hash):
        raise HTTPException(status_code=401, detail="Email atau kata sandi salah")
    token = create_access_token(user.id)
    return schemas.TokenOut(access_token=token, user=user)


@app.get("/users/me", response_model=schemas.UserOut)
async def get_own_profile(user: models.User = Depends(get_current_user)):
    return user


@app.patch("/users/me", response_model=schemas.UserOut)
async def update_own_profile(
    payload: schemas.UserUpdate,
    user: models.User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    """Self-service: cuma nama yang boleh diubah lewat endpoint ini -- role
    di body (kalau ada) sengaja diabaikan, supaya user biasa tidak bisa
    menaikkan role dirinya sendiri jadi admin. Ganti role WAJIB lewat admin
    (PATCH /users/{id})."""
    user.name = payload.name
    await db.commit()
    await db.refresh(user)
    return user


@app.get(
    "/users",
    response_model=list[schemas.UserOut],
    dependencies=[Depends(require_admin_user)],
)
async def list_users(db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(models.User).order_by(models.User.created_at.desc()))
    return result.scalars().all()


@app.post(
    "/users",
    response_model=schemas.UserOut,
    dependencies=[Depends(require_admin_user)],
)
async def create_user(payload: schemas.UserCreate, db: AsyncSession = Depends(get_db)):
    if payload.role not in ("admin", "user"):
        raise HTTPException(status_code=400, detail="role harus 'admin' atau 'user'")
    existing = await db.execute(select(models.User).where(models.User.email == payload.email))
    if existing.scalar_one_or_none() is not None:
        raise HTTPException(status_code=409, detail="Email sudah terdaftar")
    user = models.User(
        name=payload.name,
        email=payload.email,
        password_hash=hash_password(payload.password),
        role=payload.role,
    )
    db.add(user)
    await db.commit()
    await db.refresh(user)
    return user


@app.patch(
    "/users/{user_id}",
    response_model=schemas.UserOut,
    dependencies=[Depends(require_admin_user)],
)
async def update_user(user_id: int, payload: schemas.UserUpdate, db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(models.User).where(models.User.id == user_id))
    user = result.scalar_one_or_none()
    if user is None:
        raise HTTPException(status_code=404, detail="Pengguna tidak ditemukan")
    user.name = payload.name
    if payload.role is not None:
        if payload.role not in ("admin", "user"):
            raise HTTPException(status_code=400, detail="role harus 'admin' atau 'user'")
        user.role = payload.role
    await db.commit()
    await db.refresh(user)
    return user


@app.delete(
    "/users/{user_id}",
    status_code=204,
    dependencies=[Depends(require_admin_user)],
)
async def delete_user(user_id: int, db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(models.User).where(models.User.id == user_id))
    user = result.scalar_one_or_none()
    if user is None:
        raise HTTPException(status_code=404, detail="Pengguna tidak ditemukan")
    await db.delete(user)
    await db.commit()


# ===================================================================
# Waypoints -- menggantikan Firestore devices/{sn}/waypoints. Siapa saja
# yang login boleh baca/tulis (sama seperti rule Firestore lama:
# allow write: if isSignedIn()) -- tidak dibatasi role.
# ===================================================================


@app.get(
    "/devices/{sn}/waypoints",
    response_model=list[schemas.WaypointOut],
    dependencies=[Depends(get_current_user)],
)
async def list_waypoints(sn: str, db: AsyncSession = Depends(get_db)):
    result = await db.execute(
        select(models.Waypoint)
        .where(models.Waypoint.device_sn == sn)
        .order_by(models.Waypoint.created_at)
    )
    return result.scalars().all()


@app.post(
    "/devices/{sn}/waypoints",
    response_model=schemas.WaypointOut,
    dependencies=[Depends(get_current_user)],
)
async def create_waypoint(sn: str, payload: schemas.WaypointIn, db: AsyncSession = Depends(get_db)):
    device_exists = await db.execute(select(models.Device.sn).where(models.Device.sn == sn))
    if device_exists.scalar_one_or_none() is None:
        raise HTTPException(status_code=404, detail="Device tidak ditemukan")
    waypoint = models.Waypoint(device_sn=sn, name=payload.name, lat=payload.lat, lng=payload.lng)
    db.add(waypoint)
    await db.commit()
    await db.refresh(waypoint)
    return waypoint


@app.delete(
    "/devices/{sn}/waypoints/{waypoint_id}",
    status_code=204,
    dependencies=[Depends(get_current_user)],
)
async def delete_waypoint(sn: str, waypoint_id: int, db: AsyncSession = Depends(get_db)):
    result = await db.execute(
        select(models.Waypoint).where(
            models.Waypoint.id == waypoint_id, models.Waypoint.device_sn == sn
        )
    )
    waypoint = result.scalar_one_or_none()
    if waypoint is None:
        raise HTTPException(status_code=404, detail="Waypoint tidak ditemukan")
    await db.delete(waypoint)
    await db.commit()
