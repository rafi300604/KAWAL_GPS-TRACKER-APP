# ESP32 ↔ Backend Contract

## ✅ STATUS PER 2026-10-03 — DOMAIN & REPO BARU, SEMUA FITUR DI BAWAH SUDAH LIVE

Backend pindah dari `gps.jejakpintar.cloud` (fork `ghamiDev/Rafi.git`, co-hosting
project BLE-scan temen) ke domain+repo baru SENDIRI: **`https://mqttskripsigps.jejakpintar.cloud`**,
kode di `https://github.com/rafi300604/kawal-gps-backend` (private, milik Rafi
sendiri). Alasan pindah: nambah broker MQTT di stack lama bentrok sama
`mosquitto.conf` punya project BLE-scan, bikin production down total — jadi
dipisah total (repo, container/stack, domain) biar tidak saling ganggu lagi.

Firmware (`skripsi.ino`, `BACKEND_HOST`) dan app (`lib/config/backend_config.dart`)
SUDAH dua-duanya mengarah ke domain baru ini — tidak perlu diubah lagi.
Sempat ada downtime (domain resolve tapi server tidak merespons sama sekali,
bukan 502) sambil nunggu Mas Fifit setup stack Portainer baru; **dikonfirmasi
LIVE lagi per 2026-10-03** setelah perubahan port internal di sisi VPS ("port
diubah ke 8555") - dites langsung: `/health` -> `200`, `/devices` -> `200`
(device `ESP32-01` muncul dengan field `tamper`), `POST /telemetry` format
`d` -> `200` dan ter-decode benar. **Kedua banner di bawah ("BELUM DI-DEPLOY
KE PRODUCTION") SUDAH RESOLVED** - format `d` DAN bit3 tamper dua-duanya
sudah live & terverifikasi di production (bukan cuma di repo lokal lagi).

Kalau backend sempat tidak merespons lagi di masa depan, cek dulu
`curl https://mqttskripsigps.jejakpintar.cloud/health` sebelum asumsi kode
backend yang salah - riwayatnya selalu soal infra VPS (routing/port/stack),
bukan bug di `backend/`.

---

## ⚠️ PERUBAHAN WIRE FORMAT PER 2026-09-15 — [RESOLVED 2026-10-03, lihat banner di atas]

Firmware (`skripsi.ino`) sudah diubah untuk kirim payload jauh lebih ringkas
(hemat kuota data seluler ~44% per kiriman: 315→176 byte termasuk header),
menggantikan seluruh field JSON lama (`lat`/`lng`/`heading`/`speedKnots`/
`timestamp`/`dataSource`/`powerSource`/`name`) dengan SATU field biner
ter-hex-encode. **Belum di-upload ke board fisik.**

Status decode di backend: `TelemetryIn` (`backend/app/schemas.py`) dan
`ingest_telemetry()` (`backend/app/main.py`) SUDAH mendukung field `d` ini
(fungsi `_decode_compact_telemetry()`), sekaligus tetap menerima format
lama field-per-field (dipilih otomatis: kalau `d` ada dipakai, kalau tidak
fallback ke lat/lng/dst lama) supaya curl/Postman/tooling lama di dokumen
ini tidak langsung rusak. **Tapi backend production di VPS
(`gps.jejakpintar.cloud`) menjalankan kode dari fork terpisah (repo
kolaborator, ada tambahan MQTT/BLE-scan yang tidak ada di repo ini) — jadi
perubahan decode ini BELUM otomatis ada di production.** Sebelum firmware
ini di-upload ke board fisik, pastikan dulu decode `d` sudah benar-benar
jalan di `gps.jejakpintar.cloud` (bisa dites: POST `/telemetry` dengan body
`{"sn": "TEST-XXX", "d": "..."}` dan `X-API-Key` asli, harus balik `200`
bukan `400`/`422`), bukan cuma di repo lokal ini.

Body baru:
```json
{"sn": "ESP32-01", "d": "ffa16540065da380002d03526aa91aa400"}
```
(contoh di atas decode ke lat=-6.2, lng=106.8, heading=45°, speedKnots=8.5,
timestamp=2026-09-15T10:15:00Z, flags=0 -> dataSource "GSM", powerSource
"BATTERY" -- diverifikasi round-trip lewat `_decode_compact_telemetry()` di
`backend/app/main.py`. Contoh SEBELUMNYA di draf ini, `fffce3f8...d7a301`,
ternyata 35 karakter/ganjil -- tidak valid untuk 17 byte, kemungkinan typo
saat digenerate -- sudah diganti.)

Field `d` = 17 byte biner (big-endian) di-hex-encode jadi 34 karakter:

| Byte | Isi | Tipe | Catatan |
|---|---|---|---|
| 0-3 | lat | int32 | nilai asli = int32 / 1.000.000 |
| 4-7 | lng | int32 | nilai asli = int32 / 1.000.000 |
| 8-9 | heading | uint16 | derajat 0-359 langsung |
| 10-11 | speedKnots | uint16 | nilai asli = uint16 / 100 |
| 12-15 | timestamp | uint32 | UNIX epoch UTC (detik) |
| 16 | flags | uint8 | bit0: dataSource (0=GSM,1=WIFI); bit1: quota_warning (1=kuota diduga habis, cuma valid kalau bit0=1); bit2: powerSource (0=BATTERY,1=AKI); bit3: tamper (1=alarm tamper aktif SAAT titik ini, HANYA untuk titik live -- lihat bagian tamper di bawah); bit4: tamper_test (1=titik tamper hasil perintah server `test_tamper`, simulasi -- cuma valid bersama bit3=1; backend memberi alert-nya awalan `[UJI]`); bit5-7: cadangan (harus 0) |

**Kenapa BUKAN hex-dari-JSON-utuh, dan bukan raw binary `application/octet-stream`
juga**: dianalisis dari referensi `vms_u30gt_iridium_final.ino` (tracker
satelit Iridium) — di situ hex CUMA representasi String sementara di dalam
kode, data yang benar-benar dikirim ke modem tetap byte mentah. Karena kita
pakai HTTP (bukan AT+SBDWB ke modem satelit), raw binary sebenarnya lebih
hemat lagi (tidak ada penggandaan 2x dari hex) — tapi hex-di-dalam-JSON
dipilih supaya: (1) tetap gampang di-debug pakai curl/Postman/log server
seperti kontrak lama (raw binary body susah dibaca manusia), (2) perubahan
di backend minimal (endpoint tetap terima JSON, cuma field-nya beda, tidak
perlu ganti Content-Type/parsing dari nol). `sn` tetap teks biasa di luar
blob hex (supaya backend bisa routing/cari device tanpa decode dulu
sekalipun devicenya belum dikenal). `name` sengaja TIDAK dikirim lagi tiap
kali (jarang berubah saat runtime) — backend HARUS pertahankan nilai `name`
lama di tabel `devices` kalau field ini tidak ada di payload (perilaku ini
SUDAH ADA: `if payload.name is not None`, tinggal pastikan tetap begitu).

**[RESOLVED 2026-10-03 — lihat banner status di atas, sudah live di production]**
Yang sudah dikerjakan di backend: `TelemetryIn.d` (opsional, hex string) ditambah di
`schemas.py`; `ingest_telemetry()` decode `d` lewat
`_decode_compact_telemetry()` KALAU field itu ada (kalau tidak, fallback ke
lat/lng/dst lama, dengan validasi manual field wajib supaya tetap balas
`400` yang jelas, bukan `422` generik), lalu lanjut ke logic
`ingest_telemetry()` yang SUDAH ADA tanpa perubahan lain (Postgres schema,
alert kuota, Excel export app — semua tetap jalan seperti biasa karena
cuma titik masuk parsing-nya yang berubah, bukan data yang tersimpan).
Sudah dites: schema Pydantic menerima kedua format, dan
`_decode_compact_telemetry()` di-round-trip-test manual (lihat contoh hex
di atas). **Belum dites lewat HTTP end-to-end** (server FastAPI beneran)
karena tidak bisa build image Docker lokal (masalah jaringan Docker
Desktop saat sesi ini), dan belum di-deploy ke production VPS.

Kontrak field-per-field LAMA di bawah ini (bagian 1) masih berlaku sebagai
ACUAN MAKNA tiap field (lat/lng/heading/dst) — cuma cara pengemasannya di
wire yang berubah jadi biner+hex di atas.

### Tamper (bit3) — status implementasi per 2026-09-21

Firmware kirim bit3=1 kalau alarm tamper (guncangan MPU6050) sedang aktif
SAAT titik itu direkam, HANYA untuk titik live (titik susulan dari buffer
offline selalu bit3=0 — supaya titik lama tidak salah ditandai "diusik").
Begitu alarm baru menyala (rising edge), firmware langsung paksa kirim
`/telemetry` saat itu juga, tidak menunggu interval transmisi biasa.

**[RESOLVED 2026-10-03 — lihat banner status di atas, sudah live di production]**
Backend:
- `Telemetry.tamper` (kolom baru, `models.py` + migrasi `0005_telemetry_tamper.py`)
  menyimpan status ini PER BARIS riwayat, pola yang sama seperti
  `power_source`/`data_source`.
- `TelemetryOut.tamper` diekspos di `/telemetry/{sn}/history` untuk
  fitur History app.
- `device_alerts` dapat baris baru type `"tamper"` saat rising edge
  terdeteksi SERVER-SIDE (baris tamper=true masuk padahal baris
  SEBELUMNYA untuk device itu tamper=false/belum ada) — BUKAN pakai
  cooldown 12 jam seperti alert kuota, karena tamper adalah kejadian
  diskrit/kritis: tiap episode baru yang benar-benar terpisah (didahului
  titik tamper=false) harus tetap ternotifikasi, bukan di-suppress.

(Catatan lama: sebelum upload firmware tamper ke board, pastikan dulu decode
bit3 live di production — sudah terpenuhi per banner status di atas.)

---

**Status per 2026-09-10: backend LIVE, jalur GSM socket-TLS TERBUKTI GAGAL
live, jalur GSM sudah dipindah ke AT+HTTPS native, WiFi disatukan ke backend
yang sama.**

`https://gps.jejakpintar.cloud/health`, `POST /telemetry`, `GET /devices`,
`GET /devices/{sn}`, dan `GET /telemetry/{sn}/history` semua sudah dites
langsung lewat curl dan balik `200` dengan data yang benar.

Setelah backend live, jalur GSM (waktu itu masih `TINY_GSM_MODEM_A76XXSSL`,
socket TLS mentah lewat `AT+CCHOPEN`/`CCHSEND`) diuji langsung di board fisik
dengan `DEVICE_API_KEY` asli (~44 karakter, header `X-API-Key`, payload
JSON pendek) — dan **gagal** persis dengan gejala yang sama seperti dua
percobaan sebelumnya (token JWT panjang, lalu API key pendek ke endpoint
belum live): `[Backend] /telemetry -> -1`, "respons kosong dari server",
sementara WiFi fallback ke request yang SAMA sukses `200` di detik yang
sama. Ini kondisi yang sebelumnya disyaratkan di catatan versi dokumen ini
sebelum boleh mundur ke AT+HTTPS native ("lakukan SETELAH backend hidup dan
jalur socket TLS saat ini terbukti jalan/gagal secara live") — sudah
terpenuhi, dan hasilnya GAGAL. Kesimpulan: masalahnya BUKAN panjang
parameter AT command (payload sekarang pendek), tapi mekanisme
`AT+CCHOPEN`/`CCHRECV` itu sendiri pada kondisi respons server yang cepat
menutup koneksi.

Firmware sekarang (`skripsi.ino`):
- `#define TINY_GSM_MODEM_A7670` (bukan `A76XXSSL` lagi) — jalur GSM pakai
  AT+HTTPS native bawaan modem (`modem.https_begin/set_url/add_header/post`)
  via fungsi `kirimKeBackendGSM()`. Ini mekanisme yang SUDAH TERBUKTI
  reliable di sesi pengujian sebelumnya (dipakai berhasil berkali-kali untuk
  request lain sebelum masalah panjang token JWT ditemukan) — dan sekarang
  viable lagi karena `X-API-Key` jauh di bawah batas 256-600 karakter
  AT+HTTPPARA.
- Jalur WiFi (`kirimKeBackendWiFi()`) sudah disatukan ke endpoint
  `/telemetry` yang SAMA dengan GSM (`HTTPClient` + `WiFiClientSecure`,
  header `X-API-Key` yang sama) — TIDAK lagi sign-in Firebase Auth atau
  menulis ke Firestore terpisah. Ini menutup bug data-split yang sebelumnya
  ada: dulu data GSM masuk ke Postgres sedangkan data WiFi cadangan masuk ke
  Firestore, dua database berbeda tergantung jalur mana yang kebetulan
  berhasil.
- `config.device_email`/`config.device_password` dan seluruh kode Firebase
  Auth (`firebaseSignInWiFi`, `ensureFreshTokenWiFi`, dsb.) sudah dihapus —
  tidak dipakai lagi sama sekali oleh jalur mana pun. `CONFIG_MAGIC` dinaikkan
  ke versi baru supaya EEPROM lama otomatis di-reset bersih, bukan salah baca.
- Compile bersih (`arduino-cli compile --fqbn esp32:esp32:esp32`). **Belum
  di-upload ulang ke board fisik** — menunggu board tersambung USB untuk
  upload & verifikasi log Serial Monitor.

## Kenapa pindah dari Firebase sepenuhnya?

Firestore REST API butuh Firebase Auth ID token (~900-1100 karakter) sebagai
header `Bearer` — itu bekerja baik-baik saja lewat WiFi, tapi jalur seluler
(GSM/modem A7670E, dikendalikan lewat perintah AT) berulang kali gagal:
parameter AT command (baik header maupun URL) dibatasi modem MAKSIMAL
256-600 karakter, jauh di bawah panjang token itu. Solusi awalnya (Cloud
Function di Firebase sebagai perantara, device cuma kirim API key pendek)
memang menghindari masalah panjang token itu, tapi Cloud Functions
mensyaratkan paket **Blaze** (berbayar, walau ada kuota gratis besar) yang
belum diaktifkan — jadi endpoint-nya tidak pernah benar-benar hidup.

Solusi sekarang: pindah ke backend FastAPI sendiri (`backend/`, dari
`https://github.com/ghamiDev/Rafi`), hosting dan domain (`gps.jejakpintar.cloud`)
diurus terpisah oleh kolaborator. Auth-nya tetap **API key pendek** (bukan
JWT panjang), jadi berlaku sama gampangnya lewat WiFi MAUPUN GSM — tidak ada
lagi batasan panjang parameter AT command yang perlu diakali.

## 1. Endpoint kirim data (dipanggil device)

```
POST https://mqttskripsigps.jejakpintar.cloud/telemetry
Content-Type: application/json
X-API-Key: <DEVICE_API_KEY dari .env backend>

{
  "sn": "ESP32-01",
  "lat": -6.200000,
  "lng": 106.800000,
  "speedKnots": 8.5,
  "heading": 45.0,
  "timestamp": "2026-09-09T10:15:00Z",
  "name": "Kapal 1",
  "powerSource": "BATTERY",
  "dataSource": "GSM"
}
```

Field wajib: `sn`, `lat`, `lng`, `timestamp`, `dataSource`. Field opsional:
`speedKnots`, `heading` (default `0` kalau tidak dikirim), `name`,
`powerSource`, `status` (biasanya `"active"`, khusus jalur WiFi kalau perlu).
`timestamp` dari jam satelit GPS (UTC ISO-8601), bukan NTP — sama seperti
kontrak lama.

**`powerSource` TIDAK PERNAH terkirim di praktiknya (ditemukan 2026-09-14)**
— dicek langsung ke database: dari 324 baris riwayat `ESP32-01` (mulai
2026-09-09 sampai transmisi PALING BARU per 2026-09-14, jauh setelah
backend siap menyimpan field ini per-transmisi lewat migrasi
`0004_telemetry_power_source.py`), **semuanya `NULL`** — bukan cuma data
lama, transmisi HARI INI pun tidak menyertakannya. Backend/app sudah
1000% siap terima dan tampilkan field ini (lihat `GET
/telemetry/{sn}/history` -> `powerSource`, dan kolom "Sumber Daya" di
ekspor Excel app) — yang kurang murni di sisi firmware: `skripsi.ino`
kelihatannya tidak pernah menyertakan `powerSource` di body JSON
`/telemetry`, padahal field ini SUDAH ada di kontrak sejak awal (bukan
field baru). Mohon ditambahkan pengiriman `powerSource` (mis. `"BATTERY"`
atau `"SOLAR"`, string bebas — backend tidak membatasi ke nilai tertentu,
cuma disimpan & ditampilkan apa adanya) di setiap POST `/telemetry`,
jalur GSM maupun WiFi, supaya riwayat/ekspor punya nilai per-titik yang
akurat alih-alih fallback ke nilai `devices.power_source` yang sekarang
terpaksa dipakai app (nilai lama yang entah kapan terakhir kali benar-benar
dikirim, sebelum riwayat 324 baris yang tercatat saat ini).

**`dataSource` bukan cuma `"GSM"`/`"WIFI"` polos** — firmware (`skripsi.ino`,
lihat `QUOTA_WARNING_THRESHOLD`) bisa juga mengirim
`"WIFI (GSM: kuota diduga habis)"` saat mendeteksi pola "jaringan seluler
terdaftar normal tapi sesi data GSM gagal dibuka berkali-kali berturut-turut"
— heuristik untuk kuota/pulsa data SIM habis (modem GSM TIDAK PUNYA cara
membaca sisa kuota langsung dari operator, jadi ini indikasi tidak langsung,
bukan kepastian). App/dashboard yang menampilkan `dataSource` sebaiknya
tampilkan string ini APA ADANYA (bukan diasumsikan cuma dua nilai tetap),
supaya peringatan ini kelihatan ke pengguna tanpa perlu ubah skema backend.

**Sudah diakomodasi di backend + app (2026-09-13), lihat migrasi
`0003_device_alerts.py`:**
- Kolom `data_source` di tabel `devices` DAN `telemetry` diperlebar dari
  `VARCHAR(10)` ke `VARCHAR(50)` — `VARCHAR(10)` yang lama akan bikin
  `/telemetry` GAGAL (`StringDataRightTruncation`, HTTP 500) begitu
  firmware kirim string sepanjang `"WIFI (GSM: kuota diduga habis)"` (31
  karakter). Kalau firmware butuh string LEBIH PANJANG dari 50 karakter di
  masa depan, kabari dulu supaya kolomnya diperlebar lagi sebelum dipakai —
  jangan asumsikan tidak ada batas.
- Setiap `/telemetry` yang `dataSource`-nya mengandung kata "kuota" (huruf
  besar/kecil diabaikan) otomatis dicatat backend sebagai satu baris di
  tabel `device_alerts` (cooldown 12 jam per device, supaya tidak spam satu
  baris per transmisi selama kondisinya masih berlangsung) — bisa dibaca
  lewat `GET /devices/{sn}/alerts` (lihat bagian 2 di bawah).
- App Flutter menampilkan `dataSource` apa adanya di baris "Sumber Data"
  (dengan ikon+warna peringatan kalau mengandung "kuota"), dan memunculkan
  notifikasi (SnackBar merah) begitu alert BARU muncul di
  `/devices/{sn}/alerts` — lihat `lib/screens/home_screen.dart:_watchAlertsFor`.
- **Kata kunci "kuota" ini kontrak yang harus disepakati** — kalau
  `QUOTA_WARNING_THRESHOLD`/pesan di firmware berubah formatnya, pastikan
  potongan string yang dikirim di `dataSource` tetap mengandung kata
  "kuota" (huruf besar/kecil bebas), atau kabari supaya
  `QUOTA_WARNING_KEYWORD` di `backend/app/main.py` dan
  `DeviceVessel.hasQuotaWarning` di app disesuaikan bersamaan.

Device **tidak perlu registrasi terpisah** — baris di tabel `devices`
otomatis dibuat/diupdate berdasarkan `sn`.

Response `200`: JSON state terkini device itu (posisi + status) — bisa
dipakai firmware untuk langsung konfirmasi data tersimpan tanpa GET
terpisah. `401` = `X-API-Key` salah/hilang. `400` = `sn`/`lat`/`lng` tidak
valid.

### PENTING — tidak ada `historyOnly` di backend ini

Beda dari kontrak Cloud Function lama: **setiap** POST ke `/telemetry` SELALU
meng-update state "posisi terkini" device SEKALIGUS menambah baris riwayat
— tidak ada flag untuk "cuma tambah riwayat, jangan timpa posisi terkini"
seperti `historyOnly` dulu.

Konsekuensi untuk logika buffer offline firmware (`flushBufferedPoints()`):
**kirim dulu titik-titik lama dari buffer (urut dari yang paling lama),
BARU kirim titik yang sekarang PALING TERAKHIR** — supaya "posisi terkini"
yang tersimpan di server benar-benar mencerminkan titik yang paling baru,
bukan tertimpa balik oleh titik lama yang disusulkan setelahnya. Ini
kebalikan dari urutan yang dipakai kontrak Cloud Function lama (kirim
live dulu, baru susulkan buffer) — kalau firmware dipindah ke backend ini,
urutannya WAJIB dibalik.

## 2. Endpoint baca data (admin/dashboard/app)

```
GET https://mqttskripsigps.jejakpintar.cloud/devices                    # semua device
GET https://mqttskripsigps.jejakpintar.cloud/devices/{sn}                # satu device
GET https://mqttskripsigps.jejakpintar.cloud/telemetry/{sn}/history?limit=100  # riwayat (100 titik terbaru, maks 500)
GET https://mqttskripsigps.jejakpintar.cloud/telemetry/{sn}/history?since=<ISO-8601>&limit=20000  # semua titik sejak waktu itu
GET https://mqttskripsigps.jejakpintar.cloud/devices/{sn}/alerts?limit=20      # notifikasi (kuota dsb.)
Header: X-Admin-Key: <ADMIN_API_KEY dari .env backend>
```

App Flutter sudah dipindah ke endpoint ini (lihat bagian 4) — ketiga
endpoint ini menggantikan `DeviceService.watchPrimaryDevice()` dan
`watchHistory()` versi Firestore lama. Bedanya ini REST biasa (poll/fetch),
bukan realtime stream seperti Firestore snapshot listener, jadi app polling
tiap 1 detik (posisi & notifikasi; waypoint 3 detik) untuk update "live"
alih-alih listener. Riwayat dipanggil dengan `since` supaya lintasan rentang
yang dipilih tergambar utuh (tanpa `since` terpotong di 500 titik terbaru).

### Status koneksi (online/offline) - per 2026-11-01

- Setiap selesai connect MQTT (GSM/WiFi) device publish `online` ke `status/<SN>`
  dan memasang Last Will `offline` di topic yang sama (keepalive GSM 30 s).
- Broker menerbitkan `offline` sendiri ±1,5x keepalive setelah koneksi device
  putus tanpa DISCONNECT (daya dicabut, sinyal hilang). ACL broker:
  `pattern write status/%c`.
- Backend menyimpan `devices.online` (field `online` di `GET /devices`) dan
  membuat alert `offline` / `online`; `offline` baru dicatat kalau tidak
  disusul `online` dalam 5 detik (sambung-ulang sendiri tidak memicu alarm).
- App: panel menampilkan `Offline` (merah) dan pop-up "terputus" /
  "tersambung kembali".

## 3. Setup server (sekali, bukan per-device)

Lihat `backend/README.md` untuk detail lengkap. Ringkasnya: `cp .env.example
.env`, isi `POSTGRES_PASSWORD`/`DEVICE_API_KEY`/`ADMIN_API_KEY` (generate
lewat `python3 -c "import secrets; print(secrets.token_urlsafe(32))"`),
pastikan DNS domain backend (per 2026-10-03: `mqttskripsigps.jejakpintar.cloud`,
lihat banner status di atas) sudah mengarah ke IP server, lalu
`docker compose up -d --build`. Migrasi Postgres (Alembic) jalan otomatis
tiap container `api` start.

## 4. Yang masih perlu dikerjakan sebelum backend ini benar-benar dipakai

- [x] Firmware (`skripsi.ino`) jalur GSM diarahkan ke
      `gps.jejakpintar.cloud/telemetry` dengan header `X-API-Key` (ganti dari
      `CLOUD_FN_HOST`/`CLOUD_FN_PATH` Firebase Cloud Function). Compile bersih.
- [x] Urutan kirim buffer offline dibalik di `cobaKirimDenganRetry()`: buffer
      lama disusulkan (`flushBufferedPoints()`) SEBELUM titik live dikirim,
      bukan sesudahnya — supaya titik live selalu jadi POST terakhir dan
      benar-benar jadi "posisi terkini" di server (lihat catatan `historyOnly`
      di bagian 1 di atas). Compile bersih.
- [x] Backend di-deploy ke server oleh kolaborator. Diverifikasi 2026-09-10:
      `GET /health` -> 200, dan `POST /telemetry` dengan `DEVICE_API_KEY` asli
      -> 200 dengan body `DeviceState` yang benar (dites pakai `sn` percobaan
      `"TEST-VERIFY"`, tidak mengotori data device asli `ESP32-01`).
- [x] `DEVICE_API_KEY` di firmware sudah diisi nilai asli dari kolaborator
      (bukan placeholder lagi).
- [x] **Diuji live di board fisik (2026-09-10) dengan `TINY_GSM_MODEM_A76XXSSL`
      (socket TLS mentah) — GAGAL.** `[Backend] /telemetry -> -1`, "respons
      kosong dari server", padahal WiFi fallback ke request identik sukses
      `200` di saat bersamaan. Payload kali ini pendek (API key ~44 karakter),
      jadi ini kali KETIGA gejala sama muncul dengan ukuran payload yang jauh
      berbeda tiap kali (JWT 900+ karakter, API key pendek ke endpoint belum
      live, API key pendek ke endpoint live) — cukup bukti akar masalahnya di
      mekanisme `AT+CCHOPEN`/`CCHRECV` itu sendiri, bukan panjang data.
- [x] Jalur GSM dipindah dari `TINY_GSM_MODEM_A76XXSSL` (socket TLS mentah)
      ke `TINY_GSM_MODEM_A7670` + AT+HTTPS native (`modem.https_begin/
      set_url/add_header/post`, fungsi `kirimKeBackendGSM()`) — mekanisme
      yang sebelumnya sudah terbukti reliable di sesi ini untuk request lain,
      dan sekarang viable lagi karena `X-API-Key` jauh di bawah batas
      256-600 karakter AT+HTTPPARA. Compile bersih. **Belum di-upload ulang
      ke board fisik** — menunggu board tersambung USB.
- [x] Jalur fallback WiFi (`kirimKeBackendWiFi()`) disatukan ke endpoint
      `/telemetry` yang SAMA dengan GSM (header `X-API-Key` yang sama) —
      TIDAK lagi sign-in Firebase Auth / menulis ke Firestore terpisah.
      Menutup bug data-split: sebelumnya data GSM dan WiFi bisa masuk ke DUA
      database berbeda tergantung jalur mana yang kebetulan berhasil.
      `firebaseSignInWiFi`, `ensureFreshTokenWiFi`, `FIREBASE_API_KEY`,
      `FIREBASE_PROJECT_ID`, dan field `config.device_email`/
      `device_password` (+ form-nya di portal) sudah dihapus karena sudah
      tidak dipakai sama sekali. `CONFIG_MAGIC` dinaikkan supaya EEPROM lama
      otomatis reset bersih.
- [x] App Flutter dipindah sepenuhnya (2026-09-10): `DeviceService` sudah
      ganti dari Firestore SDK ke `http` REST call ke `backend/`, "live
      update" jadi polling 5 detik (bukan snapshot listener). Backend
      ditambah endpoint `POST /auth/login`, `GET/PATCH /users/me`,
      `GET/POST/PATCH/DELETE /users[/{id}]`, dan
      `GET/POST/DELETE /devices/{sn}/waypoints[/{id}]` — login sekarang JWT
      dari backend sendiri, bukan Firebase Auth lagi. Diverifikasi live di
      emulator Android: login, admin panel, create/edit user, dan waypoint
      add/list/remove semua jalan lewat backend baru.
- [x] Firestore & Cloud Function lama (`functions/`, `firebase.json`,
      `firestore.rules`/`indexes.json`, `lib/firebase_options.dart`) sudah
      dipensiunkan — dihapus dari repo setelah migrasi di atas terverifikasi
      jalan. Lihat `README.md` untuk arsitektur saat ini.

## Riwayat kontrak sebelumnya (Firebase, masih aktif di firmware saat ini)

Sebelum migrasi ke FastAPI ini, kontraknya adalah Cloud Function
`ingestPosition` (lihat `functions/index.js`) yang menulis ke Firestore
pakai Admin SDK, dipanggil device dengan `X-Device-Key`. Itu tidak pernah
benar-benar aktif karena project belum di-upgrade ke paket Blaze. Detail
lengkapnya tersimpan di riwayat git/percakapan sesi sebelumnya kalau perlu
dirujuk lagi.
