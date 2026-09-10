from fastapi import Depends, FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from . import models, schemas
from .auth import require_admin, require_device
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
