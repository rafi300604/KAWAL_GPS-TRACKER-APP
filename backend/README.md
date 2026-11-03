# KAWAL GPS — Backend

Backend sistem pelacak KAWAL (skripsi). Menerima telemetry dari alat ESP32 + modem SIMCom A7670E lewat
**MQTT (TLS)**, menyimpannya ke **PostgreSQL**, mengirim **perintah dua arah** ke alat, mencatat status
**online/offline** alat, dan menyediakan **REST API (FastAPI)** untuk aplikasi Flutter KAWAL
([rafi300604/KAWAL_GPS-TRACKER-APP](https://github.com/rafi300604/KAWAL_GPS-TRACKER-APP)).

Produksi: `https://mqttskripsigps.jejakpintar.cloud` (API) dan `mqttskripsigps.jejakpintar.cloud:8883`
(broker MQTT untuk alat).

## Arsitektur

```
                      MQTT over TLS :8883 (user/pass + ACL per client id)
┌──────────────────┐  telemetry/<SN>  status/<SN>  cmd-ack/<SN>   ┌───────────────────────────────┐
│ ESP32 + A7670E   │ ──────────────────────────────────────────▶ │  mosquitto  (kawal-gps-mqtt)   │
│ GSM utama,       │ ◀────────────────────────────────────────── │  :8883 publik, :1883 internal  │
│ WiFi cadangan    │                 cmd/<SN>                    └───────────────┬───────────────┘
└──────────────────┘                                                 :1883 (network Docker internal)
                                                                                 │
┌──────────────────┐  HTTPS (REST, JSON)   ┌──────────────┐        ┌─────────────▼────────────────┐
│ App Flutter      │ ────────────────────▶ │ nginx (VPS)  │ ─────▶ │  FastAPI  (kawal-gps-api)     │
│ KAWAL (web/iOS/  │  X-Admin-Key / JWT    │ reverse proxy│        │  + subscriber MQTT (paho)     │
│ Android)         │                       └──────────────┘        └─────────────┬────────────────┘
└──────────────────┘                                                             │
                                                                 ┌───────────────▼──────────────┐
                                                                 │ PostgreSQL 16 (kawal-gps-db)  │
                                                                 └──────────────────────────────┘
```

- **Alat → broker.** Alat publish ke listener publik `8883` (TLS + username/password). ACL membatasi tiap
  alat hanya ke topic miliknya sendiri (`%c` = client id = SN).
- **Broker → backend.** Container `api` subscribe lewat listener internal `1883` (anonymous, tidak
  di-publish ke host). Payload di-decode oleh `app/telemetry.py:decode_compact_telemetry`, lalu disimpan.
- **Perintah.** Backend publish perintah ke `cmd/<SN>` (retained, QoS 1). Alat membalas ke `cmd-ack/<SN>`.
- **Status koneksi.** Alat publish `online` ke `status/<SN>` setiap selesai connect dan memasang Last Will
  `offline`. Kalau alat putus tanpa DISCONNECT (daya dicabut, sinyal hilang), broker yang menerbitkan
  `offline`.
- **App → API.** App memakai REST lewat HTTPS. Di produksi, API berada di belakang nginx milik VPS
  (network Docker `devops_default`). `Caddyfile` hanya contoh alternatif untuk deploy mandiri; tidak
  dipakai `docker-compose.yml`.
- **`POST /telemetry` (HTTPS)** masih diterima sebagai jalur alternatif (format JSON sama, field `d`). Firmware
  saat ini mengirim lewat MQTT di jalur GSM maupun WiFi.

## Struktur

```
.
├── app/
│   ├── main.py             # endpoint FastAPI (REST)
│   ├── gps_mqtt_client.py  # subscriber MQTT: telemetry, cmd-ack, status + pengirim perintah
│   ├── telemetry.py        # decode payload 17 byte, simpan telemetry, buat alert, status online
│   ├── commands.py         # validasi & siklus hidup perintah ke alat
│   ├── models.py           # tabel SQLAlchemy
│   ├── schemas.py          # skema request/response (Pydantic)
│   ├── auth.py             # X-API-Key, X-Admin-Key, JWT login
│   └── database.py         # koneksi async ke Postgres
├── alembic/versions/       # migrasi schema 0001–0009
├── mqtt/config/            # mosquitto.conf + acl (template; passwd & certs/ dibuat di VPS)
├── docker-compose.yml      # db + api + mqtt
├── docker-entrypoint.sh    # buat database kalau belum ada, migrasi, lalu start API
├── Dockerfile
├── Caddyfile               # contoh reverse proxy untuk deploy mandiri (tidak dipakai compose)
├── .env.example
└── requirements.txt
```

## Database

| Tabel | Isi |
|---|---|
| `devices` | State terkini per `sn`: posisi, kecepatan, arah, sumber data/daya, `tamper`, `online` |
| `telemetry` | Riwayat lengkap, satu baris per titik (dipakai replay rute dan ekspor Excel) |
| `device_alerts` | Notifikasi: `tamper`, `low_quota`, `remote_alarm`, `offline`, `online` |
| `device_commands` | Perintah ke alat dan statusnya |
| `users` | Akun aplikasi (login JWT, role `admin` / `user`) |
| `waypoints` | Titik tujuan per alat untuk fitur ETA |

Migrasi Alembic berjalan otomatis setiap container `api` start.

## Autentikasi

Tiga skema, jangan tertukar:

| Header | Dipakai oleh | Untuk |
|---|---|---|
| `X-API-Key` (`DEVICE_API_KEY`) | Alat (jalur HTTPS) | `POST /telemetry` |
| `X-Admin-Key` (`ADMIN_API_KEY`) | App / admin | Baca device, riwayat, alert; kirim perintah; hapus device |
| `Authorization: Bearer <JWT>` | App (per pengguna) | `/users*`, `/devices/{sn}/waypoints*` |

Jalur MQTT tidak memakai header di atas: alat login ke broker dengan username/password (`mqtt/config/passwd`).

## REST API

| Method | Path | Auth | Keterangan |
|---|---|---|---|
| GET | `/health` | – | Cek hidup |
| POST | `/telemetry` | X-API-Key | Telemetry lewat HTTPS (`{"sn": "...", "d": "<34 hex>"}`) |
| GET | `/devices` | X-Admin-Key | State terkini semua device (termasuk `online`, `tamper`) |
| GET | `/devices/{sn}` | X-Admin-Key | State terkini satu device |
| DELETE | `/devices/{sn}` | X-Admin-Key | Hapus device beserta seluruh datanya |
| GET | `/telemetry/{sn}/history` | X-Admin-Key | Riwayat. Tanpa `since`: `limit` titik terbaru (maks 500). Dengan `since=<ISO-8601>`: semua titik sejak waktu itu (maks 20000) |
| GET | `/devices/{sn}/alerts` | X-Admin-Key | Notifikasi terbaru (`limit`, maks 100) |
| POST | `/devices/{sn}/commands` | X-Admin-Key | Kirim perintah ke alat |
| GET | `/devices/{sn}/commands` | X-Admin-Key | Status perintah (`limit`, maks 100) |
| POST | `/auth/login` | – | Email + password → JWT (berlaku 30 hari) |
| GET / PATCH | `/users/me` | JWT | Profil sendiri |
| GET / POST | `/users` | JWT admin | Daftar / buat pengguna |
| PATCH / DELETE | `/users/{id}` | JWT admin | Ubah / hapus pengguna |
| GET / POST | `/devices/{sn}/waypoints` | JWT | Daftar / tambah waypoint |
| DELETE | `/devices/{sn}/waypoints/{id}` | JWT | Hapus waypoint |

Contoh:

```bash
DOMAIN=mqttskripsigps.jejakpintar.cloud

curl https://$DOMAIN/health
curl https://$DOMAIN/devices -H "X-Admin-Key: <ADMIN_API_KEY>"
curl "https://$DOMAIN/telemetry/ESP32-01/history?since=2026-11-01T00:00:00Z&limit=20000" \
  -H "X-Admin-Key: <ADMIN_API_KEY>"
curl "https://$DOMAIN/devices/ESP32-01/alerts?limit=20" -H "X-Admin-Key: <ADMIN_API_KEY>"

curl -X POST https://$DOMAIN/auth/login -H "Content-Type: application/json" \
  -d '{"email": "skripsi@tester.com", "password": "<password akun>"}'
# -> {"accessToken": "...", "tokenType": "bearer", "user": {...}}
```

Device **tidak perlu registrasi**: baris `devices` dibuat/diupdate otomatis berdasarkan `sn`.

## MQTT

| Topic | Arah | Isi |
|---|---|---|
| `telemetry/<SN>` | alat → backend | Hex mentah 34 karakter (17 byte), **bukan** JSON |
| `status/<SN>` | alat → backend | `online` setelah connect; Last Will `offline` (QoS 1) |
| `cmd/<SN>` | backend → alat | Perintah JSON, retained + QoS 1 (lihat di bawah) |
| `cmd-ack/<SN>` | alat → backend | Balasan perintah: `applied` / `reset` / `rejected` |

### Payload telemetry (17 byte, big-endian)

| Byte | Isi | Tipe | Catatan |
|---|---|---|---|
| 0–3 | lat | int32 | nilai asli = int32 / 1.000.000 |
| 4–7 | lng | int32 | nilai asli = int32 / 1.000.000 |
| 8–9 | heading | uint16 | derajat 0–359 |
| 10–11 | speedKnots | uint16 | nilai asli = uint16 / 100 |
| 12–15 | timestamp | uint32 | UNIX epoch UTC (detik), dari jam GPS |
| 16 | flags | uint8 | bit0 dataSource (0=GSM, 1=WIFI); bit1 kuota diduga habis; bit2 powerSource (0=BATTERY, 1=AKI); bit3 tamper; bit4 tamper uji (`test_tamper`); bit5–7 cadangan |

Contoh `ffa16540065da380002d03526aa91aa400` → lat −6,2; lng 106,8; heading 45°; 8,5 knot;
2026-09-15T10:15:00Z; flags 0.

### Alert yang dibuat backend

| `type` | Pemicu |
|---|---|
| `tamper` | Setiap titik dengan bit3=1 (tanpa cooldown — kejadian kritis). Bit4=1 → pesan berawalan `[UJI]` |
| `low_quota` | `dataSource` mengandung "kuota" (bit1=1 di-decode jadi `WIFI (GSM: kuota diduga habis)`). Cooldown 12 jam per device |
| `remote_alarm` | Alat membalas `applied` untuk perintah `buzzer` |
| `offline` | `status/<SN>` = `offline` dan tidak disusul `online` dalam 5 detik |
| `online` | `status/<SN>` = `online` (atau telemetry masuk) setelah device tercatat offline |

Waktu `offline` muncul ≈ 1,5 × keepalive (GSM 30 s, WiFi 15 s) + 5 s debounce. Debounce mencegah
alarm palsu saat sesi lama digantikan sesi baru dengan client id yang sama (GSM ↔ WiFi).

## Perintah ke alat (MQTT dua arah)

| Perintah | Isi | Efek di alat |
|---|---|---|
| `interval` | `{"cmd":"interval","interval_s":30,"duration_min":60}` | Interval kirim jadi `interval_s` detik (2–1800) di semua mode selama `duration_min` menit (1–180), lalu kembali normal. Reboot juga membatalkannya. |
| `reset` | `{"cmd":"reset"}` | Batalkan override interval sekarang juga. |
| `buzzer` | `{"cmd":"buzzer","duration_s":10}` | Bunyikan buzzer 1–20 detik. Setelah `applied`, backend membuat alert `remote_alarm`. Ditolak kalau alarm tamper asli sedang aktif. |
| `test_tamper` | `{"cmd":"test_tamper"}` | Simulasi tamper: buzzer berbunyi dan alat langsung mengirim satu titik tamper bertanda uji (bit4). |

```bash
curl -X POST https://$DOMAIN/devices/ESP32-01/commands \
  -H "X-Admin-Key: <ADMIN_API_KEY>" -H "Content-Type: application/json" \
  -d '{"cmd":"interval","interval_s":30,"duration_min":60}'
curl https://$DOMAIN/devices/ESP32-01/commands -H "X-Admin-Key: <ADMIN_API_KEY>"
```

Atau langsung dari database — backend mengambil baris baru dalam ≤ 10 detik:

```sql
INSERT INTO device_commands (device_sn, cmd, interval_s, duration_min) VALUES ('ESP32-01', 'interval', 30, 60);
INSERT INTO device_commands (device_sn, cmd, duration_s) VALUES ('ESP32-01', 'buzzer', 10);
```

Status: `pending` → `sent` → `applied` / `reset` / `rejected`, atau `invalid` (nilai di luar batas),
`superseded` (digantikan perintah yang lebih baru), `expired` (tidak dibalas dalam 60 menit).

Pengiriman: perintah dipublish **retained** dan diulang tiap 2 menit sampai dibalas. Alat memeriksa
koneksi MQTT kira-kira tiap 60 detik dan menyambung ulang kalau putus, jadi perintah biasanya diterima
dalam ±1 menit. Setelah dibalas atau kedaluwarsa, retained dihapus.

## Setup

### 1. Env

```bash
cp .env.example .env
```

- `POSTGRES_USER`, `POSTGRES_PASSWORD`, `POSTGRES_DB` — compose menyusun `DATABASE_URL` dari nilai ini.
- `DEVICE_API_KEY`, `ADMIN_API_KEY`, `JWT_SECRET` — tiga nilai acak yang **berbeda**:
  ```bash
  python3 -c "import secrets; print(secrets.token_urlsafe(32))"
  ```
- `GPS_MQTT_BROKER` / `GPS_MQTT_INTERNAL_PORT` — default `mqtt` / `1883` (broker di stack ini).
- `MQTT_CONFIG_DIR` (opsional) — folder config broker di host, default `/opt/kawal-gps/mqtt/config`.

### 2. Broker MQTT (sekali, di VPS)

File berikut berisi rahasia, dibuat langsung di VPS dan **tidak pernah di-commit** (sudah di `.gitignore`):

```bash
# password alat (satu kredensial bersama untuk semua alat)
docker run --rm -v "/opt/kawal-gps/mqtt/config:/mosquitto/config" eclipse-mosquitto:2.1.2-alpine \
  mosquitto_passwd -b -c /mosquitto/config/passwd <USERNAME> <PASSWORD>

# sertifikat TLS self-signed
mkdir -p /opt/kawal-gps/mqtt/config/certs && cd /opt/kawal-gps/mqtt/config/certs
openssl req -x509 -nodes -newkey rsa:2048 -days 3650 \
  -keyout server.key -out server.crt -subj "/CN=<domain broker>"
cp server.crt ca.crt
```

Salin juga `mqtt/config/mosquitto.conf` dan `mqtt/config/acl` dari repo ini ke folder yang sama. ACL wajib
memuat keempat baris berikut. Tanpa `status/%c`, broker menolak koneksi alat yang memasang Last Will.

```
pattern write telemetry/%c
pattern write cmd-ack/%c
pattern read cmd/%c
pattern write status/%c
```

Setelah mengubah ACL: `docker kill -s HUP kawal-gps-mqtt` (atau restart container).

### 3. Jalankan

```bash
docker compose up -d --build
docker compose ps        # db "healthy", lalu api dan mqtt jalan
curl https://<domain>/health
```

### Deploy produksi

1. Push ke `main` repo ini.
2. Portainer → stack KAWAL → **Pull and redeploy**.
3. `docker exec devops-pl-nginx nginx -s reload`. nginx menyimpan IP lama `kawal-gps-api`, jadi tanpa
   reload hasilnya 502.
4. Cek `curl https://mqttskripsigps.jejakpintar.cloud/health` → `200`.

### Menambah migrasi

```bash
# ubah app/models.py dulu, lalu:
docker compose exec api alembic revision --autogenerate -m "deskripsi perubahan"
docker compose exec api alembic upgrade head
```

Commit file baru di `alembic/versions/`.

## Keterbatasan yang diketahui

- Semua alat memakai satu kredensial MQTT bersama. Pemegang kredensial itu bisa memakai client id alat lain.
  Perbaikannya: username per alat + ACL `%u`.
- `ADMIN_API_KEY` ikut tertanam di build app (endpoint baca device tidak memakai auth per pengguna), jadi
  build app tidak boleh disebarkan publik.
- Belum ada rate limiting per alat dan backup terjadwal volume database (mis. `pg_dump` via cron).
