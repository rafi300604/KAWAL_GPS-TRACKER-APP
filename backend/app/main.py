from datetime import datetime, timedelta, timezone

from fastapi import Depends, FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from . import models, schemas
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

# Skema tabel dikelola lewat Alembic (alembic/versions/), dijalankan
# otomatis oleh docker-entrypoint.sh sebelum uvicorn start.
app = FastAPI(title="Jejak Pintar Telemetry Ingest")

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

# Notifikasi "kuota SIM diduga habis" -- lihat ingest_telemetry(). Modem GSM
# tidak punya cara membaca sisa kuota asli dari operator, jadi firmware
# (skripsi.ino) mengirim heuristik lewat field `dataSource` yang sudah ada
# (bukan field angka baru) begitu mendeteksi sesi data GSM gagal berkali-kali
# padahal jaringan seluler terdaftar normal -- lihat FIRMWARE_CONTRACT.md.
# Cooldown mencegah satu baris device_alerts baru dibuat di SETIAP transmisi
# selama kondisinya masih berlangsung (device bisa kirim tiap beberapa
# detik), yang akan membanjiri riwayat alert dengan ratusan baris identik.
QUOTA_WARNING_KEYWORD = "kuota"
QUOTA_ALERT_COOLDOWN = timedelta(hours=12)


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
    (upsert) berdasarkan `sn`, mirip pola satu dokumen Firestore per sn."""
    result = await db.execute(select(models.Device).where(models.Device.sn == payload.sn))
    device = result.scalar_one_or_none()

    if device is None:
        device = models.Device(sn=payload.sn)
        db.add(device)

    if payload.name is not None:
        device.name = payload.name
    if payload.power_source is not None:
        device.power_source = payload.power_source
    device.data_source = payload.data_source
    if payload.status is not None:
        device.status = payload.status

    device.last_lat = payload.lat
    device.last_lng = payload.lng
    device.last_speed_knots = payload.speed_knots
    device.last_heading = payload.heading
    device.last_transmitted_at = payload.timestamp

    if QUOTA_WARNING_KEYWORD in payload.data_source.lower():
        cutoff = datetime.now(timezone.utc) - QUOTA_ALERT_COOLDOWN
        recent_alert = await db.execute(
            select(models.DeviceAlert.id)
            .where(
                models.DeviceAlert.device_sn == payload.sn,
                models.DeviceAlert.type == "low_quota",
                models.DeviceAlert.created_at >= cutoff,
            )
            .limit(1)
        )
        if recent_alert.scalar_one_or_none() is None:
            db.add(
                models.DeviceAlert(
                    device_sn=payload.sn,
                    type="low_quota",
                    message=f"Device {payload.sn} melaporkan: {payload.data_source}",
                    value=None,
                )
            )

    row = models.Telemetry(
        device_sn=payload.sn,
        ts=payload.timestamp,
        lat=payload.lat,
        lng=payload.lng,
        speed_knots=payload.speed_knots,
        heading=payload.heading,
        data_source=payload.data_source,
    )
    db.add(row)
    await db.commit()
    await db.refresh(device)
    return device


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


@app.get(
    "/telemetry/{sn}/history",
    response_model=list[schemas.TelemetryOut],
    dependencies=[Depends(require_admin)],
)
async def telemetry_history(sn: str, limit: int = 50, db: AsyncSession = Depends(get_db)):
    result = await db.execute(
        select(models.Telemetry)
        .where(models.Telemetry.device_sn == sn)
        .order_by(models.Telemetry.ts.desc())
        .limit(min(limit, 500))
    )
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
