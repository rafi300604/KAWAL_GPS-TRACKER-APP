"""Perintah server -> device lewat MQTT (kebalikan arah telemetry).

Topic & format (kontrak dengan firmware skripsi.ino, lihat FIRMWARE_CONTRACT.md):
- server publish ke `cmd/<SN>` (QoS 1), payload JSON:
    {"id":12,"cmd":"interval","interval_s":30,"duration_min":60}
    {"id":13,"cmd":"reset"}
    {"id":14,"cmd":"buzzer","duration_s":10}   (bunyikan buzzer 1-20 detik)
    {"id":15,"cmd":"test_tamper"}   (simulasi tamper: buzzer + kirim titik
                                    tamper bertanda uji, flags bit4)
- device membalas ke `cmd-ack/<SN>`, payload JSON:
    {"id":12,"status":"applied", ...} / {"id":13,"status":"reset"} /
    {"id":14,"status":"rejected","reason":"..."}

Perintah "interval" berlaku sementara: setelah duration_min device kembali ke
interval normal sendiri (override hanya di RAM, reboot juga membatalkannya).

Pengiriman RETAINED: lewat GSM device hanya tersambung ke broker saat mengirim
telemetry (sampai 30 menit sekali di mode SLEEP) dan selalu clean session, jadi
pesan biasa yang terkirim saat device offline hilang. Pesan retained disimpan
broker dan langsung diterima device begitu ia subscribe lagi. Retained
dihapus (payload kosong) setelah dibalas atau kedaluwarsa, dan hanya perintah
TERBARU per device yang aktif -- yang lebih lama ditandai 'superseded'.
"""
import json
from datetime import datetime, timedelta, timezone

from sqlalchemy import select, update
from sqlalchemy.ext.asyncio import AsyncSession

from . import models

# Harus sama dengan batas yang divalidasi firmware.
INTERVAL_S_MIN, INTERVAL_S_MAX = 2, 1800
DURATION_MIN_MIN, DURATION_MIN_MAX = 1, 180
# Sama dengan batas alarm tamper firmware (TAMPER_ALARM_MAX_MS 20 detik).
DURATION_S_MIN, DURATION_S_MAX = 1, 20
COMMANDS = ("interval", "reset", "test_tamper", "buzzer")

# Kirim ulang kalau belum dibalas (koneksi GSM device bisa sempat putus).
RESEND_AFTER = timedelta(minutes=2)
FINAL_STATUSES = ("applied", "reset", "rejected", "invalid", "expired", "superseded")
ACTIVE_STATUSES = ("pending", "sent")

# Berhenti mencoba kalau tidak dibalas sama sekali selama ini. Harus lebih
# lama dari interval mode SLEEP (30 menit): lewat GSM, device baru
# tersambung ulang ke broker saat mengirim telemetry berikutnya.
DELIVERY_TIMEOUT = timedelta(minutes=60)

ACK_STATUSES = ("applied", "reset", "rejected")


def validation_error(
    cmd: str, interval_s: int | None, duration_min: int | None, duration_s: int | None = None
) -> str | None:
    """Alasan perintah tidak valid, atau None kalau valid. Dipakai juga untuk
    baris yang di-INSERT manual (tidak lewat validasi skema API)."""
    if cmd not in COMMANDS:
        return f"cmd harus salah satu dari {', '.join(COMMANDS)}"
    if cmd == "interval":
        if interval_s is None or not INTERVAL_S_MIN <= interval_s <= INTERVAL_S_MAX:
            return f"interval_s harus {INTERVAL_S_MIN}-{INTERVAL_S_MAX} detik"
        if duration_min is None or not DURATION_MIN_MIN <= duration_min <= DURATION_MIN_MAX:
            return f"duration_min harus {DURATION_MIN_MIN}-{DURATION_MIN_MAX} menit"
    if cmd == "buzzer":
        if duration_s is None or not DURATION_S_MIN <= duration_s <= DURATION_S_MAX:
            return f"duration_s harus {DURATION_S_MIN}-{DURATION_S_MAX} detik"
    return None


def build_payload(command: models.DeviceCommand) -> str:
    body: dict = {"id": command.id, "cmd": command.cmd}
    if command.cmd == "interval":
        body["interval_s"] = command.interval_s
        body["duration_min"] = command.duration_min
    elif command.cmd == "buzzer":
        body["duration_s"] = command.duration_s
    return json.dumps(body, separators=(",", ":"))


async def due_for_sending(
    db: AsyncSession, now: datetime
) -> tuple[list[models.DeviceCommand], set[str]]:
    """Tandai yang kedaluwarsa/tidak valid/tergantikan, lalu kembalikan
    (perintah yang perlu dikirim (ulang) sekarang, SN yang retained-nya harus
    dihapus karena tidak ada lagi perintah aktif)."""
    expired = await db.execute(
        update(models.DeviceCommand)
        .where(
            models.DeviceCommand.status.in_(ACTIVE_STATUSES),
            models.DeviceCommand.created_at <= now - DELIVERY_TIMEOUT,
        )
        .values(status="expired", ack_message="tidak dibalas device")
        .returning(models.DeviceCommand.device_sn)
    )
    touched = {sn for (sn,) in expired}

    result = await db.execute(
        select(models.DeviceCommand)
        .where(models.DeviceCommand.status.in_(ACTIVE_STATUSES))
        .order_by(models.DeviceCommand.id.desc())
    )
    newest_per_sn: dict[str, models.DeviceCommand] = {}
    for command in result.scalars():
        error = validation_error(
            command.cmd, command.interval_s, command.duration_min, command.duration_s
        )
        if error:
            command.status = "invalid"
            command.ack_message = error[:200]
            touched.add(command.device_sn)
        elif command.device_sn in newest_per_sn:
            command.status = "superseded"
            command.ack_message = f"digantikan perintah {newest_per_sn[command.device_sn].id}"
        else:
            newest_per_sn[command.device_sn] = command

    due = [
        c for c in newest_per_sn.values()
        if c.sent_at is None or c.sent_at <= now - RESEND_AFTER
    ]
    clear_retained = {sn for sn in touched if sn not in newest_per_sn}
    return due, clear_retained


async def has_active_command(db: AsyncSession, sn: str) -> bool:
    result = await db.execute(
        select(models.DeviceCommand.id)
        .where(
            models.DeviceCommand.device_sn == sn,
            models.DeviceCommand.status.in_(ACTIVE_STATUSES),
        )
        .limit(1)
    )
    return result.scalar_one_or_none() is not None


async def apply_ack(db: AsyncSession, sn: str, payload_text: str) -> str:
    """Catat balasan device di `cmd-ack/<SN>`. Mengembalikan ringkasan untuk log.
    Pemanggil menghapus retained cmd/<SN> kalau has_active_command() False."""
    try:
        ack = json.loads(payload_text)
        command_id = int(ack["id"])
        status = str(ack["status"])
    except (ValueError, KeyError, TypeError):
        return f"ack tidak dikenali: {payload_text[:100]!r}"
    if status not in ACK_STATUSES:
        return f"status ack tidak dikenali: {status!r}"

    command = await db.get(models.DeviceCommand, command_id)
    if command is None or command.device_sn != sn:
        return f"ack untuk perintah {command_id} yang tidak ada / bukan milik {sn}"
    if command.status in FINAL_STATUSES:
        return f"ack perintah {command_id} ({status}) diabaikan, status sudah {command.status}"

    command.status = status
    command.acked_at = datetime.now(timezone.utc)
    reason = ack.get("reason")
    command.ack_message = str(reason)[:200] if reason else None
    if command.cmd == "buzzer" and status == "applied":
        # Notifikasi app: alarm dibunyikan dari jarak jauh (bukan tamper).
        db.add(
            models.DeviceAlert(
                device_sn=sn,
                type="remote_alarm",
                message=f"Alarm jarak jauh: buzzer {sn} dibunyikan {command.duration_s} detik "
                f"atas perintah server pada {command.acked_at.strftime('%Y-%m-%d %H:%M:%S')} UTC",
                value=None,
            )
        )
    await db.commit()
    return f"perintah {command_id} -> {status}"
