from datetime import datetime, timezone

from sqlalchemy import Boolean, DateTime, Float, ForeignKey, Integer, String
from sqlalchemy.orm import Mapped, mapped_column

from .database import Base


class Device(Base):
    """Representasi 'state' terkini satu device -- mirip satu dokumen
    Firestore per sn. Diupdate tiap kali device kirim telemetry baru."""

    __tablename__ = "devices"

    id: Mapped[int] = mapped_column(primary_key=True)
    sn: Mapped[str] = mapped_column(String(50), unique=True, index=True)
    name: Mapped[str | None] = mapped_column(String(100), nullable=True)
    power_source: Mapped[str | None] = mapped_column(String(20), nullable=True)
    # String(50) -- BUKAN cuma "GSM"/"WIFI" lagi. Firmware (skripsi.ino) juga
    # bisa kirim string deskriptif seperti "WIFI (GSM: kuota diduga habis)"
    # kalau mendeteksi sesi data GSM gagal berkali-kali padahal jaringan
    # seluler terdaftar normal (heuristik kuota SIM habis -- modem TIDAK
    # PUNYA cara membaca sisa kuota asli dari operator). Lihat
    # FIRMWARE_CONTRACT.md dan DeviceAlert. String(10) yang lama akan GAGAL
    # (StringDataRightTruncation) begitu firmware mengirim string sepanjang
    # itu -- WAJIB lebih lebar dari nilai normal "GSM"/"WIFI".
    data_source: Mapped[str | None] = mapped_column(String(50), nullable=True)
    status: Mapped[str | None] = mapped_column(String(20), nullable=True)

    last_lat: Mapped[float | None] = mapped_column(Float, nullable=True)
    last_lng: Mapped[float | None] = mapped_column(Float, nullable=True)
    last_speed_knots: Mapped[float | None] = mapped_column(Float, nullable=True)
    last_heading: Mapped[float | None] = mapped_column(Float, nullable=True)
    last_transmitted_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    # Status tamper SAAT INI (bukan riwayat) -- diupdate tiap transmisi baru
    # masuk, sama seperti power_source/data_source di atas. Ditambahkan
    # karena device_alerts cuma dibuat SEKALI per rising edge -- untuk
    # kejadian tamper yang berlangsung lama (lebih dari beberapa menit),
    # mengandalkan "ada alert baru-baru ini" jadi salah begitu alert itu
    # sudah lewat jendela waktunya di app, padahal device MASIH tertampering
    # saat ini. Field ini kasih app sumber kebenaran langsung, tidak perlu
    # hitung-hitungan waktu dari alert lagi.
    tamper: Mapped[bool] = mapped_column(Boolean, nullable=False, default=False)
    # Status koneksi MQTT device: True setelah pesan "online" di status/<SN> atau
    # telemetry baru, False setelah Last Will "offline" dari broker (koneksi putus
    # tanpa DISCONNECT, mis. daya dicabut). None = belum pernah dilaporkan.
    online: Mapped[bool | None] = mapped_column(Boolean, nullable=True)


class Telemetry(Base):
    """Log historis lengkap tiap transmisi -- ini yang dipakai untuk replay
    rute/riwayat perjalanan, bukan cuma posisi terakhir."""

    __tablename__ = "telemetry"

    id: Mapped[int] = mapped_column(primary_key=True)
    device_sn: Mapped[str] = mapped_column(String(50), ForeignKey("devices.sn"), index=True)
    ts: Mapped[datetime] = mapped_column(DateTime(timezone=True), index=True)
    lat: Mapped[float] = mapped_column(Float)
    lng: Mapped[float] = mapped_column(Float)
    speed_knots: Mapped[float | None] = mapped_column(Float, nullable=True)
    heading: Mapped[float | None] = mapped_column(Float, nullable=True)
    data_source: Mapped[str | None] = mapped_column(String(50), nullable=True)  # lihat Device.data_source
    # Dulu cuma disimpan di `devices` (state terkini), TIDAK per-transmisi --
    # riwayat/export jadi tidak bisa menunjukkan sumber daya yang berlaku
    # SAAT titik itu direkam (bisa berubah, mis. BATTERY -> SOLAR). Ditambah
    # di sini supaya /telemetry/{sn}/history dan ekspor Excel bisa
    # menampilkannya per baris, bukan cuma nilai terkini yang sama di semua
    # baris.
    power_source: Mapped[str | None] = mapped_column(String(20), nullable=True)
    # Status alarm tamper (guncangan MPU6050) SAAT titik ini direkam -- cuma
    # terisi True untuk titik LIVE (firmware TIDAK menandai titik susulan
    # dari buffer offline, lihat FIRMWARE_CONTRACT.md), supaya titik lama
    # tidak salah ditandai "diusik" gara-gara ada tamper baru yang kebetulan
    # aktif saat buffer itu disusulkan.
    tamper: Mapped[bool] = mapped_column(Boolean, nullable=False, default=False)
    received_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=lambda: datetime.now(timezone.utc)
    )


class User(Base):
    """Akun app (login) -- menggantikan Firebase Auth + Firestore users/{uid}.
    Cuma 'admin' | 'user' (role 'device' dari skema Firestore lama sudah
    tidak relevan -- device otentikasi pakai DEVICE_API_KEY, bukan akun)."""

    __tablename__ = "users"

    id: Mapped[int] = mapped_column(primary_key=True)
    email: Mapped[str] = mapped_column(String(200), unique=True, index=True)
    password_hash: Mapped[str] = mapped_column(String(200))
    name: Mapped[str] = mapped_column(String(100))
    role: Mapped[str] = mapped_column(String(20), default="user")
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=lambda: datetime.now(timezone.utc)
    )


class DeviceAlert(Base):
    """Riwayat notifikasi device (mis. "kuota SIM diduga habis") -- baris
    baru dibuat backend sendiri (bukan ditulis firmware langsung) saat
    /telemetry menerima `dataSource` yang mengandung kata kunci peringatan
    kuota (lihat QUOTA_WARNING_KEYWORD di main.py), dengan cooldown supaya
    tidak spam satu baris per transmisi selama kondisinya masih berlangsung.
    Modem GSM tidak punya cara membaca sisa kuota asli dari operator, jadi
    ini heuristik dari firmware (gagal buka sesi data GSM berkali-kali
    padahal jaringan seluler terdaftar normal), bukan angka pasti -- lihat
    FIRMWARE_CONTRACT.md."""

    __tablename__ = "device_alerts"

    id: Mapped[int] = mapped_column(primary_key=True)
    device_sn: Mapped[str] = mapped_column(String(50), ForeignKey("devices.sn"), index=True)
    type: Mapped[str] = mapped_column(String(30))
    message: Mapped[str] = mapped_column(String(200))
    value: Mapped[float | None] = mapped_column(Float, nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=lambda: datetime.now(timezone.utc), index=True
    )


class Waypoint(Base):
    """Titik tujuan yang diset user dari app untuk satu device -- dipakai
    fitur ETA. Murni fitur sisi-app, tidak pernah ditulis firmware."""

    __tablename__ = "waypoints"

    id: Mapped[int] = mapped_column(primary_key=True)
    device_sn: Mapped[str] = mapped_column(String(50), ForeignKey("devices.sn"), index=True)
    name: Mapped[str] = mapped_column(String(100))
    lat: Mapped[float] = mapped_column(Float)
    lng: Mapped[float] = mapped_column(Float)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=lambda: datetime.now(timezone.utc)
    )


class DeviceCommand(Base):
    """Perintah server -> device lewat MQTT `cmd/<SN>` (mis. percepat interval
    kirim sementara) -- lihat app/commands.py dan FIRMWARE_CONTRACT.md.

    Dibuat lewat POST /devices/{sn}/commands ATAU INSERT manual (status default
    'pending' & created_at default now() ada di level database, lihat migrasi
    0007, supaya INSERT dari DBeaver cukup isi device_sn/cmd/interval_s/
    duration_min). Dispatcher di gps_mqtt_client mengirim & mengirim ulang
    sampai device membalas di `cmd-ack/<SN>`.

    status: pending -> sent -> applied | reset | rejected, atau invalid (gagal
    validasi server, tidak dikirim) / expired (tidak dibalas device dalam
    DELIVERY_TIMEOUT)."""

    __tablename__ = "device_commands"

    id: Mapped[int] = mapped_column(primary_key=True)
    device_sn: Mapped[str] = mapped_column(String(50), ForeignKey("devices.sn"), index=True)
    cmd: Mapped[str] = mapped_column(String(20))
    interval_s: Mapped[int | None] = mapped_column(Integer, nullable=True)
    duration_min: Mapped[int | None] = mapped_column(Integer, nullable=True)
    # Lama bunyi buzzer untuk cmd "buzzer" (detik), migrasi 0008.
    duration_s: Mapped[int | None] = mapped_column(Integer, nullable=True)
    status: Mapped[str] = mapped_column(String(20), default="pending", index=True)
    ack_message: Mapped[str | None] = mapped_column(String(200), nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=lambda: datetime.now(timezone.utc), index=True
    )
    sent_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
    acked_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
