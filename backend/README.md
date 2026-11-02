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

- `POSTGRES_USER` — user Postgres (mis. `kawal_gps_user`).
- `POSTGRES_PASSWORD` — ganti dari default.
- `POSTGRES_DB` — nama database aplikasi, mis. `kawal_gps_db`. Wajib sama
  di semua container (compose menyusun `DATABASE_URL` dari nilai ini).
- `DEVICE_API_KEY` — shared secret untuk **semua** device di fleet.
  Generate dengan:
  ```bash
  python3 -c "import secrets; print(secrets.token_urlsafe(32))"
  ```
- `ADMIN_API_KEY` — untuk baca data (dashboard/admin). Generate dengan
  cara yang sama, pakai key **berbeda** dari `DEVICE_API_KEY`.
- `JWT_SECRET` — buat menandatangani token login pengguna (`POST
  /auth/login`). Generate dengan cara yang sama, pakai key **berbeda**
  dari dua di atas.
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

`dataSource` **tidak selalu cuma `"GSM"`/`"WIFI"` polos** (maks. 50
karakter) — firmware (`skripsi.ino`) juga bisa kirim string deskriptif
seperti `"WIFI (GSM: kuota diduga habis)"` kalau mendeteksi sesi data GSM
gagal berkali-kali padahal jaringan seluler terdaftar normal (heuristik
kuota SIM habis — modem tidak punya cara membaca sisa kuota asli dari
operator). Kalau string ini mengandung kata "kuota", backend otomatis
mencatat satu baris di `device_alerts` (lihat `GET /devices/{sn}/alerts`
di bawah) — dengan cooldown 12 jam per device supaya tidak spam satu baris
per transmisi selama kondisinya masih berlangsung. Lihat
`FIRMWARE_CONTRACT.md` untuk detail lengkap.

Response-nya balikan **state terkini** device tersebut (posisi +
status terbaru), berguna kalau firmware mau langsung konfirmasi data
tersimpan tanpa GET terpisah.

## Kirim data lewat MQTT (jalur GSM/seluler, hemat kuota)

Alternatif dari HTTPS di atas, khusus device yang kirim lewat modem
seluler (AT+HTTPS buka sesi TLS baru tiap kirim — boros kuota untuk
device yang kirim sering; MQTT cuma sekali connect, lalu publish
berkali-kali).

Topic: `telemetry/<SN>` (mis. `telemetry/ESP32-01`). Payload: HEX MENTAH
34 karakter (BUKAN dibungkus JSON) — identitas device datang dari topic,
bukan diulang di payload. Format byte persis sama dengan field `d` di
endpoint HTTPS di atas (lihat FIRMWARE_CONTRACT.md untuk layout byte
lengkap) — backend decode pakai fungsi yang SAMA
(`app/telemetry.py:decode_compact_telemetry`), jadi hasilnya identik
apa pun jalur transportnya (termasuk alert kuota/tamper).

### Setup broker MQTT (sekali saja, di VPS)

**1. Password file** (autentikasi device — dipakai firmware, BUKAN
backend; backend konek lewat listener internal anonymous):

```bash
mkdir -p mqtt/config/certs
docker run --rm -v "$(pwd)/mqtt/config:/mosquitto/config" eclipse-mosquitto:2 \
  mosquitto_passwd -b -c /mosquitto/config/passwd <USERNAME> <PASSWORD>
```

Satu kredensial dipakai bersama semua device di fleet (sama seperti
`DEVICE_API_KEY` di jalur HTTPS). Generate password acak sendiri, JANGAN
commit nilai asli ke repo (repo ini publik):

```bash
python3 -c "import secrets; print(secrets.token_urlsafe(24))"
```

(Berikan `<USERNAME>` dan password hasil generate di atas ke firmware
untuk diisi ke `MQTT_USERNAME`/`MQTT_PASSWORD` di `skripsi.ino` — simpan
di tempat lain, bukan di repo ini.)

**2. Sertifikat TLS** — self-signed cukup untuk skala thesis (device
perlu dikonfigurasi skip verifikasi hostname/CA, bukan cert asli dari
CA publik):

```bash
openssl req -x509 -nodes -newkey rsa:2048 -days 3650 \
  -keyout mqtt/config/certs/server.key \
  -out mqtt/config/certs/server.crt \
  -subj "/CN=<domain broker ini>"
cp mqtt/config/certs/server.crt mqtt/config/certs/ca.crt
```

**3. `docker compose up -d`** untuk deploy — port 8883 (publik,
TLS+auth, dipakai device) langsung ikut jalan bareng service lain.

**Penting**: `mqtt/config/passwd` dan `mqtt/config/certs/` sengaja
di-`.gitignore` — ini kredensial/private key asli, jangan pernah commit
ke git.

**4. ACL** — batasi tiap device ke topic miliknya sendiri. Salin
`mqtt/config/acl` (tidak berisi rahasia, ada di repo) ke folder config broker
di VPS, pastikan `mosquitto.conf` di VPS punya `acl_file
/mosquitto/config/acl` di bawah `listener 8883` (lihat template
`mqtt/config/mosquitto.conf`), lalu `docker restart kawal-gps-mqtt`. Hasilnya
user device hanya boleh menulis `telemetry/<SN>` & `cmd-ack/<SN>` dan membaca
`cmd/<SN>` (SN = client id); device tidak bisa mengirim perintah atau membaca
data device lain. Listener internal 1883 (backend) sengaja tanpa ACL — tidak
terbuka ke internet.

## Perintah ke device (MQTT dua arah)

Server bisa mengirim perintah ke device lewat topic `cmd/<SN>`; device
membalas di `cmd-ack/<SN>`. Saat ini ada empat perintah:

| Perintah | Isi | Efek di device |
|---|---|---|
| `interval` | `{"cmd":"interval","interval_s":30,"duration_min":60}` | Interval kirim jadi `interval_s` detik (2–1800) di semua mode selama `duration_min` menit (1–180), lalu kembali normal sendiri. Reboot juga membatalkannya. |
| `reset` | `{"cmd":"reset"}` | Batalkan override interval sekarang juga. |
| `buzzer` | `{"cmd":"buzzer","duration_s":10}` | Bunyikan buzzer `duration_s` detik (1–20), bukan kejadian tamper. Setelah device membalas `applied`, backend membuat alert `remote_alarm` ("Alarm jarak jauh ...") yang memunculkan popup + suara di app. Ditolak kalau alarm tamper asli sedang aktif. |
| `test_tamper` | `{"cmd":"test_tamper"}` | Simulasi tamper: buzzer berbunyi seperti tamper asli dan device langsung mengirim satu titik tamper bertanda uji (flags bit4). Backend membuat alert `tamper` biasa (alarm app tetap bunyi) dengan pesan berawalan `[UJI]`. |

Kirim (hanya pemegang `X-Admin-Key`):

```bash
curl -X POST https://<domain>/devices/ESP32-01/commands \
  -H "X-Admin-Key: <ADMIN_API_KEY>" -H "Content-Type: application/json" \
  -d '{"cmd":"interval","interval_s":30,"duration_min":60}'

# cek status
curl https://<domain>/devices/ESP32-01/commands -H "X-Admin-Key: <ADMIN_API_KEY>"
```

Atau langsung dari database (mis. DBeaver) — backend mengambil baris baru
ini dalam ≤10 detik:

```sql
INSERT INTO device_commands (device_sn, cmd, interval_s, duration_min)
VALUES ('ESP32-01', 'interval', 30, 60);

INSERT INTO device_commands (device_sn, cmd, duration_s)
VALUES ('ESP32-01', 'buzzer', 10);
```

Status di tabel `device_commands`: `pending` → `sent` → `applied` / `reset` /
`rejected` (dibalas device), atau `invalid` (nilai di luar batas, tidak
dikirim), `superseded` (digantikan perintah yang lebih baru untuk device yang
sama), `expired` (tidak dibalas dalam 60 menit).

Cara kerja pengiriman: perintah dipublish **retained** (QoS 1) dan diulang tiap
2 menit sampai dibalas, karena lewat GSM device hanya tersambung ke broker saat
mengirim telemetry (di mode SLEEP bisa 30 menit sekali). Begitu device
subscribe lagi, broker langsung menyerahkan perintah terakhir; setelah dibalas
atau kedaluwarsa, retained dihapus. Jadi perintah ke device yang sedang SLEEP
bisa baru diterima sampai ±30 menit kemudian.

Keterbatasan yang diketahui: semua device memakai satu kredensial MQTT bersama,
jadi pemegang kredensial itu bisa memakai client id device lain (menyamar).
Perbaikannya: username per device + ACL `%u`.

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

Riwayat/histori posisi (untuk replay rute perjalanan, dan ekspor Excel di
app). Tiap baris ikut menyimpan `dataSource` dan `powerSource` PADA SAAT
titik itu direkam (bukan cuma nilai terkini device) — baris lama yang
direkam sebelum kolom `power_source` ditambahkan (migrasi 0004) balik
`powerSource: null`, itu bukan bug:

```bash
curl "https://gps.jejakpintar.cloud/telemetry/ESP32-01/history?limit=100" \
  -H "X-Admin-Key: <isi ADMIN_API_KEY dari .env>"
```

Riwayat notifikasi otomatis device (mis. kuota SIM diduga habis — lihat
catatan `dataSource` di atas):

```bash
curl "https://gps.jejakpintar.cloud/devices/ESP32-01/alerts?limit=20" \
  -H "X-Admin-Key: <isi ADMIN_API_KEY dari .env>"
```

## Login & manajemen pengguna

Menggantikan Firebase Auth + Firestore `users/{uid}` sepenuhnya — login
pakai email/password, dapat JWT (`Authorization: Bearer <token>`, berlaku
30 hari, tidak ada refresh-token flow).

```bash
curl -X POST https://gps.jejakpintar.cloud/auth/login \
  -H "Content-Type: application/json" \
  -d '{"email": "skripsi@tester.com", "password": "<password akun>"}'
# -> {"accessToken": "...", "tokenType": "bearer", "user": {...}}
```

Profil sendiri (siapa saja yang login):

```bash
curl https://gps.jejakpintar.cloud/users/me -H "Authorization: Bearer <token>"
curl -X PATCH https://gps.jejakpintar.cloud/users/me \
  -H "Authorization: Bearer <token>" -H "Content-Type: application/json" \
  -d '{"name": "Nama Baru"}'
```

Manajemen pengguna (admin-only, `role='admin'` pada token):

```bash
curl https://gps.jejakpintar.cloud/users -H "Authorization: Bearer <admin-token>"

curl -X POST https://gps.jejakpintar.cloud/users \
  -H "Authorization: Bearer <admin-token>" -H "Content-Type: application/json" \
  -d '{"name": "User Baru", "email": "user@contoh.com", "password": "rahasia123", "role": "user"}'

curl -X PATCH https://gps.jejakpintar.cloud/users/3 \
  -H "Authorization: Bearer <admin-token>" -H "Content-Type: application/json" \
  -d '{"name": "Nama Diedit", "role": "admin"}'

curl -X DELETE https://gps.jejakpintar.cloud/users/3 -H "Authorization: Bearer <admin-token>"
```

`role` cuma `"admin"` atau `"user"` (bukan `"device"` lagi — device
otentikasi pakai `DEVICE_API_KEY`, bukan akun pengguna).

## Waypoints

Menggantikan Firestore `devices/{sn}/waypoints` — siapa saja yang login
boleh baca/tulis (tidak dibatasi role, sama seperti rule Firestore lama).

```bash
curl https://gps.jejakpintar.cloud/devices/ESP32-01/waypoints \
  -H "Authorization: Bearer <token>"

curl -X POST https://gps.jejakpintar.cloud/devices/ESP32-01/waypoints \
  -H "Authorization: Bearer <token>" -H "Content-Type: application/json" \
  -d '{"name": "Tujuan", "lat": -6.2, "lng": 106.8}'

curl -X DELETE https://gps.jejakpintar.cloud/devices/ESP32-01/waypoints/1 \
  -H "Authorization: Bearer <token>"
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
- **`users`** = akun app (login), **`waypoints`** = titik tujuan per
  device untuk fitur ETA — dua-duanya menggantikan Firestore sepenuhnya.
- **`device_alerts`** = riwayat notifikasi otomatis (mis. kuota SIM diduga
  habis), dibuat backend sendiri (bukan ditulis firmware langsung) saat
  `dataSource` di `/telemetry` mengandung kata kunci peringatan — lihat
  catatan `dataSource` di bagian "Kirim data" di atas.
- **Tiga skema auth berbeda, jangan tertukar**: `X-API-Key` (shared,
  device kirim data), `X-Admin-Key` (shared, dashboard/admin baca data
  device/telemetry/alerts), dan `Authorization: Bearer <JWT>` (per-user,
  dari `/auth/login`, dipakai endpoint `/users/*` dan `/devices/*/waypoints`).
- **HTTPS otomatis** lewat Caddy + domain `gps.jejakpintar.cloud` —
  tidak perlu setup certbot manual, port 8000/5432 tidak expose
  langsung ke internet.

Yang masih di luar scope starter ini: rate limiting per-device, dan
backup terjadwal untuk volume `db_data` (mis. `pg_dump` via cron).
