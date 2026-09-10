# GPS — Telemetry Ingest

Terima data posisi GPS + status alat (kapal/tracker) lewat HTTP POST,
simpan ke PostgreSQL. Jalan di Docker, di belakang Caddy (HTTPS otomatis
lewat domain `gps.jejakpintar.cloud`), dengan autentikasi API key dan
migrasi schema lewat Alembic.

## Struktur

```
.
├── app/
│   ├── main.py        # endpoint FastAPI
│   ├── models.py       # tabel devices (state terkini) & telemetry (histori)
│   ├── schemas.py       # validasi request/response, field sesuai firmware
│   ├── auth.py           # shared device key + admin key
│   └── database.py       # koneksi async ke Postgres
├── alembic/                # migrasi schema database
├── Caddyfile                # reverse proxy + HTTPS otomatis
├── docker-entrypoint.sh     # jalankan migrasi lalu start API
├── Dockerfile
├── docker-compose.yml
├── .env.example
└── requirements.txt
```

## Setup awal

**1. DNS dulu** — sebelum `docker compose up`, pastikan subdomain
`gps.jejakpintar.cloud` sudah punya DNS **A record** yang mengarah ke
IP VPS ini. Caddy butuh ini untuk minta sertifikat Let's Encrypt secara
otomatis lewat verifikasi HTTP-01 di port 80.

**2. Env**

```bash
cp .env.example .env
```

Isi `.env`:

- `POSTGRES_USER` — superuser Postgres (dipakai healthcheck & yang create
  database). Biarkan `postgres` kecuali punya alasan khusus.
- `POSTGRES_PASSWORD` — ganti dari default.
- `POSTGRES_DB` — nama database aplikasi, mis. `telemetry_gps`. Wajib sama
  di semua container (compose menyusun `DATABASE_URL` dari nilai ini).
- `DEVICE_API_KEY` — shared secret untuk **semua** device di fleet.
  Generate dengan:
  ```bash
  python3 -c "import secrets; print(secrets.token_urlsafe(32))"
  ```
- `ADMIN_API_KEY` — untuk baca data (dashboard/admin). Generate dengan
  cara yang sama, pakai key **berbeda** dari `DEVICE_API_KEY`.
- `DOMAIN` — sudah di-set ke `gps.jejakpintar.cloud`, biarkan saja
  kecuali mau testing lokal (ganti ke `localhost`).

## Menjalankan

```bash
docker compose up -d --build
docker compose ps   # pastikan db "healthy", lalu api dan caddy jalan
```

Migrasi database jalan otomatis tiap kali container `api` start.

```bash
curl https://gps.jejakpintar.cloud/health
```

(Tidak perlu `-k` lagi karena sertifikatnya dari Let's Encrypt, valid
publik — beda dari waktu testing lokal pakai `localhost` sebelumnya.)

## Kirim data (dari device)

Device **tidak perlu registrasi terpisah** — baris di tabel `devices`
otomatis dibuat/diupdate berdasarkan `sn`, persis seperti pola satu
dokumen Firestore per `sn` yang sudah kamu pakai sekarang.

```bash
curl -X POST https://gps.jejakpintar.cloud/telemetry \
  -H "X-API-Key: <isi DEVICE_API_KEY dari .env>" \
  -H "Content-Type: application/json" \
  -d '{
        "sn": "ESP32-01",
        "lat": -6.200000,
        "lng": 106.816666,
        "speedKnots": 5.2,
        "heading": 120,
        "timestamp": "2026-09-09T10:15:00Z",
        "name": "Kapal Nelayan 1",
        "powerSource": "BATTERY",
        "dataSource": "GSM"
      }'
```

Untuk jalur WiFi, tambahkan `"status": "active"` di body — field ini
opsional dan cuma dipakai khusus jalur WiFi sesuai spesifikasi asli.

Field yang **wajib**: `sn`, `lat`, `lng`, `timestamp`, `dataSource`.
Field opsional (`speedKnots`, `heading`, `name`, `powerSource`,
`status`) boleh diabaikan device kalau memang belum tersedia —
`speedKnots` dan `heading` default ke `0` kalau tidak dikirim.

Response-nya balikan **state terkini** device tersebut (posisi +
status terbaru), berguna kalau firmware mau langsung konfirmasi data
tersimpan tanpa GET terpisah.

## Baca data (admin/dashboard)

State terkini semua device:

```bash
curl https://gps.jejakpintar.cloud/devices \
  -H "X-Admin-Key: <isi ADMIN_API_KEY dari .env>"
```

State terkini satu device:

```bash
curl https://gps.jejakpintar.cloud/devices/ESP32-01 \
  -H "X-Admin-Key: <isi ADMIN_API_KEY dari .env>"
```

Riwayat/histori posisi (untuk replay rute perjalanan):

```bash
curl "https://gps.jejakpintar.cloud/telemetry/ESP32-01/history?limit=100" \
  -H "X-Admin-Key: <isi ADMIN_API_KEY dari .env>"
```

## Menambah migrasi baru (kalau schema berubah nanti)

```bash
# ubah dulu app/models.py, lalu:
docker compose exec api alembic revision --autogenerate -m "deskripsi perubahan"
docker compose exec api alembic upgrade head
```

Commit file migrasi baru yang muncul di `alembic/versions/` ke git.

## Ringkasan desain

- **`devices`** = state/posisi terkini per `sn`, setara satu dokumen
  Firestore — diupdate tiap kali ada data masuk.
- **`telemetry`** = log historis lengkap, satu baris per transmisi —
  dipakai untuk replay rute, bukan cuma "posisi sekarang".
- **Auth dua lapis**: `X-API-Key` (shared, dipakai semua device untuk
  kirim data) dan `X-Admin-Key` (dipakai dashboard/admin untuk baca
  data). Device tidak bisa baca data device lain karena tidak pegang
  admin key.
- **HTTPS otomatis** lewat Caddy + domain `gps.jejakpintar.cloud` —
  tidak perlu setup certbot manual, port 8000/5432 tidak expose
  langsung ke internet.

Yang masih di luar scope starter ini: rate limiting per-device, dan
backup terjadwal untuk volume `db_data` (mis. `pg_dump` via cron).
