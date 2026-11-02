"""Decode + persist logic untuk telemetry GPS, dipakai BERSAMA oleh dua jalur
transport (HTTP `POST /telemetry` di main.py, dan MQTT topic `telemetry/<SN>`
di gps_mqtt_client.py) -- supaya logic upsert device/alert kuota/alert tamper
TIDAK terduplikasi (dan berisiko divergen) di dua tempat berbeda.
"""
from datetime import datetime, timedelta, timezone

import struct

from fastapi import HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from . import models

# Notifikasi "kuota SIM diduga habis" -- lihat persist_telemetry(). Modem GSM
# tidak punya cara membaca sisa kuota asli dari operator, jadi firmware
# (skripsi.ino) mengirim heuristik lewat field `dataSource` yang sudah ada
# (bukan field angka baru) begitu mendeteksi sesi data GSM gagal berkali-kali
# padahal jaringan seluler terdaftar normal -- lihat FIRMWARE_CONTRACT.md.
# Cooldown mencegah satu baris device_alerts baru dibuat di SETIAP transmisi
# selama kondisinya masih berlangsung (device bisa kirim tiap beberapa
# detik), yang akan membanjiri riwayat alert dengan ratusan baris identik.
QUOTA_WARNING_KEYWORD = "kuota"
QUOTA_ALERT_COOLDOWN = timedelta(hours=12)

# Format kompak per 2026-09-15 (lihat FIRMWARE_CONTRACT.md) -- 17 byte
# big-endian: lat(i32) lng(i32) heading(u16) speedKnots(u16) timestamp(u32)
# flags(u8). Dipakai baik di body HTTP (field `d`, hex-encode di dalam JSON)
# MAUPUN payload MQTT (hex mentah, tanpa wrapper JSON -- identitas device
# datang dari topic `telemetry/<SN>`, bukan diulang di payload).
COMPACT_TELEMETRY_STRUCT = struct.Struct(">iiHHIB")
FLAG_WIFI = 0b0001
FLAG_QUOTA_WARNING = 0b0010
FLAG_POWER_AKI = 0b0100
FLAG_TAMPER = 0b1000
# Titik tamper hasil perintah server `test_tamper` (simulasi), bukan guncangan
# asli -- alert-nya diberi tanda [UJI]. Cuma valid bersama bit3.
FLAG_TAMPER_TEST = 0b10000
FLAG_RESERVED_MASK = 0b11100000


def decode_compact_telemetry(hex_payload: str) -> dict:
    try:
        raw = bytes.fromhex(hex_payload)
    except ValueError:
        raise HTTPException(status_code=400, detail="Field 'd' bukan hex yang valid")
    if len(raw) != COMPACT_TELEMETRY_STRUCT.size:
        raise HTTPException(
            status_code=400,
            detail=f"Field 'd' harus {COMPACT_TELEMETRY_STRUCT.size} byte "
            f"({COMPACT_TELEMETRY_STRUCT.size * 2} karakter hex), dapat {len(raw)} byte",
        )
    lat_raw, lng_raw, heading, speed_raw, ts_raw, flags = COMPACT_TELEMETRY_STRUCT.unpack(raw)
    if flags & FLAG_RESERVED_MASK:
        raise HTTPException(status_code=400, detail="Bit cadangan (bit5-7) di flags harus 0")
    if flags & FLAG_TAMPER_TEST and not flags & FLAG_TAMPER:
        raise HTTPException(status_code=400, detail="tamper_test (bit4) cuma valid kalau tamper (bit3=1)")

    is_wifi = bool(flags & FLAG_WIFI)
    quota_warning = bool(flags & FLAG_QUOTA_WARNING)
    if quota_warning and not is_wifi:
        raise HTTPException(
            status_code=400, detail="quota_warning (bit1) cuma valid kalau dataSource WIFI (bit0=1)"
        )

    if is_wifi:
        data_source = "WIFI (GSM: kuota diduga habis)" if quota_warning else "WIFI"
    else:
        data_source = "GSM"

    return {
        "lat": lat_raw / 1_000_000,
        "lng": lng_raw / 1_000_000,
        "heading": float(heading),
        "speed_knots": speed_raw / 100,
        "timestamp": datetime.fromtimestamp(ts_raw, tz=timezone.utc),
        "data_source": data_source,
        "power_source": "AKI" if flags & FLAG_POWER_AKI else "BATTERY",
        "tamper": bool(flags & FLAG_TAMPER),
        "tamper_test": bool(flags & FLAG_TAMPER_TEST),
    }


async def persist_telemetry(
    db: AsyncSession,
    *,
    sn: str,
    lat: float,
    lng: float,
    heading: float,
    speed_knots: float,
    timestamp: datetime,
    data_source: str,
    power_source: str | None,
    tamper: bool,
    tamper_test: bool = False,
    name: str | None = None,
    status: str | None = None,
) -> models.Device:
    """Upsert `devices` (state terkini) + insert baris `telemetry` +
    alert kuota/tamper kalau relevan. Dipanggil oleh ingest_telemetry()
    (HTTP) dan gps_mqtt_client.py (MQTT) -- SATU jalur logic, supaya
    perilaku identik apa pun transport yang dipakai device."""
    result = await db.execute(select(models.Device).where(models.Device.sn == sn))
    device = result.scalar_one_or_none()

    if device is None:
        device = models.Device(sn=sn)
        db.add(device)

    if name is not None:
        device.name = name
    if power_source is not None:
        device.power_source = power_source
    device.data_source = data_source
    if status is not None:
        device.status = status

    device.last_lat = lat
    device.last_lng = lng
    device.last_speed_knots = speed_knots
    device.last_heading = heading
    device.last_transmitted_at = timestamp
    device.tamper = tamper
    # Data baru = device pasti tersambung lagi (juga menutup kasus pesan "online"
    # yang tidak sampai).
    if device.online is False:
        db.add(models.DeviceAlert(device_sn=sn, type="online", message=f"Device {sn} tersambung kembali", value=None))
    device.online = True

    if QUOTA_WARNING_KEYWORD in data_source.lower():
        cutoff = datetime.now(timezone.utc) - QUOTA_ALERT_COOLDOWN
        recent_alert = await db.execute(
            select(models.DeviceAlert.id)
            .where(
                models.DeviceAlert.device_sn == sn,
                models.DeviceAlert.type == "low_quota",
                models.DeviceAlert.created_at >= cutoff,
            )
            .limit(1)
        )
        if recent_alert.scalar_one_or_none() is None:
            db.add(
                models.DeviceAlert(
                    device_sn=sn,
                    type="low_quota",
                    message=f"Device {sn} melaporkan: {data_source}",
                    value=None,
                )
            )

    if tamper:
        # Setiap titik data dengan tamper=true membuat baris device_alerts
        # baru sendiri-sendiri, TANPA suppression/cooldown/rising-edge --
        # ini kejadian kritis yang harus langsung memicu notifikasi/alarm di
        # app terlepas dari seberapa dekat jaraknya dengan titik tamper
        # sebelumnya (bisa 1 detik sekalipun).
        # Simulasi (perintah `test_tamper`) tetap type "tamper" supaya alarm
        # app sama persis dengan aslinya, tapi pesannya ditandai [UJI].
        stamp = f"{timestamp.strftime('%Y-%m-%d %H:%M:%S')} UTC"
        message = (
            f"[UJI] Device {sn} diusik (tamper) pada {stamp} - simulasi dari perintah server"
            if tamper_test
            else f"Device {sn} diusik (tamper) pada {stamp}"
        )
        db.add(models.DeviceAlert(device_sn=sn, type="tamper", message=message, value=None))

    row = models.Telemetry(
        device_sn=sn,
        ts=timestamp,
        lat=lat,
        lng=lng,
        speed_knots=speed_knots,
        heading=heading,
        data_source=data_source,
        power_source=power_source,
        tamper=tamper,
    )
    db.add(row)
    await db.commit()
    await db.refresh(device)
    return device


async def set_device_online(db: AsyncSession, sn: str, online: bool) -> bool:
    """Catat perubahan status koneksi dari topic status/<SN> + buat alert
    "offline"/"online" supaya app memunculkan pop-up. True kalau status berubah.
    Dari None (belum pernah dilaporkan) ke True tidak membuat alert."""
    result = await db.execute(select(models.Device).where(models.Device.sn == sn))
    device = result.scalar_one_or_none()
    if device is None or device.online == online:
        return False
    if online is False:
        db.add(models.DeviceAlert(device_sn=sn, type="offline",
                                  message=f"Koneksi device {sn} terputus (daya mati atau sinyal hilang)", value=None))
    elif device.online is False:
        db.add(models.DeviceAlert(device_sn=sn, type="online", message=f"Device {sn} tersambung kembali", value=None))
    device.online = online
    await db.commit()
    return True
