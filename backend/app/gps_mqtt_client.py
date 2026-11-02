"""Subscriber MQTT -- jalur pengganti HTTPS untuk device yang kirim lewat
GSM/seluler (hemat kuota data: satu koneksi persisten, bukan buka sesi TLS
baru tiap kirim -- lihat FIRMWARE_CONTRACT.md).

Payload di topic `telemetry/<SN>` adalah HEX MENTAH (bukan dibungkus JSON)
-- identitas device datang dari topic, bukan dari payload.

Arah sebaliknya (server -> device): perintah di `cmd/<SN>`, dibalas device di
`cmd-ack/<SN>` -- lihat app/commands.py.

Status koneksi: device publish "online" di `status/<SN>` setiap selesai connect
dan memasang Last Will "offline" di topic yang sama -- broker menerbitkannya
kalau koneksi device putus tanpa DISCONNECT (daya dicabut, sinyal hilang),
setelah 1,5x keepalive. Lihat telemetry.set_device_online()."""
import asyncio
import os
import socket
from datetime import datetime, timezone

import paho.mqtt.client as mqtt

from . import commands, telemetry
from .database import async_session

_MQTT_CLIENT = None
_MAIN_LOOP = None
_DISPATCH_TASK = None
_DISPATCH_WAKE: asyncio.Event | None = None
DISPATCH_EVERY_S = 10
# Dispatcher & pemroses ack sama-sama memutuskan publish/hapus retained
# cmd/<SN> berdasarkan isi DB; lock ini mencegah ack menghapus retained milik
# perintah yang baru saja dikirim di sela-sela await keduanya.
_COMMAND_LOCK: asyncio.Lock | None = None
# "offline" baru dicatat kalau tidak disusul "online" dalam jeda ini -- saat
# device sambung ulang sendiri (restart layanan MQTT modem, pindah GSM<->WiFi
# dengan client id yang sama) broker menerbitkan Will sesi lama tepat sebelum
# sesi baru publish "online"; jeda ini mencegah pop-up offline palsu.
OFFLINE_DEBOUNCE_S = 5
_OFFLINE_PENDING: dict[str, asyncio.Task] = {}


def _get_mqtt_config():
    broker = os.getenv("GPS_MQTT_BROKER", "")
    # Default 1883 (listener INTERNAL, anonymous, tanpa TLS -- lihat
    # mqtt-gps/config/mosquitto.conf) -- backend jalan di network Docker yang
    # sama dengan broker, tidak perlu lewat listener publik 8883 (yang pakai
    # cert self-signed + auth, khusus device eksternal dari internet).
    port = int(os.getenv("GPS_MQTT_PORT", "1883"))
    client_id = os.getenv("GPS_MQTT_CLIENT_ID") or f"kawal-gps-backend-{socket.gethostname()}-{os.getpid()}"
    return {
        "broker": broker,
        "port": port,
        "user": os.getenv("GPS_MQTT_USER", ""),
        "password": os.getenv("GPS_MQTT_PASSWORD", ""),
        "client_id": client_id,
        "topic": "telemetry/+",
        "ack_topic": "cmd-ack/+",
        "status_topic": "status/+",
        "use_tls": os.getenv("GPS_MQTT_TLS", "false").lower() == "true",
    }


def _extract_sn_from_topic(topic: str, prefix: str = "telemetry") -> str | None:
    parts = topic.strip("/").split("/")
    if len(parts) == 2 and parts[0] == prefix:
        return parts[1]
    return None


async def _handle_telemetry_message(sn: str, hex_payload: str):
    try:
        decoded = telemetry.decode_compact_telemetry(hex_payload)
    except Exception as exc:
        print(f"[mqtt] Gagal decode payload dari topic telemetry/{sn}: {exc}", flush=True)
        return
    async with async_session() as db:
        await telemetry.persist_telemetry(
            db,
            sn=sn,
            lat=decoded["lat"],
            lng=decoded["lng"],
            heading=decoded["heading"],
            speed_knots=decoded["speed_knots"],
            timestamp=decoded["timestamp"],
            data_source=decoded["data_source"],
            power_source=decoded["power_source"],
            tamper=decoded["tamper"],
            tamper_test=decoded["tamper_test"],
        )
    print(f"[mqtt] Tersimpan: sn={sn} lat={decoded['lat']} lng={decoded['lng']}", flush=True)


def _on_connect(client, userdata, flags, rc, properties=None):
    config = _get_mqtt_config()
    if rc == 0:
        print(f"[mqtt] Terhubung; subscribe ke {config['topic']}, {config['ack_topic']} & "
              f"{config['status_topic']}", flush=True)
        client.subscribe([(config["topic"], 0), (config["ack_topic"], 1), (config["status_topic"], 1)])
        trigger_command_dispatch()  # kirim perintah yang tertunda selama terputus
    else:
        print(f"[mqtt] Gagal connect, kode {rc}", flush=True)


def _on_disconnect(client, userdata, rc, properties=None):
    print(f"[mqtt] Terputus, kode {rc}", flush=True)


def _run_on_main_loop(coro, what: str):
    if _MAIN_LOOP is not None and _MAIN_LOOP.is_running():
        asyncio.run_coroutine_threadsafe(coro, _MAIN_LOOP)
    else:
        try:
            asyncio.run(coro)
        except Exception as exc:
            print(f"[mqtt] Error memproses {what}: {exc}", flush=True)


def clear_retained_command(sn: str):
    """Hapus perintah retained di cmd/<SN> (payload kosong + retain)."""
    if _MQTT_CLIENT is not None:
        _MQTT_CLIENT.publish(f"cmd/{sn}", payload=b"", qos=1, retain=True)
        print(f"[mqtt] Retained cmd/{sn} dihapus", flush=True)


async def _handle_ack_message(sn: str, text: str):
    async with _COMMAND_LOCK, async_session() as db:
        print(f"[mqtt] cmd-ack/{sn}: {await commands.apply_ack(db, sn, text)}", flush=True)
        if not await commands.has_active_command(db, sn):
            clear_retained_command(sn)


async def _set_online(sn: str, online: bool):
    async with async_session() as db:
        if await telemetry.set_device_online(db, sn, online):
            print(f"[mqtt] status/{sn}: {'online' if online else 'OFFLINE'}", flush=True)


async def _offline_after_debounce(sn: str):
    await asyncio.sleep(OFFLINE_DEBOUNCE_S)
    _OFFLINE_PENDING.pop(sn, None)
    await _set_online(sn, False)


def _handle_status(sn: str, text: str):
    """Jalan di main loop (call_soon_threadsafe) supaya _OFFLINE_PENDING
    hanya disentuh satu thread."""
    pending = _OFFLINE_PENDING.pop(sn, None)
    if pending is not None:
        pending.cancel()
    if text == "offline":
        _OFFLINE_PENDING[sn] = _MAIN_LOOP.create_task(_offline_after_debounce(sn))
    elif text == "online":
        _MAIN_LOOP.create_task(_set_online(sn, True))
    else:
        print(f"[mqtt] status/{sn}: payload tidak dikenali {text!r}", flush=True)


def _on_message(client, userdata, msg):
    status_sn = _extract_sn_from_topic(msg.topic, prefix="status")
    if status_sn is not None:
        text = msg.payload.decode("utf-8", "replace").strip()
        if _MAIN_LOOP is not None and _MAIN_LOOP.is_running():
            _MAIN_LOOP.call_soon_threadsafe(_handle_status, status_sn, text)
        return

    ack_sn = _extract_sn_from_topic(msg.topic, prefix="cmd-ack")
    if ack_sn is not None:
        try:
            text = msg.payload.decode("utf-8").strip()
        except UnicodeDecodeError:
            print(f"[mqtt] Ack bukan teks di topic {msg.topic}", flush=True)
            return
        _run_on_main_loop(_handle_ack_message(ack_sn, text), f"ack dari {ack_sn}")
        return

    sn = _extract_sn_from_topic(msg.topic)
    if sn is None:
        print(f"[mqtt] Topic tidak dikenali: {msg.topic}", flush=True)
        return
    try:
        hex_payload = msg.payload.decode("ascii").strip()
    except UnicodeDecodeError:
        print(f"[mqtt] Payload bukan teks ascii/hex di topic {msg.topic}", flush=True)
        return

    _run_on_main_loop(_handle_telemetry_message(sn, hex_payload), f"pesan dari {sn}")


def trigger_command_dispatch():
    """Bangunkan dispatcher sekarang (thread-safe) -- dipanggil setelah
    perintah baru dibuat lewat API dan saat (re)connect ke broker."""
    if _DISPATCH_WAKE is not None and _MAIN_LOOP is not None and _MAIN_LOOP.is_running():
        _MAIN_LOOP.call_soon_threadsafe(_DISPATCH_WAKE.set)


async def _dispatch_once():
    client = _MQTT_CLIENT
    if client is None or not client.is_connected():
        return
    now = datetime.now(timezone.utc)
    async with _COMMAND_LOCK, async_session() as db:
        due, clear_retained = await commands.due_for_sending(db, now)
        for sn in clear_retained:
            clear_retained_command(sn)
        for command in due:
            # retained: device yang sedang offline tetap menerimanya saat
            # subscribe lagi -- lihat docstring app/commands.py.
            info = client.publish(
                f"cmd/{command.device_sn}", commands.build_payload(command), qos=1, retain=True
            )
            if info.rc == mqtt.MQTT_ERR_SUCCESS:
                command.status = "sent"
                command.sent_at = now
                print(f"[mqtt] Perintah {command.id} dikirim ke cmd/{command.device_sn}", flush=True)
            else:
                print(f"[mqtt] Gagal kirim perintah {command.id}: rc={info.rc}", flush=True)
        await db.commit()


async def _command_dispatcher():
    """Kirim perintah `pending` (dari API maupun INSERT manual) dan kirim ulang
    yang belum dibalas -- lihat commands.due_for_sending()."""
    while True:
        try:
            await _dispatch_once()
        except Exception as exc:
            print(f"[mqtt] Dispatcher perintah error: {exc}", flush=True)
        try:
            await asyncio.wait_for(_DISPATCH_WAKE.wait(), timeout=DISPATCH_EVERY_S)
        except asyncio.TimeoutError:
            pass
        _DISPATCH_WAKE.clear()


def start_mqtt_client(loop=None):
    global _MQTT_CLIENT, _MAIN_LOOP
    if loop is not None:
        _MAIN_LOOP = loop
    elif _MAIN_LOOP is None:
        try:
            _MAIN_LOOP = asyncio.get_running_loop()
        except RuntimeError:
            pass

    if _MQTT_CLIENT is not None:
        return _MQTT_CLIENT

    config = _get_mqtt_config()
    if not config["broker"]:
        print("[mqtt] GPS_MQTT_BROKER belum diset -- subscriber MQTT tidak dijalankan.", flush=True)
        return None

    print(
        f"[mqtt] Inisialisasi client broker={config['broker']}:{config['port']} "
        f"tls={config['use_tls']} client_id={config['client_id']}",
        flush=True,
    )

    try:
        client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION1,
            client_id=config["client_id"],
            clean_session=True,
            protocol=mqtt.MQTTv311,
        )
    except (AttributeError, TypeError):
        client = mqtt.Client(client_id=config["client_id"], clean_session=True, protocol=mqtt.MQTTv311)

    if config["user"] or config["password"]:
        client.username_pw_set(config["user"], config["password"])
    if config["use_tls"]:
        client.tls_set()

    client.on_connect = _on_connect
    client.on_disconnect = _on_disconnect
    client.on_message = _on_message

    for attempt in range(1, 11):
        try:
            client.connect(config["broker"], config["port"], keepalive=60)
            client.loop_start()
            _MQTT_CLIENT = client
            _start_dispatcher()
            print(f"[mqtt] Client jalan; broker={config['broker']}:{config['port']}", flush=True)
            return client
        except Exception as exc:
            print(f"[mqtt] Broker belum siap (percobaan {attempt}/10): {exc}; coba lagi 2 detik", flush=True)
            import time
            time.sleep(2)

    print("[mqtt] Broker MQTT tidak tersedia setelah beberapa percobaan; lanjut tanpa MQTT.", flush=True)
    return None


def _start_dispatcher():
    global _DISPATCH_TASK, _DISPATCH_WAKE, _COMMAND_LOCK
    if _DISPATCH_TASK is not None or _MAIN_LOOP is None:
        return
    _DISPATCH_WAKE = asyncio.Event()
    _COMMAND_LOCK = asyncio.Lock()
    _DISPATCH_TASK = _MAIN_LOOP.create_task(_command_dispatcher())


def stop_mqtt_client():
    global _MQTT_CLIENT, _DISPATCH_TASK
    if _DISPATCH_TASK is not None:
        _DISPATCH_TASK.cancel()
        _DISPATCH_TASK = None
    if _MQTT_CLIENT is None:
        return
    try:
        _MQTT_CLIENT.disconnect()
    finally:
        _MQTT_CLIENT.loop_stop()
        _MQTT_CLIENT = None
