/*
  =========================================================================
  GNSS + GSM (SIM CARD) -> BACKEND FASTAPI (gps.jejakpintar.cloud)
  =========================================================================
  A7670E dipakai untuk DUA fungsi sekaligus lewat satu chip yang sama:
    1. GNSS (GPS)  -> posisi, kecepatan, arah, DAN waktu UTC (dari satelit,
       bukan NTP - lihat alasan di bawah).
    2. Data seluler (kartu SIM + APN) -> jalur internet UTAMA buat kirim ke
       backend, lewat AT+HTTPS NATIVE bawaan modem (bukan socket TLS mentah -
       lihat riwayat di bawah kenapa).

  RIWAYAT SINGKAT (kenapa arsitekturnya begini):
  1. Awalnya kirim langsung ke Firestore pakai idToken Firebase Auth (~900-
     1100 karakter) sebagai header Authorization. AT+HTTPPARA (baik header
     maupun URL) dibatasi modem chipset ASR1603 MAKSIMAL 256-600 karakter
     (A76XX_Series_AT_Command_Manual_V1.12.pdf) - token itu jelas kepanjangan.
  2. Dicoba socket TLS mentah (TINY_GSM_MODEM_A76XXSSL, AT+CCHOPEN/CCHSEND)
     yang secara teori tidak dibatasi panjang parameter AT command. TERNYATA
     TETAP TIDAK RELIABLE: gagal "respons kosong dari server" dua kali,
     dengan payload yang beda jauh ukurannya (token 900+ karakter MAUPUN
     X-API-Key pendek ~44 karakter) - jadi terbukti BUKAN soal ukuran data,
     tapi mekanisme AT+CCHOPEN/CCHRECV modul ini sendiri yang bermasalah
     (kemungkinan race condition: server membalas lalu langsung menutup
     koneksi secepat itu sehingga data yang sempat diterima modem tidak
     sempat diambil sebelum status "connected" modem berubah jadi false).
  3. Solusi backend: pindah dari Firestore+Firebase Auth ke backend FastAPI
     sendiri (`backend/` di project ini, dari https://github.com/ghamiDev/Rafi,
     live di gps.jejakpintar.cloud) dengan auth API KEY PENDEK (~44 karakter,
     header X-API-Key) - bukan token JWT panjang. Ini menghilangkan alasan
     ASLI untuk pakai socket TLS mentah sama sekali.
  4. Karena auth sekarang pendek, GSM balik pakai AT+HTTPS NATIVE
     (modem.https_*) - SUDAH TERBUKTI RELIABLE di sesi pengujian sebelumnya
     (dipakai berhasil berkali-kali untuk sign-in Firebase) - dan jalur WiFi
     cadangan disatukan ke endpoint backend YANG SAMA (dulu WiFi masih nulis
     langsung ke Firestore terpisah - itu bug nyata: data device kepecah ke
     DUA database berbeda tergantung jalur mana yang kebetulan berhasil).

  AKSES PORTAL CONFIG PERMANEN: Access Point (SoftAP) SEKARANG SELALU AKTIF
  (WIFI_AP_STA), tidak lagi cuma nyala saat device belum dikonfigurasi. IP-nya
  (192.168.4.1 default ESP32) TIDAK PERNAH BERUBAH, jadi portal selalu bisa
  dibuka dengan connect ke SSID alat kapan pun, apa pun status GSM/WiFi lokal.
  Kalau WiFi lokal (rumah/kantor) dikonfigurasi, device JUGA coba sambung
  sebagai STA (paralel dengan AP, bukan gantiin) supaya portal juga bisa
  diakses dari jaringan itu lewat mDNS (http://<hostname>.local) tanpa perlu
  tahu IP DHCP-nya. GSM tetap JALUR UTAMA kirim data - STA di sini murni
  untuk aksesibilitas portal + cadangan kirim data, tidak mengubah prioritas
  itu (lihat kirimKeBackend()).

  DETEKSI KUOTA SIM HABIS (TIDAK LANGSUNG): modem GSM TIDAK PUNYA perintah AT
  untuk membaca sisa kuota data - itu cuma ada di server operator, bukan di
  SIM/modem. Jadi dipakai HEURISTIK: kalau jaringan seluler terdaftar normal
  (waitForNetwork sukses) TAPI sesi data (gprsConnect) gagal dibuka berkali-
  kali berturut-turut, itu indikasi kuat kuota/pulsa data habis (lihat
  QUOTA_WARNING_THRESHOLD di gsmConnect()). Ini BUKAN kepastian 100% (bisa
  juga APN salah atau layanan data disuspend operator), makanya selalu
  dilaporkan sebagai "diduga"/"kemungkinan", bukan fakta pasti. Notifikasinya
  dicetak ke Serial Monitor secara berkala DAN dititipkan ke field
  "dataSource" saat kirim lewat WiFi cadangan (backend tidak punya field
  notifikasi terpisah), supaya kelihatan di app tanpa perlu ubah skema
  backend/redeploy.

  NTP bawaan ESP32 (configTime()) TETAP tidak dipakai - saat GSM saja yang
  aktif (tanpa WiFi), ESP32 tidak selalu punya rute internet sendiri buat NTP.
  Timestamp diambil LANGSUNG dari jam satelit GPS (field <date>/<UTC-time> di
  +CGNSSINFO) - lebih presisi, dan tidak bergantung koneksi apa pun.

  Kontrak API & skema field mengikuti FIRMWARE_CONTRACT.md di project app
  (skripsi_gps_tracker/backend/README.md untuk detail backend-nya):
    POST https://gps.jejakpintar.cloud/telemetry
    Header: X-API-Key: <DEVICE_API_KEY>
    Body JSON: {sn, d} - lihat "PENGHEMATAN BYTE" di bawah untuk isi `d`.
  PENTING: backend ini TIDAK punya konsep "historyOnly" - setiap POST SELALU
  menimpa posisi "live" SEKALIGUS nambah riwayat. Makanya buffer offline
  (flushBufferedPoints()) sengaja disusulkan SEBELUM titik live dikirim di
  cobaKirimDenganRetry() - supaya titik live selalu jadi POST TERAKHIR dan
  tidak ketimpa balik oleh titik lama.

  PENGHEMATAN BYTE (2026-09-15) - payload dikompres dari JSON penuh
  (~197 byte body) jadi JSON ringkas berisi field biner ter-hex-encode
  (~58 byte body, hemat ~70%). Dianalisis dari referensi
  vms_u30gt_iridium_final.ino (tracker Iridium satelit) - TAPI perlu dicatat
  finding penting: di file itu, hex CUMA representasi String sementara di
  dalam kode (gampang di-print/di-passing); data yang BENAR-BENAR dikirim ke
  modem Iridium tetap byte mentah (`sendDataToIridium()` mengonversi hex itu
  balik ke byte sebelum `iridiumSerial.write()`). Karena kita kirim lewat
  HTTP (bukan AT+SBDWB ke modem Iridium yang perlu representasi String),
  raw binary (`Content-Type: application/octet-stream`) sebenarnya LEBIH
  hemat lagi (tidak ada penggandaan 2x dari hex) - TAPI hex-di-dalam-JSON
  dipilih di sini karena: (1) tetap bisa di-debug pakai curl/Postman/log
  server seperti sebelumnya (raw binary di body HTTP susah dibaca manusia),
  (2) perubahan di backend jauh lebih kecil (cuma tambah 1 field, endpoint
  tetap terima JSON seperti biasa, tidak perlu ganti Content-Type/parsing).

  Format field `d`: 17 byte biner di-hex-encode jadi 34 karakter heksadesimal
  (big-endian semua):
    byte 0-3:   lat  (int32, nilai asli = lat * 1.000.000)
    byte 4-7:   lng  (int32, nilai asli = lng * 1.000.000)
    byte 8-9:   heading (uint16, derajat 0-359 langsung, tanpa scaling)
    byte 10-11: speedKnots (uint16, nilai asli = speedKnots * 100)
    byte 12-15: timestamp (uint32, UNIX epoch UTC - lihat utcEpochFromGps())
    byte 16:    flags (uint8) - bit0: dataSource (0=GSM,1=WIFI),
                bit1: quota_warning (1=GSM diduga kuota habis, cuma valid
                kalau bit0=1), bit2: powerSource (0=BATTERY,1=AKI),
                bit3: tamper (1=alat SEDANG diusik/dibongpal paksa saat
                pengiriman ini - lihat [MPU6050 TAMPER] & tamperAlarmAktif;
                HANYA diisi untuk titik LIVE, SELALU 0 untuk titik susulan
                dari buffer offline - lihat kirimSatuRiwayatPosisi()),
                bit4: tamper_test (1=paket tamper ini SIMULASI dari perintah
                server "test_tamper", SELALU bersama bit3=1 dan cuma di SATU
                paket kirim-paksa itu - lihat [PERINTAH MQTT]),
                bit5-7: cadangan, harus 0
  KONTRAK LINTAS-REPO: penambahan bit3 ini PERLU dikoordinasikan dengan
  backend+app di repo skripsi_gps_tracker (lihat FIRMWARE_CONTRACT.md di
  situ) SEBELUM firmware ini di-upload ke board produksi - decode bit3 belum
  tentu sudah ada di sisi backend/app saat komentar ini ditulis.
  Field `sn` TETAP teks biasa di JSON (di luar blob hex) - supaya backend
  bisa routing/cari device tanpa perlu decode hex dulu. Field `name` SENGAJA
  DIHAPUS dari tiap pengiriman (jarang berubah saat runtime, backend sudah
  otomatis pertahankan nilai lama kalau field ini tidak dikirim - lihat
  `if payload.name is not None` di backend/app/main.py).

  !!! PERUBAHAN INI MENGUBAH KONTRAK WIRE FORMAT - backend WAJIB di-update
  dulu untuk bisa decode field `d` ini sebelum firmware ini diupload ke
  board asli, kalau tidak semua POST /telemetry akan gagal divalidasi
  Pydantic (field lat/lng/dst lama tidak lagi dikirim). Lihat pesan yang
  dikirim ke sesi "gps-tracker-flutter-rebuild" untuk detail spek decode-nya. !!!

  LIBRARY YANG WAJIB DIPAKAI UNTUK A7670E (PENTING):
    Harus pakai fork LilyGo "lewisxhe/TinyGSM-fork" (library.properties:
    "TinyGSM Fork"), BUKAN TinyGSM versi resmi dari Library Manager.
    Versi resmi tidak mengenal macro TINY_GSM_MODEM_A7670 -> compile gagal
    dengan pesan "Please define GSM modem model".

  KONFIGURASI PIN — sudah dicocokkan dengan utilities.h resmi board
  "LilyGo-T-Call A7670 V1.0" (JANGAN pakai nilai pin dari tutorial/board
  varian lain seperti T-A7670G - pin-nya berbeda).
  =========================================================================
*/

#define TINY_GSM_MODEM_A7670
#define TINY_GSM_RX_BUFFER  1024  // buffer respons HTTPS lebih besar dari default
// #define DUMP_AT_COMMANDS   // aktifkan buat lihat log AT mentah ke modem

#include <esp_system.h>  // esp_reset_reason() - lihat alasanResetTerakhir()
#include <Wire.h>
#include <TinyGsmClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>  // MQTT lewat WiFi - lihat WIFI_TRANSPORT_MQTT
#include <WebServer.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

// Forward declaration: Arduino menyisipkan prototype fungsi otomatis tepat di
// sini (baris kode pertama setelah blok #include), jadi tipe struct yang
// dipakai sebagai parameter fungsi (GpsData, didefinisikan lengkap di bawah)
// harus sudah dikenal namanya duluan di titik ini, kalau tidak compile gagal.
struct GpsData;

// ==========================================================
// [1] KONFIGURASI BACKEND & DEVICE (TETAP, TIDAK LEWAT PORTAL)
// ==========================================================
const char* DEVICE_SN     = "ESP32-01";  // harus sama persis dengan SN yang didaftarkan di app

// Backend FastAPI - PINDAH 2026-09-25 dari repo/domain lama
// (ghamiDev/Rafi, gps.jejakpintar.cloud - SEKARANG MATI/502, jangan dipakai
// lagi) ke stack baru sendiri (github.com/rafi300604/kawal-gps-backend,
// mqttskripsigps.jejakpintar.cloud). Dikonfirmasi Rafi langsung bahwa ini
// proyek yang sama, dan diverifikasi SENDIRI lewat curl (bukan cuma percaya
// laporan) sebelum baris ini diubah: GET /health -> 200, POST /telemetry
// dengan DEVICE_API_KEY baru + sn dummy "TEST-VERIFY" -> 200 dengan body
// DeviceState yang benar. DEVICE_API_KEY ini SAMA untuk semua device di
// fleet (bukan identitas per-device - identitas device datang dari field
// "sn" di body request).
#include "secrets.h"  // DEVICE_API_KEY, MQTT_USERNAME, MQTT_PASSWORD, AP_PASSWORD (tidak di-commit; contoh: secrets.example.h)
const char* BACKEND_HOST   = "mqttskripsigps.jejakpintar.cloud";
const char* BACKEND_PATH   = "/telemetry";

// Migrasi HTTPS -> MQTT untuk jalur GSM (Iterasi 3, lihat Bab 3.5 Proses
// Iteratif proposal) - tujuannya hemat kuota data seluler: HTTPS lewat
// AT+HTTPACTION buka sesi TLS baru TIAP kirim, sedangkan MQTT sekali
// connect lalu publish berkali-kali lewat koneksi yang sama.
//
// GSM_TRANSPORT_MQTT sengaja default 0 (tetap pakai HTTPS) - broker MQTT
// (lihat MQTT_BROKER_HOST dkk di bawah) BELUM disiapkan di sisi backend saat
// baris ini ditulis (dikoordinasikan lewat sesi skripsi_gps_tracker). Ganti
// jadi 1 HANYA SETELAH broker terkonfirmasi hidup DAN nilai
// MQTT_BROKER_HOST/PORT/USER/PASS di bawah sudah diisi nilai asli (bukan
// placeholder) - kalau tetap 0 sementara broker belum siap, jalur WiFi
// (kirimKeBackendWiFi, tidak terpengaruh flag ini) tetap jalan seperti
// biasa lewat HTTPS, jadi tracking tidak mati total kalau lupa flag ini.
//
// RIWAYAT KOORDINASI (supaya jelas kenapa nilai di bawah berubah):
// - 2026-09-24: kredensial pertama dari broker "mqtt-gps" di domain lama
//   (gps.jejakpintar.cloud) - ternyata belum sempat dipakai, broker itu
//   ditinggalkan gara-gara port collision dengan project BLE-scan yang
//   sempat co-host di server yang sama.
// - 2026-09-25: backend dipindah ke repo+stack sendiri
//   (github.com/rafi300604/kawal-gps-backend), domain baru khusus MQTT ini
//   (mqttskripsigps.jejakpintar.cloud). Dikonfirmasi LANGSUNG oleh Rafi
//   (bukan cuma dari sesi lintas-terminal) bahwa ini proyek yang sama dan
//   Mas Fifit adalah pengelola backend yang sebenarnya - lihat percakapan
//   sesi ini sebelum baris ini ditulis kalau perlu jejak lengkapnya.
//   Backend pipeline sudah dites end-to-end (publish manual, data masuk ke
//   tabel telemetry/device_alerts dengan benar) SEBELUM baris ini diaktifkan.
#define GSM_TRANSPORT_MQTT  1

// STRATEGI UJI COBA (2026-09-25, permintaan Rafi): nyalakan MQTT di WiFi
// LEBIH DULU, karena WiFi jauh lebih gampang diuji (selalu berhasil connect
// di semua tes sejauh ini, tidak seperti GSM yang berkali-kali timeout cari
// sinyal) - kalau MQTT lewat WiFi terbukti jalan, itu memvalidasi LOGIKA
// MQTT-nya sendiri (broker, topic, payload, decode backend) independen dari
// masalah modem seluler. Kalau nanti GSM_TRANSPORT_MQTT gagal, jadi jelas
// itu soal modem/AT-command, BUKAN soal MQTT-nya salah. Alasan lain WiFi
// tetap relevan dipindah ke MQTT (bukan cuma buat uji coba): di kendaraan
// nanti, WiFi biasanya BUKAN dari jaringan rumah/kantor tak terbatas, tapi
// dari modem WiFi mobil atau hotspot HP user - dua-duanya SAMA-SAMA pakai
// kuota data seluler juga, jadi penghematan MQTT tetap relevan di jalur ini.
#define WIFI_TRANSPORT_MQTT  1

const char*    MQTT_BROKER_HOST = "mqttskripsigps.jejakpintar.cloud";
const uint16_t MQTT_BROKER_PORT = 8883;
const bool     MQTT_USE_TLS     = true;
// MQTT_USERNAME & MQTT_PASSWORD: lihat secrets.h
// Sertifikat TLS broker ini self-signed (skala thesis, bukan dari CA publik)
// - mqttPastikanTersambung() SENGAJA TIDAK memanggil modem.mqtt_set_certificate()
// sama sekali, supaya authMethod di TinyGsmMqttA76xx::mqtt_connect() tetap 0
// ("no authentication" - lihat komentar "0 - no authentication" di
// TinyGsmMqttA76xx.h) - koneksi TETAP terenkripsi TLS (MQTT_USE_TLS=true),
// cuma tidak memverifikasi rantai sertifikat server. Kalau nanti broker
// pindah ke sertifikat CA publik (Let's Encrypt dsb, sama seperti Caddy di
// jalur HTTPS), verifikasi bisa diaktifkan lagi lewat mqtt_set_certificate().
// Topic publish: telemetry/<DEVICE_SN> - payload PERSIS hex 34 karakter yang
// sama dengan field "d" di kontrak HTTPS (lihat encodeTelemetryHex()), TANPA
// bungkus JSON {"sn":...} lagi - identitas device sudah ada di topic-nya
// sendiri, jadi tidak perlu diulang di payload (lebih hemat byte lagi).

// Interval kirim ADAPTIF 3 TINGKAT - lihat juga [MPU6050] di bawah untuk
// subsistem tamper/anti-theft yang TERPISAH total dari mode transmisi ini.
//
// TEMUAN LAPANGAN (2026-09-21, uji coba nyata 3 mode): desain AWAL memakai
// "GPS DAN MPU harus sama-sama setuju" untuk BERGERAK ternyata SALAH pada
// dua arah sekaligus:
//   1) Kendaraan BENAR-BENAR jalan tapi getaran tidak cukup menggerakkan
//      MPU (jalan mulus/threshold MPU tidak kepicu) -> salah dilaporkan
//      SLEEPY padahal sedang bergerak.
//   2) Sudah SLEEP (terparkir lama), lalu MPU sesaat mendeteksi getaran
//      (disenggol, angin, dsb) yang KEBETULAN berbarengan dengan noise GPS
//      (speed sesaat sedikit di atas ambang, umum terjadi indoor/multipath -
//      lihat juga "BUG NYATA #2" di bagian tamper) -> satu iterasi loop
//      dengan kombinasi kebetulan ini langsung mereset diamSejakMillis ke 0
//      dan salah lapor balik ke SLEEPY, walau kendaraan tidak pernah benar
//      benar bergerak.
// SOLUSI: GPS (posisi/kecepatan dari satelit) dijadikan SATU-SATUNYA acuan
// mode transmisi, TIDAK LAGI perlu persetujuan MPU. Supaya noise GPS sesaat
// (poin 2 di atas) tetap tidak lolos, kecepatan GPS di atas ambang harus
// BERTAHAN dulu selama GPS_MOVING_CONFIRM_MS sebelum dipercaya sebagai
// gerakan sungguhan (pola konfirmasi yang sama seperti
// TAMPER_BERGERAK_CONFIRM_MS di bagian tamper). MPU6050 sekarang SEPENUHNYA
// didedikasikan untuk subsistem tamper/anti-theft saja - lihat [MPU6050
// TAMPER] di bawah - tidak lagi ikut menentukan status BERGERAK/SLEEPY/SLEEP.
//   BERGERAK: kecepatan GPS di atas ambang, TERKONFIRMASI bertahan minimal
//             GPS_MOVING_CONFIRM_MS - kirim paling sering.
//   SLEEPY:   baru saja berhenti (berlaku SEJAK detik pertama tidak lagi
//             "bergerak") - mencakup berhenti sebentar (lampu merah, isi
//             bensin, dsb.) tanpa langsung dianggap terparkir.
//   SLEEP:    sudah di kondisi "sleepy" TERUS-MENERUS selama
//             SLEEPY_KE_SLEEP_MS - baru dianggap benar-benar terparkir lama,
//             kirim paling jarang.
// BUG NYATA #8 (ditemukan lewat pengujian nyata 2026-09-26, dikonfirmasi
// alat BENAR-BENAR diam total sepanjang tes): ambang 1.0 knot + tahan 8
// detik TERNYATA masih bisa ditembus noise kecepatan GPS chip ini, bahkan
// saat fix SANGAT bagus (HDOP 1.5-2.3, 24-28 satelit) - diamati langsung
// dari log: 0.89, 1.81, 2.02, dan 2.91 knot muncul silih berganti PADAHAL
// alat diam total, dan salah satu (2.91 knot) sampai bertahan penuh 8
// detik sehingga sungguhan memicu "BERGERAK" palsu, mereset diamSejakMillis
// dan membuat mode SLEEP tidak pernah sempat mengirim di interval 30
// menitnya (selalu kepotong balik ke SLEEPY sebelum sempat). Solusi
// berdasarkan angka nyata di atas (bukan tebakan): naikkan ambang jauh di
// atas noise tertinggi yang teramati (2.91 knot) DAN perpanjang durasi
// konfirmasi, supaya noise sesaat/multipath makin sulit menembus dua-duanya
// sekaligus. Kendaraan yang BENAR-BENAR jalan tetap terdeteksi cepat -
// dalam praktik nyata durasi jalan sungguhan jauh melebihi 15 detik.
#define SPEED_MOVING_THRESHOLD_KNOTS  3.5f     // di atas ini GPS dianggap "bergerak" (naik dari 1.0 - noise tertinggi teramati 2.91 knot, lihat BUG NYATA #8)
#define GPS_MOVING_CONFIRM_MS         15000UL  // naik dari 8000 - kecepatan GPS harus bertahan di atas ambang minimal 15 detik dulu baru dipercaya BERGERAK sungguhan (menyaring noise GPS sesaat/multipath saat diam)
#define TRANSMIT_INTERVAL_MOVING_MS   30000UL   // 30 detik - mode BERGERAK
#define TRANSMIT_INTERVAL_STILL_MS    300000UL  // 5 menit - mode SLEEPY
#define TRANSMIT_INTERVAL_SLEEP_MS    1800000UL // 30 menit - mode SLEEP (terparkir lama)
// 15 menit dipilih sebagai titik tengah: cukup panjang supaya berhenti
// sebentar (lampu merah, mampir sebentar) TIDAK keburu dianggap "terparkir"
// dan turun ke interval 30 menit, tapi cukup pendek supaya baterai/kuota
// tetap benar-benar hemat begitu kendaraan sungguhan terparkir lama.
// SILAKAN disesuaikan setelah uji coba nyata di lapangan.
#define SLEEPY_KE_SLEEP_MS            900000UL  // 15 menit diam terus-menerus

#define KIRIM_MAX_RETRY  3  // percobaan sambung ulang SIM/WiFi + kirim ulang sebelum data dibuffer

// Heuristik deteksi kuota/pulsa data SIM habis - lihat penjelasan lengkap di
// komentar atas file ("DETEKSI KUOTA SIM HABIS").
#define QUOTA_WARNING_THRESHOLD  5  // gagal buka sesi data GSM berturut-turut (padahal jaringan terdaftar) sebelum dicurigai kuota habis
int  consecutiveGprsFailWhileRegistered = 0;
bool quotaLikelyExhausted = false;

// Supaya loop() (yang jalan sangat sering) tidak spam percobaan reconnect
// WiFi lokal setiap iterasi kalau sedang gagal terus - jarak minimal antar
// percobaan reconnect.
#define WIFI_RECONNECT_COOLDOWN_MS  30000UL
unsigned long lastWifiAttemptMillis = 0;

// PENTING (stabilitas portal): modem.waitForNetwork()/gprsConnect() di
// gsmConnect() adalah panggilan BLOCKING dari library TinyGSM yang TIDAK
// punya hook untuk memanggil server.handleClient() selama menunggu - kalau
// gsmConnect() dipanggil ULANG setiap iterasi loop() tanpa jeda (yang terjadi
// terus-menerus selama sinyal seluler tidak ada/lemah), portal (HTTP server)
// nyaris tidak pernah kebagian giliran diproses sama sekali, gejalanya:
// halaman config "loading" sangat lama lalu akhirnya "site can't be reached"
// walaupun perangkat masih tersambung ke Access Point alat. Cooldown ini
// memberi jeda supaya loop() (dan server.handleClient() di dalamnya) tetap
// dapat jatah waktu jalan rutin di antara percobaan GSM.
#define GSM_RECONNECT_COOLDOWN_MS  20000UL
unsigned long lastGsmAttemptMillis = 0;

// Info cara akses portal config (+ status kuota) diingatkan berkala di
// Serial, bukan cuma sekali di awal boot - supaya tidak gampang kelewat.
#define STATUS_REMINDER_INTERVAL_MS  300000UL  // 5 menit
unsigned long lastStatusReminderMillis = 0;

// ==========================================================
// [1b] BUFFER OFFLINE - simpan titik GPS di RAM saat internet mati, kirim
// semua sekaligus begitu koneksi (SIM atau WiFi) pulih lagi.
// ==========================================================
#define MAX_BUFFERED_POINTS  200  // ~200 titik x menit-an = cukup untuk beberapa jam offline

struct BufferedPoint {
  double lat, lng;
  float  speedKnots, heading;
  uint32_t epochUtc;  // UNIX epoch UTC, diambil dari jam GPS saat titik itu direkam
};

BufferedPoint pointBuffer[MAX_BUFFERED_POINTS];
int bufferCount = 0;  // jumlah titik yang sedang tersimpan (buffer berhenti nerima kalau penuh)

// ==========================================================
// [2] WIFI — DIISI LEWAT PORTAL WEB (POLA SAMA SEPERTI koding/BLE/BLE.ino):
// disimpan ke EEPROM, kalau belum pernah diisi device otomatis buka Access
// Point sendiri untuk diisi pertama kali. Auth ke backend sekarang API key
// pendek yang tetap (compile-time const di atas), JADI TIDAK ADA LAGI field
// email/password akun Firebase yang perlu diisi lewat portal - itu sudah
// dihapus (dulu dipakai buat sign-in Firebase Auth di jalur WiFi, yang
// sekarang sudah tidak dipakai sama sekali).
// ==========================================================
#define EEPROM_SIZE  512
// AP_PASSWORD (password AP portal konfigurasi): lihat secrets.h

// GANTI NILAI INI SETIAP KALI STRUCT Config DI BAWAH BERUBAH (tambah/hapus/
// pindah field) - supaya firmware baru tidak salah membaca EEPROM yang
// ditulis oleh struct versi lama (field bisa geser posisi byte-nya, jadi
// terbaca isi acak/salah kalau tidak dideteksi lewat magic number ini).
#define CONFIG_MAGIC  0x53475033UL  // "SGP3" - versi ke-3 (hapus device_email/password, tambah "configured")

struct Config {
  uint32_t magic;      // WAJIB field pertama - lihat CONFIG_MAGIC di atas
  char ssid[32];
  char password[64];
  char hostname[32];
  char device_name[64];  // nama vessel/alat yang tampil di app, field "name" di backend
  char apn[32];          // APN kartu SIM (mis. "indosatgprs" untuk Indosat/IM3)
  int  wifi_timeout;      // TIDAK LAGI DIPAKAI (connectWiFi() sekarang non-blocking, lihat komentarnya) - field dibiarkan ada supaya layout EEPROM lama tidak perlu di-migrasi, sudah dihapus dari form portal
  bool configured;        // true setelah portal /save pernah dipanggil sekali (lihat setup())
};

Config config;
WebServer server(80);

// Diisi startMDNS() - slug hostname yang dipakai untuk http://<ini>.local
// (kosong kalau mDNS gagal start, lihat startMDNS()).
String mdnsHostname = "";

// Status registrasi jaringan seluler terakhir, dipakai loop() & ditampilkan
// di dashboard portal.
bool gsmRegistered = false;

bool loadConfig() {
  EEPROM.get(0, config);
  // magic salah = struct Config di firmware ini beda dari yang menulis EEPROM
  // terakhir kali (field ditambah/pindah) - field lain pasti ikut salah baca,
  // jangan dipakai sama sekali, reset bersih ke default.
  bool invalid = (config.magic != CONFIG_MAGIC) ||
                 (config.wifi_timeout < 5 || config.wifi_timeout > 120);
  if (invalid) {
    config.magic = CONFIG_MAGIC;
    strcpy(config.ssid, "");
    strcpy(config.password, "");
    strcpy(config.hostname, "GPS-TRACKER");
    strcpy(config.device_name, "Kapal 1");
    strcpy(config.apn, "indosatgprs");
    config.wifi_timeout = 20;
    config.configured = false;
    return false;
  }
  return true;
}

void saveConfig() {
  config.magic = CONFIG_MAGIC;
  EEPROM.put(0, config);
  EEPROM.commit();
  Serial.println("Config saved.");
}

// Struct GPS didefinisikan di sini (dekat struct Config) — BUKAN cuma gaya
// penulisan: prototype fungsi auto-generate Arduino disisipkan di awal file,
// jadi tipe struct yang dipakai sebagai parameter fungsi harus sudah dikenal
// sebelum titik sisip itu, kalau tidak compile gagal "does not name a type".
struct GpsData {
  bool     valid;
  uint8_t  fixType;          // 2 = 2D fix, 3 = 3D fix
  int      satGPS, satBeidou, satGlonass, satGalileo, satTotal;
  double   latitude;
  double   longitude;
  float    altitude;
  float    speedKnots;
  bool     courseValid;
  float    course;           // heading, derajat (0 = utara)
  float    pdop, hdop, vdop;
  uint16_t year;              // waktu UTC dari satelit GPS (bukan NTP - lihat komentar di atas file)
  uint8_t  month, day, hour, minute, second;
};

// ==========================================================
// [3] KONFIGURASI PIN GNSS (LilyGo T-Call A7670E V1.0)
// ==========================================================
#define MODEM_TX_PIN                  26   // ESP32 TX -> RX modem
#define MODEM_RX_PIN                  25   // ESP32 RX <- TX modem
#define MODEM_DTR_PIN                 14
#define BOARD_PWRKEY_PIN               4
#define MODEM_RESET_PIN               27
#define MODEM_RESET_LEVEL           LOW
#define MODEM_POWERON_PULSE_WIDTH_MS 100
#define UART_BAUD                 115200

HardwareSerial SerialAT(1);

#ifdef DUMP_AT_COMMANDS
  #include <StreamDebugger.h>
  StreamDebugger debugger(SerialAT, Serial);
  TinyGsm modem(debugger);
#else
  TinyGsm modem(SerialAT);
#endif

// ==========================================================
// [4] PARAMETER KUALITAS FIX GPS
// ==========================================================
#define MIN_SATELLITES         4      // Minimal total satelit sebelum fix dianggap layak
#define MAX_HDOP_ACCEPTED       3.0f  // HDOP maksimal yang masih diterima
#define REQUIRE_3D_FIX          true  // true = wajib 3D fix (altitude valid)

// ==========================================================
// [6] PARSING CSV MENTAH "+CGNSSINFO" (lihat analisis sebelumnya)
// ==========================================================
String csvField(const String &data, int index) {
  int start = 0, current = 0;
  for (int i = 0; i <= (int)data.length(); i++) {
    if (i == (int)data.length() || data.charAt(i) == ',') {
      if (current == index) return data.substring(start, i);
      start = i + 1;
      current++;
    }
  }
  return "";
}

bool bacaGNSS(GpsData &d) {
  d = GpsData();

  String raw = modem.getGPSraw();
  raw.trim();
  if (raw.length() == 0) return false;

  d.fixType = csvField(raw, 0).toInt();
  if (d.fixType != 2 && d.fixType != 3) return false;

  d.satGPS     = csvField(raw, 1).toInt();
  d.satBeidou  = csvField(raw, 2).toInt();
  d.satGlonass = csvField(raw, 3).toInt();
  d.satGalileo = csvField(raw, 4).toInt();
  d.satTotal   = d.satGPS + d.satBeidou + d.satGlonass + d.satGalileo;

  double latRaw = csvField(raw, 5).toDouble();
  char   ns     = csvField(raw, 6).charAt(0);
  double lonRaw = csvField(raw, 7).toDouble();
  char   ew     = csvField(raw, 8).charAt(0);
  if (latRaw == 0 || lonRaw == 0) return false;

  d.latitude  = (ns == 'S') ? -latRaw : latRaw;
  d.longitude = (ew == 'W') ? -lonRaw : lonRaw;

  String dateStr = csvField(raw, 9);   // ddmmyy
  if (dateStr.length() >= 6) {
    d.day   = dateStr.substring(0, 2).toInt();
    d.month = dateStr.substring(2, 4).toInt();
    d.year  = 2000 + dateStr.substring(4, 6).toInt();
  }
  String timeStr = csvField(raw, 10);  // hhmmss.ss (UTC)
  if (timeStr.length() >= 6) {
    d.hour   = timeStr.substring(0, 2).toInt();
    d.minute = timeStr.substring(2, 4).toInt();
    d.second = timeStr.substring(4, 6).toInt();
  }

  d.altitude   = csvField(raw, 11).toFloat();
  d.speedKnots = csvField(raw, 12).toFloat();

  String courseStr = csvField(raw, 13);
  d.courseValid = courseStr.length() > 0;
  d.course      = courseStr.toFloat();

  d.pdop = csvField(raw, 15).toFloat();
  d.hdop = csvField(raw, 16).toFloat();
  d.vdop = csvField(raw, 17).toFloat();

  d.valid = true;
  return true;
}

bool fixLayakDipakai(const GpsData &d) {
  return (d.satTotal >= MIN_SATELLITES) &&
         (d.hdop > 0 && d.hdop <= MAX_HDOP_ACCEPTED) &&
         (!REQUIRE_3D_FIX || d.fixType == 3) &&
         (d.year >= 2020);  // waktu UTC dari satelit wajib valid juga (dipakai buat timestamp)
}

// Timestamp UTC untuk backend, diambil LANGSUNG dari jam satelit GPS (bukan
// NTP) - lihat penjelasan di komentar atas file. Dikembalikan sebagai UNIX
// epoch (detik sejak 1970-01-01 UTC), BUKAN string ISO8601 lagi - dipakai
// payload biner ringkas (lihat "PENGHEMATAN BYTE" di komentar atas file).
// Algoritma days-from-civil (Howard Hinnant) - aman untuk tanggal Gregorian
// manapun, tidak butuh fungsi libc timegm() yang dukungannya tidak pasti
// di semua core ESP32.
uint32_t utcEpochFromGps(const GpsData &d) {
  int y = d.year;
  int m = d.month;
  y -= (m <= 2) ? 1 : 0;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d.day - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long daysSinceEpoch = era * 146097L + (long)doe - 719468L;
  return (uint32_t)(daysSinceEpoch * 86400UL + d.hour * 3600UL + d.minute * 60UL + d.second);
}

// ==========================================================
// [MPU6050] DETEKSI GERAK KENDARAAN (untuk mode transmisi adaptif 3 tingkat)
// ==========================================================
// Wiring: MPU6050 VCC->3V3, GND->GND, SDA->GPIO21, SCL->GPIO22, AD0->GND
// (alamat I2C 0x68).
//
// CATATAN: fitur deteksi tamper+buzzer dari mpuAccelerator.ino SEMPAT
// diintegrasikan di sini, TAPI DICABUT LAGI (2026-09) atas permintaan user
// setelah ditemukan bug nyata di sistem deteksi GERAK (lihat penjelasan
// algoritma di bawah) - user minta MPU dipakai HANYA untuk mode transmisi
// dulu sampai itu stabil. Kalau tamper+buzzer mau diaktifkan lagi nanti,
// logikanya (identik mpuAccelerator.ino) ada di riwayat git/percakapan
// sesi ini, tinggal ditambahkan kembali - TAPI pastikan pakai baseline-nya
// SENDIRI yang terpisah dari algoritma gerak di bawah, JANGAN pakai pola
// "baseline cuma belajar saat tenang" untuk detektor yang berjalan
// berjam-jam (lihat kenapa di bawah), pola itu cuma aman untuk detektor
// tamper yang episode-nya singkat.
//
// ALGORITMA (PENTING - sempat ada bug macet permanen di versi sebelumnya):
// versi awal membandingkan tiap bacaan ke SATU baseline yang cuma di-update
// (exponential moving average) saat status "diam". Ternyata ini bisa MACET
// PERMANEN di status "GERAK": begitu kendaraan benar-benar jalan lalu
// berhenti di posisi/kemiringan yang beda dari sebelumnya, baseline lama
// jadi basi dan TIDAK PERNAH bisa update lagi (update cuma jalan kalau
// status "diam", tapi status tidak pernah jadi "diam" karena baseline-nya
// basi - saling mengunci). Sekarang dipakai pendekatan yang tidak mungkin
// macet: membandingkan tiap bacaan ke BACAAN SEBELUMNYA (bukan ke baseline
// jangka panjang) - begitu kendaraan benar-benar diam (di posisi manapun),
// bacaan berturut-turut otomatis hampir sama sehingga deviasi otomatis
// turun ke dekat nol TANPA perlu "belajar" apa pun.
#define MPU_ADDR              0x68
#define MPU_SDA_PIN           21
#define MPU_SCL_PIN           22
#define MPU_REG_PWR_MGMT_1    0x6B
#define MPU_REG_ACCEL_CONFIG  0x1C
#define MPU_REG_GYRO_CONFIG   0x1B
#define MPU_REG_ACCEL_XOUT_H  0x3B
#define MPU_REG_GYRO_XOUT_H   0x43
#define BUZZER_PIN            23  // buzzer tamper - lihat bagian [MPU6050 TAMPER] di bawah

// Ambang deviasi accel antar-bacaan (raw unit, ±2g -> 16384 LSB/g) untuk
// dianggap "ada getaran/gerakan kendaraan".
// Dinaikkan dari 1500 -> 4000 (2026-09) berdasar referensi penelitian getaran
// kendaraan (MEMS accelerometer dashboard: idle mesin nyala ~0.07-0.15g,
// jalan sungguhan ~0.5-2.0g+, ambang acuan ~0.3g = ~4915 raw 1-sumbu) supaya
// tidak gampang salah anggap "GERAK" gara-gara getaran kecil/mesin idle.
// TETAP nilai perkiraan dari literatur, BUKAN hasil kalibrasi di kendaraan
// asli - kalau di lapangan ternyata kurang pas, jalankan dulu
// cetakDiagnostikMpu() di berbagai kondisi nyata (diam mesin mati, idle,
// jalan pelan, jalan kencang) lalu sesuaikan lagi.
#define MPU_MOTION_THRESHOLD  4000

bool  mpuTersedia = false;

int16_t mpuPrevAx = 0, mpuPrevAy = 0, mpuPrevAz = 0;
bool    mpuPrevInit = false;
long    mpuLastDeviasi = 0;    // buat cetakDiagnostikMpu() - lihat fungsi itu
bool    mpuSedangGerak = false;

// PENTING - ditemukan lewat pengujian nyata (2026-09): saat modem A7670E
// transmit data BERAT (buka sesi data/attach, atau kirim HTTP - BUKAN
// sekadar mencari sinyal, lihat tungguJaringanSambilPantauMpu() untuk itu),
// pembacaan I2C MPU6050 bisa melonjak TIDAK MEREPRESENTASIKAN gerakan fisik
// SAMA SEKALI (deviasi sampai puluhan ribu raw unit dalam sekejap, jauh di
// luar pola gerakan wajar, bertahan beberapa detik lalu hilang sendiri) -
// kemungkinan besar karena gangguan tegangan/RF sesaat di board yang sama,
// bukan bug logika deteksi. Solusinya: tandai kapan modem baru saja transmit
// data berat (tandaiGsmRfAktif(), dipanggil dari gsmConnect() dan
// kirimKeBackendGSM()), lalu ABAIKAN pembacaan MPU (jangan update apa pun -
// anggap datanya tidak bisa dipercaya) selama beberapa detik sesudahnya di
// mpuUpdate(). TIDAK dipakai untuk proses CARI SINYAL (waitForNetwork) -
// itu ditangani terpisah lewat polling manual supaya MPU tetap aktif
// SELAMA proses itu, bukan cuma diberi jendela tenang sesudahnya.
#define MPU_GSM_QUIET_MS  5000UL
unsigned long lastGsmRfMillis = 0;
void tandaiGsmRfAktif() { lastGsmRfMillis = millis(); }

void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool mpuBaca(int16_t &ax, int16_t &ay, int16_t &az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MPU_ADDR, 6) != 6) return false;
  ax = (Wire.read() << 8) | Wire.read();
  ay = (Wire.read() << 8) | Wire.read();
  az = (Wire.read() << 8) | Wire.read();
  return true;
}

// Dipakai KHUSUS oleh tamperUpdate() di bawah - baca accel+gyro sekaligus,
// terpisah dari mpuBaca() (dipakai deteksi gerak) supaya dua subsistem ini
// tidak saling berbagi state pembacaan sama sekali.
bool mpuBacaGyro(int16_t &gx, int16_t &gy, int16_t &gz) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_REG_GYRO_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MPU_ADDR, 6) != 6) return false;
  gx = (Wire.read() << 8) | Wire.read();
  gy = (Wire.read() << 8) | Wire.read();
  gz = (Wire.read() << 8) | Wire.read();
  return true;
}

void mpuSetup() {
  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);
  Wire.setClock(400000);

  mpuWrite(MPU_REG_PWR_MGMT_1, 0x00);  // MPU6050 default menyala dalam mode sleep, bangunkan dulu
  delay(50);
  mpuWrite(MPU_REG_ACCEL_CONFIG, 0x00);  // ±2g
  mpuWrite(MPU_REG_GYRO_CONFIG, 0x00);   // ±250 dps - dipakai tamperUpdate()

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);  // buzzer AKTIF (sudah punya osilator sendiri) - lihat catatan di BUZZER_FREQ_HZ

  Wire.beginTransmission(MPU_ADDR);
  mpuTersedia = (Wire.endTransmission() == 0);
  if (mpuTersedia) {
    Serial.println("MPU6050 siap - dipakai untuk deteksi gerak kendaraan (mode transmisi adaptif).");
  } else {
    Serial.println("MPU6050 tidak terdeteksi (cek wiring SDA/SCL/VCC/GND) - mode transmisi adaptif akan HANYA mengandalkan kecepatan GPS.");
  }
}

// Baca sensor & update status gerak. Hasilnya dibaca lewat mpuSedangGerak
// setelah memanggil ini - dipanggil SEKALI per iterasi loop().
void mpuUpdate() {
  if (!mpuTersedia) { mpuSedangGerak = false; return; }

  int16_t ax, ay, az;
  if (!mpuBaca(ax, ay, az)) { mpuSedangGerak = false; return; }

  // Modem baru saja/sedang transmit RF - bacaan I2C tidak bisa dipercaya
  // untuk beberapa detik ke depan (lihat penjelasan lengkap di
  // MPU_GSM_QUIET_MS). JANGAN update apa pun - biarkan tetap di status
  // TERAKHIR YANG VALID sampai gangguannya reda, supaya tidak salah pindah
  // mode transmisi gara-gara data yang rusak/tidak nyata.
  if (millis() - lastGsmRfMillis < MPU_GSM_QUIET_MS) {
    return;
  }

  if (!mpuPrevInit) {
    mpuPrevAx = ax; mpuPrevAy = ay; mpuPrevAz = az;
    mpuPrevInit = true;
    mpuSedangGerak = false;
    return;
  }

  long deviasi = labs(ax - (long)mpuPrevAx) + labs(ay - (long)mpuPrevAy) + labs(az - (long)mpuPrevAz);
  mpuLastDeviasi = deviasi;
  mpuSedangGerak = deviasi > MPU_MOTION_THRESHOLD;

  // SELALU update ke bacaan barusan, TANPA syarat apa pun - inilah yang
  // bikin algoritma ini tidak mungkin macet (lihat penjelasan di atas).
  mpuPrevAx = ax; mpuPrevAy = ay; mpuPrevAz = az;
}

// Cetak nilai deviasi mentah MPU ke Serial secara berkala - KHUSUS UNTUK
// KALIBRASI MPU_MOTION_THRESHOLD. Jalankan alat dalam berbagai kondisi nyata
// (mesin mati diam, mesin nyala idle, jalan pelan, jalan kencang, lewat
// polisi tidur) sambil amati angka "deviasi" di sini, lalu pilih
// MPU_MOTION_THRESHOLD di antara nilai "diam wajar" tertinggi dan nilai
// "benar-benar jalan" terendah.
#define MPU_DIAGNOSTIC_INTERVAL_MS  2000UL
unsigned long lastMpuDiagnosticMillis = 0;
void cetakDiagnostikMpu(bool gerakTerdeteksi) {
  if (!mpuTersedia) return;
  if (millis() - lastMpuDiagnosticMillis < MPU_DIAGNOSTIC_INTERVAL_MS) return;
  lastMpuDiagnosticMillis = millis();
  Serial.printf("[MPU kalibrasi] deviasi=%ld  ambang=%d  -> %s\n",
                mpuLastDeviasi, MPU_MOTION_THRESHOLD, gerakTerdeteksi ? "GERAK" : "diam");
}

// ==========================================================
// [MPU6050 TAMPER] DETEKSI ALAT DIANGKAT/DIBUKA PAKSA + BUZZER
// ==========================================================
// Wiring buzzer: (+) -> GPIO23, (-) -> GND (identik mpuAccelerator.ino).
//
// RISET (2026-09, sebelum implementasi - lihat percakapan sesi ini): sistem
// anti-theft berbasis accel+gyro yang terbukti dipakai di lapangan
// MEMBEDAKAN gerakan sah (guncangan jalan/polisi tidur/kendaraan sungguhan
// jalan) dari upaya alat diusik lewat KOMBINASI BEBERAPA syarat sekaligus,
// BUKAN cuma satu ambang amplitudo (ambang amplitudo saja terbukti gampang
// keliru - overlap antara guncangan wajar & tamper sungguhan). Sumber:
// IEEE "Anti-theft Integrated Bike Lock with Gyroscope Sensor and
// Accelerometer Motion Detection"; arXiv 2012.10293 "Low-Cost Compact
// Theft-Detection System using MPU-6050"; paten-paten anti-theft kendaraan
// yang memakai pola "adaptive threshold + event window + state machine"
// khusus untuk menyaring guncangan jalan. Tiga syarat yang dipakai di sini:
//   1) Deviasi akselerasi di atas ambang (ada sesuatu terjadi ke alat)
//   2) DISERTAI rotasi giroskop signifikan - mengangkat/membuka alat SELALU
//      memutar alat, sedangkan guncangan jalan/polisi tidur murni getaran
//      LINEAR dan nyaris tidak menghasilkan rotasi sebesar ini walau jalan
//      rusak parah
//   3) BERTAHAN terus-menerus minimal beberapa ratus ms - hentakan/bump
//      selalu reda < 500ms karena diredam suspensi kendaraan, sedangkan
//      upaya tamper sungguhan (diangkat, dibongkar) tidak reda secepat itu
// LAPISAN TAMBAHAN KHUSUS PROYEK INI (riset accel-only umumnya TIDAK punya
// ini karena tidak ada GPS): tamper HANYA dievaluasi saat mode transmisi
// TERAKHIR YANG DIKETAHUI bukan "BERGERAK" (lihat statusModeLabel di
// loop()) - kalau GPS+MPU sudah sama-sama bilang kendaraan sedang jalan,
// buzzer tamper dimatikan total selama itu, menghapus habis risiko salah
// bunyi gara-gara kendaraan benar-benar dikendarai (termasuk di jalan
// rusak/berlubang sekalipun).
//
// PENTING: baseline & state di bawah SENGAJA terpisah total dari algoritma
// deteksi gerak (mpuPrevAx dkk di atas) - baseline "belajar saat tenang"
// TIDAK aman untuk detektor yang berjalan berjam-jam (lihat penjelasan di
// bagian atas file), tapi AMAN untuk detektor tamper begini karena
// episodenya singkat (alarm ke AMAN lagi dalam hitungan detik).
// TEMUAN LAPANGAN #3 (2026-09-21, uji coba nyata): TAMPER_DISTURB_THRESHOLD
// awal (5700, ~0.35g gabungan 3 sumbu) TERLALU SENSITIF - kesenggol/digoyang
// ke samping dengan tangan saja sudah cukup memicu alarm, padahal MPU6050
// dikonfigurasi ±2g (16384 raw/g per sumbu) sehingga guncangan tangan wajar
// dengan mudah melewati ambang serendah itu. Dinaikkan jadi 11000 (~0.67g
// gabungan, sekitar 2x lipat) supaya jauh dari jangkauan senggolan/goyangan
// tangan biasa, tapi masih jauh di bawah gaya yang timbul saat alat benar
// benar diangkat/dicungkil paksa. NILAI INI PERKIRAAN AWAL (belum ada log
// kalibrasi nyata dari kejadian "digoyang ke samping" tsb) - kalau masih
// kurang pas, jalankan cetakDiagnostikTamper() (sudah ada di bawah, otomatis
// tercetak tiap 2 detik) sambil mengulang uji goyang-ke-samping yang sama,
// baca angka "deviasi=" persis pada saat alarm salah bunyi, lalu set ambang
// ini sedikit di atas angka tsb (pola yang sama dengan kalibrasi
// MPU_MOTION_THRESHOLD di atas).
#define TAMPER_DISTURB_THRESHOLD   11000L   // |Δx|+|Δy|+|Δz| accel raw, ~0.67g gabungan 3 sumbu (naik dari 5700 - lihat TEMUAN LAPANGAN #3)
#define TAMPER_ROTATION_THRESHOLD  6550L    // gyro raw (131 LSB/dps) -> ~50 dps
#define TAMPER_SUSTAINED_MS        800UL
#define TAMPER_RECOVER_MS          1000UL
#define TAMPER_BASELINE_ALPHA      0.01f
// GANTI 2026-09-29: buzzer dipasang ulang dengan model AKTIF (sudah punya
// osilator nada sendiri di dalamnya, DC 3-24V) menggantikan buzzer pasif
// kecil sebelumnya - dikonfirmasi Rafi langsung ke GPIO23 sudah cukup kuat
// menyalakannya (tidak perlu transistor). Buzzer aktif TIDAK dikasih
// frekuensi (tone()) seperti buzzer pasif - cukup dinyalakan/dimatikan
// biasa (digitalWrite HIGH/LOW), nadanya sudah tetap dari buzzer itu
// sendiri. Makanya BUZZER_FREQ_HZ dihapus - sudah tidak relevan lagi.

// BUG NYATA #1 (ditemukan lewat pengujian 2026-09): baseline tamper HANYA
// belajar saat tenang (!overThreshold) - by design (lihat komentar di atas),
// TAPI kalau alat dipindah/diletakkan PERMANEN di posisi/orientasi baru
// selagi sedang dievaluasi sebagai "diusik", deviasi dari baseline LAMA
// tidak akan pernah turun di bawah ambang lagi -> baseline tidak pernah
// sempat belajar ulang -> alarm MACET NYALA SELAMANYA (baru mati kalau
// power dicabut). Solusi: batasi durasi alarm maksimal - kalau sudah
// menyala selama ini TANPA sempat reda alami, PAKSA mati dan anggap posisi
// sekarang sebagai "normal baru" (reset baseline). Sama seperti alarm mobil
// sungguhan yang punya batas siklus bunyi, bukan menyala tanpa henti.
#define TAMPER_ALARM_MAX_MS        20000UL

// BUG NYATA #2 (ditemukan lewat pengujian 2026-09): mode "BERGERAK" dipakai
// untuk membatalkan alarm yang SEDANG aktif - tapi upaya mengangkat/mengusik
// alat SENDIRI menghasilkan gerakan MPU, dan GPS kadang membaca speed noise
// sedikit di atas ambang (umum terjadi indoor/multipath) - kombinasi
// kebetulan ini bisa membuat sistem salah simpulkan "kendaraan sedang jalan"
// PERSIS saat sedang diusik, lalu otomatis membungkam alarm yang seharusnya
// tetap berbunyi. Solusi DUA lapis:
//   1) Mode BERGERAK harus bertahan TERKONFIRMASI (bukan cuma satu fix GPS
//      sesaat) sebelum dianggap sungguhan, supaya satu noise GPS sesaat
//      tidak langsung dipercaya.
//   2) Bahkan setelah terkonfirmasi, kalau alarm SUDAH AKTIF, statusnya
//      TIDAK PERNAH dibatalkan lewat jalur ini - alarm yang sudah aktif
//      hanya boleh berhenti lewat recovery normal (tenang betulan) atau
//      TAMPER_ALARM_MAX_MS di atas. Ini krusial: kalau tidak, pencuri yang
//      mengangkat alat lalu langsung kabur membawa kendaraan otomatis
//      "membungkam" alarm sendiri lewat gerakan kaburnya.
#define TAMPER_BERGERAK_CONFIRM_MS  5000UL

float         tamperBaselineAx = 0, tamperBaselineAy = 0, tamperBaselineAz = 0;
bool          tamperBaselineInit = false;
bool          tamperDisturbed = false;
unsigned long tamperDisturbanceStart = 0;
unsigned long tamperLastDisturbanceTime = 0;
unsigned long tamperAlarmStartMillis = 0;
unsigned long tamperBergerakSejakMillis = 0;
bool          tamperSawRotation = false;
bool          tamperAlarmAktif = false;  // true = alarm terkonfirmasi, buzzer menyala
// true SESAAT setelah tamperAlarmAktif baru menyala (rising edge) - dibaca
// loop() untuk memaksa kirim SEKARANG JUGA (bukan menunggu interval mode
// biasa yang bisa sampai 30 menit di mode SLEEP) supaya peringatan diusik
// sampai ke backend/app nyaris real-time. Dibersihkan sendiri oleh loop()
// begitu sempat dipakai memaksa satu kali kirim.
bool          tamperKirimSegera = false;
// Uji tamper dari server (perintah MQTT "test_tamper", 2026-10-21): alarm
// dinyalakan lewat jalur yang SAMA dengan tamper sungguhan (buzzer + kirim
// paksa), tapi ditandai supaya backend/app bisa membedakannya:
//   tamperAlarmDariUji  - alarm yang sedang menyala berasal dari uji; paket
//                         terjadwal selama alarm ini TIDAK membawa bit tamper
//                         (supaya tidak tercatat sebagai tamper sungguhan),
//   tamperUjiTandaiKirim - SATU paket kirim-paksa berikutnya membawa bit3
//                         tamper + bit4 tamper_test, lalu dibersihkan.
bool          tamperAlarmDariUji = false;
bool          tamperUjiTandaiKirim = false;
// Buzzer uji ditahan minimal selama ini (alat diam, jadi recovery normal
// TAMPER_RECOVER_MS 1 detik terlalu singkat untuk didengar saat uji).
#define TAMPER_UJI_BUZZER_MS  3000UL


// Cetak nilai mentah deviasi & rotasi tamper ke Serial secara berkala -
// KHUSUS UNTUK KALIBRASI/DEBUG, supaya kelihatan kenapa alarm tidak/terpicu
// (mis. deviasi cukup tapi rotasi belum cukup, atau sebaliknya).
#define TAMPER_DIAGNOSTIC_INTERVAL_MS  2000UL
unsigned long lastTamperDiagnosticMillis = 0;
// Throttle terpisah untuk peringatan "tamper menunggu tapi belum pernah
// dapat fix sama sekali" di loop() - lihat "BUG NYATA #6".
unsigned long lastTamperNoFixWarnMillis = 0;
void cetakDiagnostikTamper(long deviasi, long rotasi, bool overThreshold) {
  if (millis() - lastTamperDiagnosticMillis < TAMPER_DIAGNOSTIC_INTERVAL_MS) return;
  lastTamperDiagnosticMillis = millis();
  Serial.printf("[TAMPER kalibrasi] deviasi=%ld/%ld  rotasi=%ld/%ld  overThreshold=%s  sawRotation=%s  alarmAktif=%s\n",
                deviasi, (long)TAMPER_DISTURB_THRESHOLD, rotasi, (long)TAMPER_ROTATION_THRESHOLD,
                overThreshold ? "ya" : "tidak", tamperSawRotation ? "ya" : "tidak", tamperAlarmAktif ? "ya" : "tidak");
}

// statusModeLabel & statusTamperLastMillis baru DIDEFINISIKAN belakangan di
// file ini (dashboard portal) - deklarasikan dulu di sini supaya bisa
// dipakai tamperUpdate().
extern String statusModeLabel;
extern unsigned long statusTamperLastMillis;

// Dipanggil TIAP iterasi loop() (lihat loop()) - independen dari mpuUpdate().
void tamperUpdate() {
  if (!mpuTersedia) return;

  // Sama seperti mpuUpdate(): abaikan bacaan MPU beberapa detik sesudah
  // modem transmit RF berat, supaya lonjakan I2C palsu (bisa puluhan ribu
  // raw unit, lihat catatan MPU_GSM_QUIET_MS) tidak dikira alat diusik.
  if (millis() - lastGsmRfMillis < MPU_GSM_QUIET_MS) return;

  bool modeBergerak = (statusModeLabel == "BERGERAK");
  if (modeBergerak) {
    if (tamperBergerakSejakMillis == 0) tamperBergerakSejakMillis = millis();
  } else {
    tamperBergerakSejakMillis = 0;
  }
  // Lihat catatan "BUG NYATA #2" di atas - butuh bertahan beberapa detik dulu
  // baru dipercaya sebagai kendaraan SUNGGUHAN jalan, bukan noise GPS sesaat.
  bool bergerakTerkonfirmasi = modeBergerak &&
      (millis() - tamperBergerakSejakMillis >= TAMPER_BERGERAK_CONFIRM_MS);

  if (bergerakTerkonfirmasi && !tamperAlarmAktif) {
    // Kendaraan TERKONFIRMASI benar-benar jalan DAN belum ada alarm aktif -
    // aman menahan evaluasi baru & reset baseline (supaya begitu berhenti
    // lagi, evaluasi mulai bersih, tidak mewarisi status "disturbed" dari
    // getaran jalan barusan; baseline lama sebelum jalan juga sudah basi).
    if (millis() - lastTamperDiagnosticMillis >= TAMPER_DIAGNOSTIC_INTERVAL_MS) {
      lastTamperDiagnosticMillis = millis();
      Serial.println("[TAMPER kalibrasi] dinonaktifkan sementara (mode=BERGERAK terkonfirmasi)");
    }
    tamperDisturbed = false;
    tamperBaselineInit = false;
    return;
  }
  // PENTING (lihat "BUG NYATA #2"): SENGAJA TIDAK ADA jalur di sini yang
  // membatalkan tamperAlarmAktif yang sedang true. Alarm yang sudah aktif
  // hanya boleh berhenti lewat recovery normal (tenang betulan, di bawah)
  // atau TAMPER_ALARM_MAX_MS (juga di bawah) - TIDAK PERNAH karena mode
  // berubah jadi BERGERAK, apapun alasannya.

  int16_t ax, ay, az, gx, gy, gz;
  if (!mpuBaca(ax, ay, az)) return;
  if (!mpuBacaGyro(gx, gy, gz)) return;

  if (!tamperBaselineInit) {
    tamperBaselineAx = ax; tamperBaselineAy = ay; tamperBaselineAz = az;
    tamperBaselineInit = true;
    return;
  }

  long dX = labs(ax - (long)tamperBaselineAx);
  long dY = labs(ay - (long)tamperBaselineAy);
  long dZ = labs(az - (long)tamperBaselineAz);
  long deviasi = dX + dY + dZ;
  long rotasi = labs((long)gx) + labs((long)gy) + labs((long)gz);

  bool overThreshold = deviasi > TAMPER_DISTURB_THRESHOLD;
  cetakDiagnostikTamper(deviasi, rotasi, overThreshold);

  if (overThreshold) {
    if (!tamperDisturbed) {
      tamperDisturbed = true;
      tamperDisturbanceStart = millis();
      tamperSawRotation = false;
    }
    if (rotasi > TAMPER_ROTATION_THRESHOLD) tamperSawRotation = true;

    unsigned long durasiGangguan = millis() - tamperDisturbanceStart;

    // Tamper terkonfirmasi hanya kalau deviasi bertahan lama DAN ada rotasi -
    // dua syarat ini yang membedakan "diangkat/dibuka" dari sekadar guncangan.
    if (!tamperAlarmAktif && tamperSawRotation && durasiGangguan >= TAMPER_SUSTAINED_MS) {
      tamperAlarmAktif = true;
      statusTamperLastMillis = millis();  // dibaca dashboard portal lokal - lihat handleStatus()
      tamperKirimSegera = true;  // minta loop() kirim posisi+status SEKARANG, jangan tunggu interval mode
      tamperAlarmStartMillis = millis();
      digitalWrite(BUZZER_PIN, HIGH);
      Serial.printf("STATUS: PERINGATAN - ALAT DIUSIK! deviasi=%ld rotasi=%ld (buzzer menyala)\n",
                    deviasi, rotasi);
    }
    tamperLastDisturbanceTime = millis();
  } else {
    // Di bawah ambang lagi: kalau belum sempat jadi alarm, ini cuma hentakan
    // sesaat - diabaikan diam-diam, tanpa bunyi.
    tamperDisturbed = false;
  }

  // Alarm mati setelah alat tenang (di bawah ambang) terus-menerus selama
  // TAMPER_RECOVER_MS.
  if (tamperAlarmAktif && !overThreshold && (millis() - tamperLastDisturbanceTime > TAMPER_RECOVER_MS) &&
      !(tamperAlarmDariUji && millis() - tamperAlarmStartMillis < TAMPER_UJI_BUZZER_MS)) {
    tamperAlarmAktif = false;
    tamperAlarmDariUji = false;
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("STATUS: AMAN (buzzer mati)");
  }

  // Alarm mati PAKSA kalau sudah menyala terlalu lama tanpa sempat reda
  // alami - lihat catatan "BUG NYATA #1" di atas. Baseline SENGAJA direset
  // di sini (bukan cuma buzzer dimatikan) supaya posisi/orientasi SEKARANG
  // dipakai sebagai acuan "normal" yang baru, jadi evaluasi berikutnya tidak
  // langsung macet lagi gara-gara baseline lama sudah tidak relevan.
  if (tamperAlarmAktif && (millis() - tamperAlarmStartMillis >= TAMPER_ALARM_MAX_MS)) {
    tamperAlarmAktif = false;
    tamperAlarmDariUji = false;
    tamperDisturbed = false;
    digitalWrite(BUZZER_PIN, LOW);
    tamperBaselineInit = false;
    Serial.println("STATUS: AMAN (buzzer dimatikan paksa - durasi alarm maksimal tercapai, baseline direset ke posisi sekarang)");
  }

  // Baseline hanya belajar saat alat benar-benar tenang, supaya tidak ikut
  // "menyerap" orientasi baru selama proses tamper sedang berlangsung/dievaluasi.
  if (!overThreshold && !tamperAlarmAktif) {
    tamperBaselineAx += (ax - tamperBaselineAx) * TAMPER_BASELINE_ALPHA;
    tamperBaselineAy += (ay - tamperBaselineAy) * TAMPER_BASELINE_ALPHA;
    tamperBaselineAz += (az - tamperBaselineAz) * TAMPER_BASELINE_ALPHA;
  }
}

// ==========================================================
// [7] POWER-ON MODEM A7670E (khusus untuk aktifkan GNSS-nya)
// ==========================================================
void modemPowerOn() {
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
  delay(100);
  digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);
  delay(2600);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);

  pinMode(MODEM_DTR_PIN, OUTPUT);
  digitalWrite(MODEM_DTR_PIN, LOW);

  pinMode(BOARD_PWRKEY_PIN, OUTPUT);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);
  delay(100);
  digitalWrite(BOARD_PWRKEY_PIN, HIGH);
  delay(MODEM_POWERON_PULSE_WIDTH_MS);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);
}

// ==========================================================
// [7b] REGISTRASI JARINGAN SELULER (KARTU SIM/APN) - JALUR KIRIM DATA
// ==========================================================
// Pengganti modem.waitForNetwork() bawaan TinyGSM - isi aslinya (lihat
// TinyGsmModem.tpp:waitForNetworkImpl) cuma loop yang cek
// modem.isNetworkConnected() tiap 250ms sampai timeout. Ditulis ulang manual
// di sini SUPAYA bisa menyisipkan mpuUpdate()+tamperUpdate() (+
// server.handleClient() untuk portal) di setiap putarannya - kalau pakai
// versi bawaan, MPU (dan portal) beku total sepanjang proses ini berlangsung
// (sampai 60 detik), yang berarti alat bisa diusik tanpa terdeteksi sama
// sekali selama itu. isNetworkConnected() sendiri adalah fungsi PUBLIK
// bawaan TinyGSM, aman dipanggil langsung.
//
// BUG NYATA yang ditemukan lewat pengujian (2026-09): tamperUpdate() SEMPAT
// TIDAK dipanggil di sini (cuma mpuUpdate()) karena fungsi tamper baru
// ditambahkan belakangan setelah fungsi ini ditulis - akibatnya buzzer
// tamper "bisu" total setiap kali alat sedang mencari sinyal seluler
// (sampai 60 detik tiap percobaan, dan di sinyal lemah/indoor ini bisa
// terjadi BERULANG KALI - lihat log: buzzer sempat bunyi normal ~20 detik,
// lalu bisu lagi begitu modem coba sambung ulang). Sudah diperbaiki dengan
// menambahkan tamperUpdate() di sini juga, PERSIS pola yang sama dengan
// mpuUpdate() di atas.
bool tungguJaringanSambilPantauMpu(uint32_t timeout_ms) {
  for (uint32_t start = millis(); millis() - start < timeout_ms;) {
    if (modem.isNetworkConnected()) return true;
    mpuUpdate();
    // WAJIB dipanggil di sini juga (bukan cuma di loop() utama) - kalau
    // tidak, status gerak MPU tetap ter-update dengan benar di belakang
    // layar selama proses ini (sudah diverifikasi lewat pengujian nyata
    // dengan print debug sementara), TAPI log kalibrasi jadi kosong sama
    // sekali selama proses pencarian berlangsung, kelihatan seperti MPU
    // tidak terpantau padahal sebenarnya terpantau.
    cetakDiagnostikMpu(mpuSedangGerak);
    tamperUpdate();  // WAJIB juga - lihat catatan bug di atas

    // BUG NYATA #7 (ditemukan lewat pengujian nyata 2026-09-22, log Serial):
    // fungsi ini sudah benar menjaga MPU+tamper tetap AKTIF MENDETEKSI
    // selama proses cari sinyal berlangsung (lihat catatan di atas), TAPI
    // pengiriman/notifikasi tamper-nya sendiri (tamperKirimSegera, lihat
    // "BUG NYATA #4/#5/#6") ada di loop() UTAMA, yang baru dijalankan lagi
    // SETELAH fungsi blocking ini selesai - jadi walau alarm terdeteksi &
    // berbunyi tepat waktu, notifikasi ke backend/app baru terkirim SETELAH
    // proses cari sinyal 60 detik ini tuntas (kejadian nyata: alarm jam
    // 21:20:07, baru terkirim jam 21:21:07 - persis 60 detik kemudian).
    // Solusi: begitu ada tamper yang menunggu kiriman di tengah proses ini,
    // LANGSUNG keluar (anggap "belum dapat sinyal" untuk percobaan ini) -
    // supaya loop() utama secepatnya dapat giliran mencoba kirim (lewat
    // WiFi kalau GSM memang belum siap, lihat kirimKeBackend()). GSM tetap
    // akan dicoba lagi otomatis di percobaan berikutnya seperti biasa
    // (lihat GSM_RECONNECT_COOLDOWN_MS) - ini cuma menunda percobaan GSM
    // itu, tidak membatalkannya, dan prioritas tamper jauh lebih penting
    // daripada beberapa detik lebih cepat sambung GSM untuk telemetri biasa.
    if (tamperKirimSegera) {
      Serial.println("[TAMPER] Keluar lebih awal dari proses cari sinyal seluler - ada tamper yang perlu dikirim segera, coba WiFi dulu.");
      return false;
    }

    server.handleClient();
    pastikanWiFiTersambung();  // non-blocking - WiFi cadangan tetap bisa tersambung selama GSM cari sinyal
    // Keepalive MQTT WiFi (15 s) WAJIB tetap jalan selama blocking ini (bisa
    // 60 s) - kalau tidak, broker memutus sesi WiFi dan perintah cmd/<SN>
    // tidak bisa masuk. Pesan masuk cukup diantrekan & diproses di sini.
    mqttWifiJagaKeepalive();
    layaniPerintahMqtt();
    delay(250);
  }
  return false;
}

bool gsmConnect() {
  Serial.println("Cek kartu SIM...");
  SimStatus sim = SIM_ERROR;
  int simRetry = 0;
  while (sim != SIM_READY && simRetry++ < 15) {
    sim = modem.getSimStatus();
    if (sim == SIM_READY) {
      Serial.println("Kartu SIM terbaca.");
    } else if (sim == SIM_LOCKED) {
      Serial.println("Kartu SIM terkunci PIN - tidak bisa lanjut, lepas dulu PIN-nya di HP biasa.");
      return false;
    } else {
      // Loop ini biasanya cuma sekali putaran (SIM langsung terbaca), tapi
      // tetap dijaga MPU+tamper aktif kalau kebetulan lambat (SIM error/retry).
      mpuUpdate();
      tamperUpdate();
      server.handleClient();
      delay(1000);
    }
  }
  if (sim != SIM_READY) {
    Serial.println("Kartu SIM tidak terbaca. Cek pemasangan kartu.");
    return false;
  }

  Serial.print("Menunggu sinyal jaringan seluler");
  // Dikembalikan ke 60 detik (sempat diperpendek ke 15 detik untuk stabilitas
  // portal, TAPI itu regresi nyata: registrasi ke tower operator bisa
  // butuh lebih dari 15 detik di sinyal lemah/indoor, jadi malah keburu
  // di-timeout sebelum sempat berhasil - padahal ini TIDAK ADA hubungannya
  // dengan kuota data, cuma soal waktu tunggu sinyal). Stabilitas portal
  // sekarang dijamin lewat cooldown di pastikanGsmTersambung() yang dihitung
  // dari AKHIR percobaan (bukan awal) - jadi durasi SATU percobaan boleh
  // lama tanpa mengorbankan jeda istirahat untuk portal sesudahnya.
  //
  // PAKAI tungguJaringanSambilPantauMpu() DI SINI, BUKAN modem.waitForNetwork()
  // bawaan - ditemukan lewat pengujian nyata (log Serial menunjukkan celah
  // 60 detik PENUH tanpa satu pun pembacaan MPU): modem.waitForNetwork()
  // bawaan memblokir SELURUH program (termasuk MPU) sampai selesai. Kalau
  // alat diusik TEPAT saat proses ini berlangsung, usikan itu SAMA SEKALI
  // tidak terdeteksi - risiko keamanan nyata untuk alat yang mengandalkan
  // MPU. Lihat definisi tungguJaringanSambilPantauMpu() untuk detail.
  if (!tungguJaringanSambilPantauMpu(60000UL)) {
    Serial.println("\nTimeout menunggu sinyal jaringan seluler.");
    return false;
  }
  Serial.println();

  Serial.printf("Menghubungkan data seluler (APN: %s)...\n", config.apn);
  tandaiGsmRfAktif();  // buka sesi data (attach) juga transmit RF
  if (!modem.gprsConnect(config.apn, "", "")) {
    // Jaringan seluler terdaftar normal (waitForNetwork di atas sukses) tapi
    // sesi data gagal dibuka - kalau ini terjadi berkali-kali berturut-turut,
    // itu indikasi kuat kuota/pulsa data habis (bukan soal sinyal/registrasi).
    // Lihat "DETEKSI KUOTA SIM HABIS" di komentar atas file.
    consecutiveGprsFailWhileRegistered++;
    Serial.printf("Gagal menyambungkan data seluler (gagal ke-%d berturut-turut padahal jaringan seluler SUDAH terdaftar) - cek APN, atau kemungkinan kuota/pulsa data habis.\n",
                  consecutiveGprsFailWhileRegistered);
    if (consecutiveGprsFailWhileRegistered >= QUOTA_WARNING_THRESHOLD && !quotaLikelyExhausted) {
      quotaLikelyExhausted = true;
      Serial.println("!!! PERINGATAN: kemungkinan besar KUOTA/PULSA DATA SIM CARD HABIS (jaringan terdaftar normal, tapi sesi data gagal dibuka berkali-kali). Cek isi ulang paket data operator Anda. !!!");
    }
    tandaiGsmRfAktif();  // percobaan attach yang gagal tetap transmit RF
    return false;
  }
  tandaiGsmRfAktif();  // attach baru saja selesai - perpanjang jendela tenang MPU
  consecutiveGprsFailWhileRegistered = 0;
  if (quotaLikelyExhausted) {
    Serial.println("Sesi data seluler berhasil lagi - peringatan kuota sebelumnya dianggap sudah tidak berlaku.");
  }
  quotaLikelyExhausted = false;

  // Jeda singkat: PDP context yang baru dibuka tidak selalu langsung siap
  // dipakai TLS/DNS - request pertama yang langsung ditembak kadang gagal
  // resolve DNS karena jalur routing-nya belum sepenuhnya settle.
  delay(2000);

  Serial.println("Data seluler aktif.");
  return true;
}

void pastikanGsmTersambung() {
  gsmRegistered = modem.isGprsConnected();
  if (gsmRegistered) return;

  // Cooldown - lihat penjelasan lengkap di GSM_RECONNECT_COOLDOWN_MS di atas.
  if (millis() - lastGsmAttemptMillis < GSM_RECONNECT_COOLDOWN_MS) return;

  // Uji 2026-10-21 (antena GSM dicabut, WiFi jalan): gsmConnect() menahan
  // loop() sampai 60 detik menunggu sinyal tiap 20 detik - selama itu
  // telemetry tidak terkirim (interval 30 detik jadi ~80 detik). Selama
  // WiFi sedang tersambung, cukup cek CEPAT apakah modem sudah terdaftar -
  // modem tetap mencari jaringan sendiri di background, dan gsmConnect()
  // baru dipanggil begitu terdaftar (tahap tunggu sinyalnya langsung lolos).
  static bool lewatiTungguSinyalDicetak = false;
  if (strlen(config.ssid) > 0 && WiFi.status() == WL_CONNECTED && !modem.isNetworkConnected()) {
    if (!lewatiTungguSinyalDicetak) {
      Serial.println("[GSM] Belum terdaftar di jaringan seluler - WiFi jadi jalur data, GSM dicek cepat berkala (tanpa blocking).");
      lewatiTungguSinyalDicetak = true;
    }
    lastGsmAttemptMillis = millis();
    return;
  }
  lewatiTungguSinyalDicetak = false;

  Serial.println("Jaringan seluler terputus, mencoba sambungkan ulang...");
  gsmRegistered = gsmConnect();

  // SENGAJA dicatat SETELAH gsmConnect() selesai (bukan sebelum) - gsmConnect()
  // sendiri bisa blocking cukup lama (sampai puluhan detik menunggu sinyal).
  // Kalau dicatat SEBELUM, dan durasi percobaan itu sendiri sudah melebihi
  // cooldown, jeda ini jadi tidak berarti sama sekali (percobaan berikutnya
  // langsung terpicu lagi di iterasi loop() selanjutnya tanpa jeda nyata).
  // Dicatat di sini menjamin ADA jeda nyata setelah setiap percobaan, jadi
  // portal selalu kebagian waktu jalan normal di antaranya - berapa pun lama
  // satu percobaan gsmConnect() berlangsung.
  lastGsmAttemptMillis = millis();
}

// ==========================================================
// [8] WIFI + PORTAL KONFIGURASI (pola sama seperti koding/BLE/BLE.ino)
// ==========================================================
// PENTING (stabilitas portal): dulu fungsi ini BLOCKING - menunggu sampai
// tersambung atau timeout (config.wifi_timeout detik) lewat while(delay(300))
// TANPA memanggil server.handleClient() sama sekali selama menunggu. Itu
// bikin portal config tidak terjangkau selama proses ini berlangsung (bisa
// puluhan detik), DITAMBAH lagi ESP32 cuma punya SATU radio WiFi - selama
// STA sedang scan/connect ke jaringan lain, Access Point yang sedang dipakai
// user untuk membuka portal bisa ikut tersendat (keterbatasan hardware, radio
// harus gantian channel). Sekarang NON-BLOCKING: cuma memulai proses connect
// lalu langsung kembali - loop()/server.handleClient() tetap jalan normal
// selama proses di background. Hasil sambung/gagal dilaporkan async lewat
// onWifiEvent(), bukan return value fungsi ini.
// [WIFI KUAT] (2026-10-21) - uji nyata: "WiFi lokal terputus." tiap ~2.8
// detik tanpa pernah tersambung, padahal user yakin SSID/password benar.
// Penyebab yang terlihat: SSID tersimpan "Acpridan " (ada SPASI di akhir -
// autocomplete keyboard HP waktu isi portal), alasan putus tidak pernah
// dicetak, dan WiFi.begin() manual bentrok dengan auto-reconnect core
// ("sta is connecting, cannot set config"). Sekarang tiap percobaan:
//   1. hentikan percobaan STA yang masih jalan (Access Point portal tetap
//      hidup - mode tetap AP_STA),
//   2. scan 2.4 GHz secara ASYNC (tidak menahan loop/portal/tamper),
//   3. cocokkan SSID: persis -> tanpa spasi tepi -> beda huruf besar/kecil,
//      pilih BSSID dengan sinyal terkuat, lalu sambung ke BSSID+channel itu,
//   4. kalau tidak terlihat, cetak daftar jaringan yang terlihat + alasannya
//      (ESP32 hanya 2.4 GHz - router yang cuma 5 GHz tidak akan pernah muncul).
// Alasan putus dicetak beserta petunjuknya. Password TIDAK pernah dicetak.
bool          wifiScanBerjalan = false;
uint8_t       wifiAlasanPutusTerakhir = 0;
unsigned long wifiAlasanPutusDicetakMillis = 0;
uint8_t       wifiGagalPasswordBeruntun = 0;
bool          wifiPakaiPasswordTrim = false;
#define WIFI_SCAN_TIMEOUT_MS  15000UL

bool wifiAlasanSoalPassword(uint8_t r) {
  return r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
         r == WIFI_REASON_HANDSHAKE_TIMEOUT || r == WIFI_REASON_AUTH_EXPIRE ||
         r == WIFI_REASON_MIC_FAILURE;
}

const char* wifiPetunjukAlasan(uint8_t r) {
  if (r == WIFI_REASON_NO_AP_FOUND) return "SSID tidak terlihat - cek nama persis (spasi/huruf besar) & router WAJIB menyiarkan 2.4 GHz (ESP32 tidak bisa 5 GHz)";
  if (wifiAlasanSoalPassword(r)) return "kemungkinan besar PASSWORD salah (cek huruf besar/kecil & spasi)";
  if (r == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY || r == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD)
    return "mode keamanan router tidak cocok (mis. WPA3-only) - set router ke WPA2 atau WPA2/WPA3";
  if (r == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD || r == WIFI_REASON_BEACON_TIMEOUT)
    return "sinyal terlalu lemah/hilang - dekatkan alat ke router";
  return "";
}

const char* wifiNamaAuth(wifi_auth_mode_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN:          return "terbuka";
    case WIFI_AUTH_WEP:           return "WEP";
    case WIFI_AUTH_WPA_PSK:       return "WPA";
    case WIFI_AUTH_WPA2_PSK:      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:  return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default:                      return "lainnya/enterprise";
  }
}

bool wifiPasswordAdaSpasiTepi() {
  size_t n = strlen(config.password);
  return n > 0 && (isspace((unsigned char)config.password[0]) || isspace((unsigned char)config.password[n - 1]));
}

// Password yang dipakai untuk percobaan ini. Kalau password tersimpan
// diawali/diakhiri spasi DAN percobaan sebelumnya gagal karena password,
// coba versi tanpa spasi tepi (bergantian, satu percobaan tiap siklus).
const char* wifiPasswordDipakai() {
  static String trimmed;
  if (!wifiPakaiPasswordTrim) return config.password;
  trimmed = String(config.password);
  trimmed.trim();
  return trimmed.c_str();
}

void wifiSambungDariHasilScan(int16_t n) {
  String target = String(config.ssid);
  String targetTrim = target;
  targetTrim.trim();

  int16_t pilih = -1;
  int tingkat = 0;  // 3 = persis, 2 = sama setelah buang spasi tepi, 1 = beda huruf besar/kecil saja
  for (int16_t i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    int t = 0;
    if (s == target) t = 3;
    else if (s == targetTrim) t = 2;
    else if (s.equalsIgnoreCase(targetTrim)) t = 1;
    if (t == 0) continue;
    if (pilih < 0 || t > tingkat || (t == tingkat && WiFi.RSSI(i) > WiFi.RSSI(pilih))) {
      pilih = i;
      tingkat = t;
    }
  }

  if (pilih < 0) {
    Serial.printf("[WiFi] SSID \"%s\" TIDAK terlihat. %d jaringan 2.4 GHz yang terlihat:\n", config.ssid, n);
    for (int16_t i = 0; i < n && i < 12; i++) {
      Serial.printf("[WiFi]   \"%s\"  RSSI %d dBm  ch %ld  %s\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                    (long)WiFi.channel(i), wifiNamaAuth(WiFi.encryptionType(i)));
    }
    Serial.println("[WiFi] Kalau nama WiFi Anda tidak ada di daftar: router kemungkinan cuma menyiarkan 5 GHz (ESP32 hanya bisa 2.4 GHz), atau alat terlalu jauh. Dicoba lagi otomatis.");
    WiFi.scanDelete();
    return;
  }

  String ssidPakai = WiFi.SSID(pilih);
  int32_t channel = WiFi.channel(pilih);
  uint8_t bssid[6];
  memcpy(bssid, WiFi.BSSID(pilih), 6);
  if (tingkat < 3) {
    Serial.printf("[WiFi] SSID tersimpan \"%s\" tidak persis sama dengan yang terlihat - dipakai \"%s\" (beda spasi/huruf besar). Simpan ulang lewat portal supaya rapi.\n",
                  config.ssid, ssidPakai.c_str());
  }
  if (wifiPakaiPasswordTrim) {
    Serial.println("[WiFi] Password tersimpan diawali/diakhiri spasi & sebelumnya ditolak - dicoba versi TANPA spasi tepi.");
  }
  Serial.printf("[WiFi] Menyambungkan ke \"%s\" (RSSI %d dBm, ch %ld, %s, password %u karakter)...\n",
                ssidPakai.c_str(), WiFi.RSSI(pilih), (long)channel, wifiNamaAuth(WiFi.encryptionType(pilih)),
                (unsigned)strlen(wifiPasswordDipakai()));
  WiFi.scanDelete();
  WiFi.begin(ssidPakai.c_str(), wifiPasswordDipakai(), channel, bssid);
}

// Cek hasil scan async - dipanggil dari pastikanWiFiTersambung().
void wifiSelesaikanScan() {
  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    if (millis() - lastWifiAttemptMillis < WIFI_SCAN_TIMEOUT_MS) return;
    Serial.println("[WiFi] Scan tidak selesai-selesai - dibatalkan, dicoba lagi nanti.");
    WiFi.scanDelete();
    wifiScanBerjalan = false;
    return;
  }
  wifiScanBerjalan = false;
  if (n < 0) {
    Serial.println("[WiFi] Scan gagal - coba sambung langsung tanpa scan.");
    String ssidTrim = String(config.ssid);
    ssidTrim.trim();
    WiFi.begin(ssidTrim.c_str(), wifiPasswordDipakai());
    return;
  }
  wifiSambungDariHasilScan(n);
}

// NON-BLOCKING: cuma memulai satu percobaan (scan async) lalu langsung
// kembali - kelanjutannya di wifiSelesaikanScan(), hasil akhirnya lewat
// onWifiEvent().
void connectWiFi() {
  lastWifiAttemptMillis = millis();
  WiFi.mode(WIFI_AP_STA);  // WAJIB AP_STA (bukan STA polos) - supaya Access Point TETAP aktif walau kita (re)konek STA
  if (strlen(config.hostname) > 0) WiFi.setHostname(config.hostname);
  WiFi.setSleep(false);  // tanpa power-save: koneksi TLS/MQTT lewat WiFi lebih stabil & responsif

  // Satu percobaan ekstra pakai password tanpa spasi tepi tiap kali gagal
  // karena password (bergantian, tidak membanjiri router).
  wifiPakaiPasswordTrim = wifiPasswordAdaSpasiTepi() && wifiGagalPasswordBeruntun > 0 && !wifiPakaiPasswordTrim;

  // Hentikan percobaan STA yang masih jalan (auto-reconnect core) - selama
  // STA "connecting", scan & begin baru ditolak. wifioff=false, eraseap=false:
  // Access Point portal & kredensial tetap utuh.
  WiFi.disconnect(false, false);
  int16_t r = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false, /*passive=*/false, /*max_ms_per_chan=*/150);
  if (r == WIFI_SCAN_FAILED) {
    Serial.println("[WiFi] Scan gagal dimulai - coba sambung langsung tanpa scan.");
    String ssidTrim = String(config.ssid);
    ssidTrim.trim();
    WiFi.begin(ssidTrim.c_str(), wifiPasswordDipakai());
    return;
  }
  wifiScanBerjalan = true;
  Serial.printf("[WiFi] Memindai jaringan 2.4 GHz untuk mencari \"%s\" (di background, tidak menahan portal)...\n", config.ssid);
}

// Dipanggil otomatis oleh ESP32 WiFi stack saat status STA berubah - lihat
// WiFi.onEvent() di setup().
void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.printf("WiFi lokal tersambung ke \"%s\". IP: %s  RSSI %d dBm  ch %d\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
    wifiGagalPasswordBeruntun = 0;
    wifiAlasanPutusTerakhir = 0;
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    uint8_t r = info.wifi_sta_disconnected.reason;
    if (r == WIFI_REASON_ASSOC_LEAVE) return;  // diputus sendiri oleh alat (mis. sebelum scan) - normal
    if (wifiAlasanSoalPassword(r) && wifiGagalPasswordBeruntun < 255) wifiGagalPasswordBeruntun++;
    // Core bisa melapor alasan yang sama berkali-kali per detik - cetak hanya
    // kalau alasannya berubah atau sudah 30 detik sejak cetak terakhir.
    if (r != wifiAlasanPutusTerakhir || millis() - wifiAlasanPutusDicetakMillis >= 30000UL) {
      wifiAlasanPutusTerakhir = r;
      wifiAlasanPutusDicetakMillis = millis();
      const char* petunjuk = wifiPetunjukAlasan(r);
      Serial.printf("WiFi lokal terputus (alasan %u: %s)%s%s\n", r, WiFi.disconnectReasonName((wifi_err_reason_t)r),
                    petunjuk[0] ? " - " : "", petunjuk);
    }
  }
}

// Access Point (SoftAP) SEKARANG SELALU AKTIF (dipanggil sekali di setup(),
// tidak pernah dimatikan) - IP-nya (192.168.4.1, default ESP32) permanen,
// tidak pernah berubah, jadi portal config selalu bisa dibuka dengan connect
// ke SSID ini kapan pun, apa pun status GSM/WiFi lokal. Lihat "AKSES PORTAL
// CONFIG PERMANEN" di komentar atas file.
void setupAP() {
  String apSsid = strlen(config.hostname) > 0 ? String(config.hostname) : "GPS-TRACKER";
  WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
  Serial.printf("Access Point permanen aktif. SSID: %s  Password: %s  IP: %s (TIDAK PERNAH BERUBAH)\n",
                apSsid.c_str(), AP_PASSWORD, WiFi.softAPIP().toString().c_str());
  Serial.println("Buka browser ke IP di atas kapan saja untuk mengisi WiFi/nama alat/APN, walaupun alat sudah tersambung SIM card/WiFi lain.");
}

// Ubah hostname/AP SSID bebas (huruf besar, spasi, dst) jadi slug yang valid
// untuk mDNS (huruf kecil, angka, strip) - alamat mDNS punya aturan karakter
// lebih ketat daripada SSID WiFi biasa.
String mdnsSlugFromHostname() {
  String base = strlen(config.hostname) > 0 ? String(config.hostname) : "gps-tracker";
  String slug;
  for (size_t i = 0; i < base.length(); i++) {
    char c = base.charAt(i);
    if (isalnum((unsigned char)c)) {
      slug += (char)tolower(c);
    } else if (slug.length() > 0 && slug.charAt(slug.length() - 1) != '-') {
      slug += '-';
    }
  }
  while (slug.length() > 0 && slug.charAt(slug.length() - 1) == '-') slug.remove(slug.length() - 1);
  if (slug.length() == 0) slug = "gps-tracker";
  return slug;
}

// Alamat "permanen" untuk WiFi lokal (rumah/kantor): http://<slug>.local -
// tidak berubah walau IP DHCP-nya berubah tiap reboot. Dipanggil sekali di
// setup(), setelah Access Point + percobaan STA awal.
void startMDNS() {
  mdnsHostname = mdnsSlugFromHostname();
  if (MDNS.begin(mdnsHostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS aktif: http://%s.local (bisa diakses dari WiFi lokal yang sama tanpa perlu tahu IP)\n", mdnsHostname.c_str());
  } else {
    Serial.println("Gagal memulai mDNS - portal tetap bisa diakses lewat IP (Access Point permanen di atas, atau IP WiFi lokal).");
    mdnsHostname = "";
  }
}

// Delay yang tetap melayani portal web di tengah-tengah menunggu, supaya
// halaman config tidak terasa macet kapan pun dibuka - dipakai menggantikan
// delay() polos di titik-titik yang sering dipanggil berulang.
void delayLayaniPortal(unsigned long ms) {
  unsigned long start = millis();
  do {
    server.handleClient();
    layaniPerintahMqtt();  // pesan masuk cmd/<DEVICE_SN> - lihat [PERINTAH MQTT]
    delay(10);
  } while (millis() - start < ms);
}

// GSM (kartu SIM) tetap JALUR UTAMA untuk kirim data (lihat kirimKeBackend())
// - fungsi ini TIDAK memengaruhi prioritas itu. WiFi lokal di sini dijaga
// TERLEPAS dari status GSM, dengan dua alasan: (1) supaya portal config bisa
// diakses dari WiFi rumah/kantor lewat mDNS, tidak cuma dari SoftAP alat, dan
// (2) tetap siap jadi cadangan kirim data kalau GSM gagal. Access Point
// (SoftAP) permanen terpisah dari ini, tidak pernah dimatikan - lihat
// setupAP(). Diberi cooldown supaya loop() (yang jalan sangat sering) tidak
// memanggil WiFi.begin() (di connectWiFi()) berulang-ulang tanpa jeda kalau
// sedang gagal terus - connectWiFi() sendiri sudah non-blocking, tapi tetap
// tidak perlu dipanggil ulang tiap iterasi selama proses sebelumnya belum
// selesai/gagal.
void pastikanWiFiTersambung() {
  if (strlen(config.ssid) == 0) return;       // WiFi lokal tidak dikonfigurasi - tidak ada yang perlu dijaga
  if (wifiScanBerjalan) {                     // percobaan sedang jalan - lanjutkan begitu scan selesai
    wifiSelesaikanScan();
    return;
  }
  if (WiFi.status() == WL_CONNECTED) return;  // sudah tersambung, tidak perlu apa-apa

  if (millis() - lastWifiAttemptMillis < WIFI_RECONNECT_COOLDOWN_MS) return;

  Serial.println("WiFi lokal terputus - mencoba sambungkan ulang (untuk akses portal & cadangan kirim data)...");
  connectWiFi();
}

// Dipanggil dari jalur kirim data saat GSM baru gagal dan butuh WiFi sebagai
// cadangan SEKARANG JUGA. Menghormati cooldown yang sama dengan
// pastikanWiFiTersambung() supaya tidak dobel-retry beruntun.
bool wifiSiapDipakaiCadangan() {
  if (strlen(config.ssid) == 0) return false;  // WiFi lokal tidak dikonfigurasi
  if (WiFi.status() != WL_CONNECTED) {
    pastikanWiFiTersambung();
  }
  return WiFi.status() == WL_CONNECTED;
}

// ---- Halaman portal: dashboard, form konfigurasi, simpan, status, css ----
void handleRoot() {
  String html = R"(
  <!DOCTYPE html>
  <html>
  <head>
    <title>GPS Tracker Dashboard</title>
    <link rel="stylesheet" href="/style.css">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <script>
      function modeBadge(mode) {
        return mode || '-';
      }
      function formatDurasi(detik) {
        if (detik < 60) return detik + ' detik lalu';
        const menit = Math.floor(detik / 60);
        const sisaDetik = detik % 60;
        if (menit < 60) return menit + ' menit ' + sisaDetik + ' detik lalu';
        const jam = Math.floor(menit / 60);
        const sisaMenit = menit % 60;
        return jam + ' jam ' + sisaMenit + ' menit ' + sisaDetik + ' detik lalu';
      }
      async function updateStatus() {
        try {
          const res = await fetch('/status',{cache:'no-store'});
          const data = await res.json();
          document.getElementById('status').innerHTML = `
            <h2>Status</h2>
            <p><strong>Access Point (permanen):</strong> ${data.ap_ssid} @ ${data.ap_ip} - selalu bisa diakses, tidak pernah berubah</p>
            <p><strong>WiFi Lokal:</strong> ${data.wifi_connected ? (data.wifi_ssid + ' @ ' + data.wifi_ip) : 'Belum tersambung'}</p>
            ${data.mdns_host ? `<p><strong>Alamat Permanen (WiFi lokal):</strong> http://${data.mdns_host}.local</p>` : ''}
            <p><strong>Nama Kapal/Alat:</strong> ${data.device_name}</p>
            <p><strong>APN Seluler:</strong> ${data.apn}</p>
            <p><strong>Jaringan Seluler:</strong> ${data.gsm_registered ? 'Terdaftar' : 'Belum/Terputus'}</p>
            ${data.quota_likely_exhausted ? '<p><strong>PERINGATAN:</strong> kemungkinan besar kuota/pulsa data SIM habis (jaringan terdaftar tapi sesi data gagal berkali-kali)</p>' : ''}
            <p><strong>GPS Fix:</strong> ${data.gps_fix ? 'Ya (layak dipakai)' : 'Belum/kualitas kurang'}</p>
            <p><strong>Posisi terakhir:</strong> ${data.last_lat}, ${data.last_lng}</p>
            <p><strong>Mode Transmisi Saat Ini:</strong> ${modeBadge(data.mode)}</p>
            <p><strong>Kirim ke backend terakhir:</strong> ${data.last_send_ok ? 'Berhasil' : 'Gagal/belum pernah'}${data.last_send_ago_s >= 0 ? ' (' + formatDurasi(data.last_send_ago_s) + ')' : ''}</p>
            <p><strong>Sumber data (kirim terakhir):</strong> ${data.last_data_source}${data.last_send_was_fallback_posisi ? ' <em>(posisi TERAKHIR YANG DIKETAHUI, bukan fix live - dikirim darurat karena GPS tidak dapat sinyal saat tamper terjadi)</em>' : ''}</p>
            <p><strong>Titik tersimpan offline:</strong> ${data.buffered_points}${data.buffered_points > 0 ? ' (akan dikirim otomatis begitu koneksi pulih)' : ''}</p>
            <p><strong>Status Tamper:</strong> ${data.tamper_active ? '<span style="color:red;font-weight:bold;">⚠ SEDANG DIUSIK SEKARANG</span>' : (data.tamper_last_ago_s >= 0 ? 'Aman (terakhir diusik ' + formatDurasi(data.tamper_last_ago_s) + ')' : 'Aman (belum pernah terdeteksi sejak alat menyala)')}</p>
          `;
        } catch(e) { console.error(e); }
      }
      setInterval(updateStatus, 3000);
      window.onload = updateStatus;
    </script>
  </head>
  <body>
    <div class="nav">
      <button onclick="location.href='/'">Dashboard</button>
      <button onclick="location.href='/config'">WiFi & Alat Config</button>
    </div>
    <div class="card" id="status">
      <h2>Loading...</h2>
    </div>
    <div class="card">
      <h2>Penjelasan Mode Transmisi</h2>
      <p><strong>BERGERAK</strong> (kirim posisi tiap 30 detik) - kecepatan GPS terdeteksi di atas ambang dan bertahan konsisten, artinya kendaraan benar-benar sedang berjalan/dikendarai. Interval paling sering supaya jalur perjalanan tercatat rapat dan detail.</p>
      <p><strong>SLEEPY</strong> (kirim posisi tiap 5 menit) - kendaraan baru saja berhenti bergerak, tapi belum genap 15 menit diam terus-menerus. Mencakup situasi seperti berhenti di lampu merah, isi bensin, atau mampir sebentar - belum tentu benar-benar terparkir lama.</p>
      <p><strong>SLEEP</strong> (kirim posisi tiap 30 menit) - kendaraan sudah diam terus-menerus SELAMA 15 MENIT ATAU LEBIH, dianggap benar-benar terparkir/tidak dipakai. Interval paling jarang untuk menghemat kuota data seluler dan baterai alat.</p>
    </div>
  </body>
  </html>
  )";
  server.send(200, "text/html", html);
}

void handleConfig() {
  String html = "";
  html += "<!DOCTYPE html>\n<html>\n<head>\n";
  html += "  <title>Configuration</title>\n";
  html += "  <link rel=\"stylesheet\" href=\"/style.css\">\n";
  html += "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  html += "</head>\n<body>\n";
  html += "  <div class=\"nav\"><button onclick=\"location.href='/'\">&larr; Back</button></div>\n";
  html += "  <div class=\"card\">\n";
  html += "    <h2>WiFi & Identitas Alat</h2>\n";
  html += "    <form method=\"POST\" action=\"/save\">\n";
  html += "      <label>Nama Kapal/Alat (tampil di app):</label>\n";
  html += "      <input type=\"text\" name=\"device_name\" value=\"" + String(config.device_name) + "\" maxlength=\"63\" required>\n";
  html += "      <label>SSID WiFi (opsional - kosongkan kalau hanya pakai kartu SIM):</label>\n";
  html += "      <input type=\"text\" name=\"ssid\" value=\"" + String(config.ssid) + "\">\n";
  html += "      <label>Password WiFi (opsional):</label>\n";
  html += "      <input type=\"password\" name=\"password\" value=\"" + String(config.password) + "\">\n";
  html += "      <label>Hostname / AP SSID / Alamat mDNS (huruf besar &amp; spasi otomatis disesuaikan untuk http://...local):</label>\n";
  html += "      <input type=\"text\" name=\"hostname\" value=\"" + String(config.hostname) + "\">\n";
  html += "      <label>APN Kartu SIM (mis. indosatgprs, internet, 3gprs):</label>\n";
  html += "      <input type=\"text\" name=\"apn\" value=\"" + String(config.apn) + "\" required>\n";
  html += "      <button type=\"submit\">Save & Reboot</button>\n";
  html += "    </form>\n";
  html += "  </div>\n</body>\n</html>\n";
  server.send(200, "text/html", html);
}

void handleSave() {
  String ssid = server.arg("ssid");
  ssid.trim();  // autocomplete keyboard HP sering menambah spasi di akhir - lihat [WIFI KUAT]
  ssid.toCharArray(config.ssid, sizeof(config.ssid));
  server.arg("password").toCharArray(config.password, sizeof(config.password));
  server.arg("hostname").toCharArray(config.hostname, sizeof(config.hostname));
  config.wifi_timeout = server.arg("wifi_timeout").toInt();
  if (config.wifi_timeout < 5) config.wifi_timeout = 20;

  String apn = server.arg("apn");
  if (apn.length() > 0) {
    apn.toCharArray(config.apn, sizeof(config.apn));
  }

  // Backend mewajibkan name tidak kosong - jangan simpan nama kosong.
  String deviceName = server.arg("device_name");
  if (deviceName.length() > 0) {
    deviceName.toCharArray(config.device_name, sizeof(config.device_name));
  }

  config.configured = true;
  saveConfig();
  server.send(200, "text/plain", "Configuration saved. Rebooting...");
  delay(700);
  ESP.restart();
}

// Diisi dari loop() setiap pembacaan GPS, dibaca oleh handleStatus()
bool    statusGpsFix   = false;
double  statusLastLat  = 0, statusLastLng = 0;
// Posisi+waktu TERAKHIR YANG DIKETAHUI dari fix yang benar-benar valid (beda
// dari statusLastLat/Lng yang HANYA lat/lng tanpa referensi waktu kapan
// didapat) - dipakai fallback tamper darurat saat GPS lagi tidak dapat fix
// sama sekali, lihat "BUG NYATA #6" & kirimTamperPakaiPosisiTerakhir().
// 0 berarti belum pernah dapat fix valid sejak boot.
uint32_t      statusLastFixEpoch = 0;
unsigned long statusLastFixCapturedAtMillis = 0;
// true kalau pengiriman TERAKHIR pakai posisi cache (bukan fix live saat
// itu) - lihat kirimTamperPakaiPosisiTerakhir(). Dibaca handleStatus() biar
// dashboard portal transparan soal ini bukan posisi real-time.
bool          statusLastSendWasFallbackPosisi = false;
bool    statusSendOk   = false;
unsigned long statusSendMillis = 0;
// Sumber data (GSM/WIFI) yang berhasil dipakai untuk kirim TERAKHIR KALI -
// diisi di kirimKeBackend() persis saat salah satu jalur berhasil.
String  statusLastDataSource = "Belum pernah kirim";
// Mode transmisi TERKINI (BERGERAK/SLEEPY/SLEEP) - diisi loop() tiap ada fix
// GPS layak dipakai, dibaca handleStatus() untuk dashboard portal. Tetap
// berisi mode TERAKHIR yang diketahui selama GPS belum dapat fix baru lagi
// (bukan direset ke "Belum diketahui"), supaya dashboard tidak kosong terus
// kalau kebetulan dibuka pas GPS lagi kehilangan fix sebentar.
String  statusModeLabel = "Belum diketahui (menunggu fix GPS pertama)";
// millis() saat alarm tamper TERAKHIR KALI menyala (rising edge) - 0 berarti
// belum pernah terjadi sejak boot. Diisi tamperUpdate(), dibaca handleStatus()
// untuk dashboard portal lokal (lihat [MPU6050 TAMPER]).
unsigned long statusTamperLastMillis = 0;

void handleStatus() {
  JsonDocument doc;
  doc["ap_ssid"] = strlen(config.hostname) > 0 ? String(config.hostname) : "GPS-TRACKER";
  doc["ap_ip"] = WiFi.softAPIP().toString();
  doc["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  doc["wifi_ssid"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "";
  doc["wifi_ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
  doc["mdns_host"] = mdnsHostname;
  doc["device_name"] = String(config.device_name);
  doc["apn"] = String(config.apn);
  doc["gsm_registered"] = gsmRegistered;
  doc["quota_likely_exhausted"] = quotaLikelyExhausted;
  doc["gps_fix"] = statusGpsFix;
  doc["last_lat"] = statusLastLat;
  doc["last_lng"] = statusLastLng;
  doc["last_send_ok"] = statusSendOk;
  // PENTING: dua cabang ternary ini HARUS bertipe sama (long) - sebelumnya
  // cabang gagal ("-1") bertipe int sedangkan cabang sukses bertipe
  // unsigned long (dari millis()), jadi C++ otomatis mengubah -1 jadi
  // unsigned dulu sebelum dibandingkan -> hasilnya 4294967295 (2^32-1),
  // bukan -1 seperti yang dimaksud. Bug ini yang bikin dashboard sempat
  // menampilkan "4294967295s lalu" alih-alih "belum pernah kirim".
  doc["last_send_ago_s"] = statusSendMillis > 0 ? (long)((millis() - statusSendMillis) / 1000) : -1L;
  doc["last_data_source"] = statusLastDataSource;
  doc["mode"] = statusModeLabel;
  doc["buffered_points"] = bufferCount;
  doc["tamper_active"] = tamperAlarmAktif;
  // Pola sama seperti last_send_ago_s di atas - long di kedua cabang,
  // -1 berarti belum pernah terjadi sejak boot.
  doc["tamper_last_ago_s"] = statusTamperLastMillis > 0 ? (long)((millis() - statusTamperLastMillis) / 1000) : -1L;
  doc["last_send_was_fallback_posisi"] = statusLastSendWasFallbackPosisi;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleCSS() {
  String css = R"(
    body { font-family: 'Segoe UI', Arial, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; }
    .card { background: #f9f9f9; border-radius: 8px; padding: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
    input, button { padding: 10px; margin: 8px 0; width: 100%; box-sizing: border-box; }
    button { background: #4CAF50; color: white; border: none; cursor: pointer; border-radius: 4px; }
    button:hover { background: #45a049; }
    .nav { display: flex; gap: 10px; margin-bottom: 20px; }
    label { display: block; margin-top: 10px; font-weight: 600; }
  )";
  server.send(200, "text/css", css);
}

// Deteksi baterai vs aki per 2026-10-09 - ditrace dari skematik power
// circuit custom Rafi (bridge rectifier D1 -> buck U2 -> 5V ke ESP32, diode-OR
// D2/D3 di VIN+ buck antara adaptor vs baterai 2x18650). Rencana lama dari
// Kemal (monitor VBAT, lihat histori di git) TIDAK dipakai -- D2/D3 bikin
// VIN+ buck sama saja tegangannya dari sumber mana pun, jadi tidak ada sinyal
// yang bisa dibaca dari situ. Sense yang benar: node RAW+ (output rectifier
// D1, SEBELUM D3) -- node ini ~10V+ kalau adaptor terpasang (diblok D3 ke
// VIN+ makanya tidak kebawa switching noise buck), dan collapse ke ~0V kalau
// cuma baterai (D3 blok arus balik). Tidak overlap sama sekali dengan
// rentang tegangan baterai (7.4-8.4V) karena baterai tidak pernah "terlihat"
// dari node RAW+ ini.
//
// Hardware tambahan (voltage divider ke RAW+, SEBELUM D3):
//   R_top 100k (RAW+ -> pin ADC) + R_bottom 10k (pin ADC -> GND) + cap
//   filter 100nF paralel R_bottom. Rasio 11x. GPIO34 dipakai (ADC1, input-
//   only, tidak dipakai modul lain di firmware ini -- lihat pin map GNSS/MPU/
//   buzzer di atas) -- ADC2 SENGAJA dihindari karena tidak reliable saat
//   WiFi aktif (firmware ini pakai WiFi fallback).
// PERINGATAN: GPIO34 dikonfirmasi bebas dari SISI FIRMWARE saja -- belum
// dicek ke skematik fisik board T-Call A7670E V1.0, lakukan cek kontinuitas
// multimeter dulu sebelum solder (beberapa varian LilyGO mereservasi pin
// ADC1 tertentu untuk fungsi onboard).
#define PIN_VCC_SENSE 34

String bacaSumberDaya() {
  // Rata-rata beberapa sample buat redam ripple/noise dari rectifier.
  uint32_t sumMv = 0;
  const int N = 16;
  for (int i = 0; i < N; i++) {
    sumMv += analogReadMilliVolts(PIN_VCC_SENSE);  // API ter-kalibrasi, bukan analogRead() mentah
    delayMicroseconds(200);
  }
  float rawPlusV = (sumMv / (float)N / 1000.0f) * 11.0f;  // rasio divider 100k:10k

  // Threshold 5V: jauh di atas noise floor baterai-only (~0V), jauh di bawah
  // RAW+ minimum saat adaptor nyala (~10V buat adaptor 12V).
  return (rawPlusV > 5.0f) ? "AKI" : "BATTERY";
}

String bytesToHex(const uint8_t* data, size_t len) {
  static const char digits[] = "0123456789abcdef";
  String hex;
  hex.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    hex += digits[(data[i] >> 4) & 0x0F];
    hex += digits[data[i] & 0x0F];
  }
  return hex;
}

// Kemas lat/lng/heading/speed/timestamp/flags jadi 17 byte biner ringkas lalu
// hex-encode - lihat spek lengkap ("PENGHEMATAN BYTE") di komentar atas file.
String encodeTelemetryHex(double lat, double lng, double heading, double speedKnots,
                          uint32_t epochUtc, bool isWifi, bool quotaWarning, bool tamperAktif) {
  uint8_t buf[17];

  int32_t latScaled = (int32_t)lround(lat * 1000000.0);
  int32_t lngScaled = (int32_t)lround(lng * 1000000.0);
  buf[0] = (latScaled >> 24) & 0xFF;
  buf[1] = (latScaled >> 16) & 0xFF;
  buf[2] = (latScaled >> 8) & 0xFF;
  buf[3] = latScaled & 0xFF;
  buf[4] = (lngScaled >> 24) & 0xFF;
  buf[5] = (lngScaled >> 16) & 0xFF;
  buf[6] = (lngScaled >> 8) & 0xFF;
  buf[7] = lngScaled & 0xFF;

  // Clamp - jaga-jaga dari glitch GPS/heading di luar rentang wajar supaya
  // tidak wraparound diam-diam saat dipaksa masuk uint16.
  uint16_t headingScaled = (uint16_t)constrain((long)lround(heading), 0, 359);
  uint16_t speedScaled = (uint16_t)constrain((long)lround(speedKnots * 100.0), 0, 65535);
  buf[8] = (headingScaled >> 8) & 0xFF;
  buf[9] = headingScaled & 0xFF;
  buf[10] = (speedScaled >> 8) & 0xFF;
  buf[11] = speedScaled & 0xFF;

  buf[12] = (epochUtc >> 24) & 0xFF;
  buf[13] = (epochUtc >> 16) & 0xFF;
  buf[14] = (epochUtc >> 8) & 0xFF;
  buf[15] = epochUtc & 0xFF;

  uint8_t flags = 0;
  if (isWifi) flags |= 0x01;
  if (quotaWarning) flags |= 0x02;
  if (bacaSumberDaya() != "BATTERY") flags |= 0x04;
  if (tamperAktif) flags |= 0x08;
  if (tamperAktif && tamperUjiTandaiKirim) flags |= 0x10;  // tamper_test - lihat mulaiUjiTamper()
  buf[16] = flags;

  return bytesToHex(buf, sizeof(buf));
}

// ==========================================================
// [11] KIRIM KE BACKEND (POST /telemetry, gps.jejakpintar.cloud)
// ==========================================================
// Payload dibangun sekali per percobaan kirim, dipakai oleh JALUR MANA PUN
// (GSM atau WiFi) yang berhasil - keduanya bicara ke endpoint & skema field
// yang SAMA PERSIS, cuma beda transport-nya. `name` SENGAJA tidak dikirim
// lagi tiap kali - lihat "PENGHEMATAN BYTE" di komentar atas file.
String buatPayloadTelemetry(double lat, double lng, double speedKnots, double heading,
                            uint32_t epochUtc, bool isWifi, bool quotaWarning, bool tamperAktif) {
  JsonDocument doc;
  doc["sn"] = DEVICE_SN;
  doc["d"] = encodeTelemetryHex(lat, lng, heading, speedKnots, epochUtc, isWifi, quotaWarning, tamperAktif);
  String payload;
  serializeJson(doc, payload);
  return payload;
}

// Kirim lewat GSM - AT+HTTPS NATIVE bawaan modem (AT+HTTPINIT/HTTPPARA/
// HTTPACTION). Header X-API-Key sekarang pendek (~44 karakter) jadi muat
// jauh di bawah batas 256-600 karakter AT+HTTPPARA="USERDATA" - lihat
// penjelasan lengkap kenapa ini dipakai lagi (bukan socket TLS mentah) di
// komentar atas file.
#if GSM_TRANSPORT_MQTT
// Beda dari HTTPS (kirimKeBackendGSM di bawah) yang buka+tutup koneksi
// TIAP kirim, MQTT SENGAJA dijaga tersambung TERUS-MENERUS - itulah letak
// penghematan kuotanya (sekali handshake TLS, banyak publish kecil
// sesudahnya). mqttSudahMulai memastikan modem.mqtt_begin() (yang di
// dalamnya ada AT+CMQTTSTART, agak "berat") cuma dipanggil SEKALI seumur
// hidup sketch, bukan tiap kali mau kirim.
bool mqttSudahMulai = false;
bool mqttCmdSubscribed = false;
unsigned long mqttConnectOkMillis = 0;  // 0 = belum pernah connect sukses

// modem.mqtt_connected() meng-cache hasil AT+CMQTTDISC? selama 10 detik dan
// TIDAK di-update oleh mqtt_connect() - jadi tepat setelah connect sukses
// ia masih bisa melapor "putus" (cache lama), memicu connect ulang yang
// tidak perlu (dan bisa gagal karena client index masih terpakai). Sejak
// ada cek koneksi berkala [MQTT-CEK], connect lalu kirim telemetry dalam
// 10 detik jadi mungkin - anggap tersambung selama jendela cache itu.
bool mqttGsmTersambung() {
  if (mqttConnectOkMillis != 0 && millis() - mqttConnectOkMillis < 10000UL) return true;
  return modem.mqtt_connected();
}

// Pastikan sesi MQTT aktif & tersambung ke broker - dipanggil sebelum tiap
// publish. Kalau ternyata sempat putus (jaringan seluler drop dsb), coba
// sambung ulang di sini juga.
bool mqttPastikanTersambung() {
  // BUG NYATA (uji 2026-10-21): setelah sesi data seluler (PDP) putus atau
  // broker restart, layanan MQTT di modem A7670 jadi basi - TCP masih
  // terbuka ke broker, tapi TLS/CONNECT tidak pernah selesai, dan karena
  // mqtt_begin() cuma dipanggil sekali seumur boot, alat tidak bisa
  // tersambung lagi sampai reboot. Jadi kalau sesi yang sudah pernah jalan
  // ternyata hilang, restart penuh layanan MQTT modem (mqtt_begin():
  // CMQTTDISC/REL/STOP/START) sebelum connect ulang.
  if (mqttSudahMulai && !mqttGsmTersambung()) {
    Serial.println("[MQTT] Sesi hilang - restart layanan MQTT modem sebelum sambung ulang.");
    mqttCmdSubscribed = false;  // sesi lama hilang - subscription-nya ikut hilang
    // BUG NYATA (uji 2026-10-21, broker restart saat redeploy): mqtt_begin()
    // library GAGAL terus (<100 ms) - ia kirim AT+CMQTTSTOP, cuma menunggu
    // "OK" (padahal berhentinya baru dilaporkan belakangan lewat URC
    // "+CMQTTSTOP: 0"), lalu 20 ms kemudian AT+CMQTTSTART yang ditolak
    // karena layanan belum benar-benar berhenti. Restart versi sendiri yang
    // MENUNGGU URC itu - lihat mqttRestartLayananModem().
    if (!mqttRestartLayananModem()) return false;
  }
  if (!mqttSudahMulai) {
    if (!modem.mqtt_begin(MQTT_USE_TLS)) {
      // Race CMQTTSTOP/START yang sama (lihat mqttRestartLayananModem()) bisa
      // terjadi juga di sini kalau layanan MQTT modem masih jalan dari sebelum
      // ESP32 reboot. Buffer library sudah dialokasikan sebelum START, jadi
      // cukup ulangi STOP/START dengan benar.
      Serial.println("[MQTT] mqtt_begin() gagal - coba restart layanan MQTT modem dengan benar.");
      if (!mqttRestartLayananModem()) return false;
    }
    // Wajib di-set: pointer callback di TinyGsmMqttA76xx tidak diinisialisasi
    // library, padahal mqtt_handle() memanggilnya begitu ada pesan masuk.
    modem.mqtt_set_callback(mqttGsmCallbackPerintah);  // lihat [PERINTAH MQTT]
    mqttSudahMulai = true;
  }
  if (mqttGsmTersambung()) {
    if (!mqttCmdSubscribed) mqttSubscribePerintah();
    return true;
  }

  mqttCmdSubscribed = false;  // sesi lama hilang - subscription-nya ikut hilang
  Serial.printf("[MQTT] Menyambungkan ke broker %s:%u...\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  bool ok;
  if (strlen(MQTT_USERNAME) > 0) {
    ok = modem.mqtt_connect(0, MQTT_BROKER_HOST, MQTT_BROKER_PORT, DEVICE_SN, MQTT_USERNAME, MQTT_PASSWORD);
  } else {
    ok = modem.mqtt_connect(0, MQTT_BROKER_HOST, MQTT_BROKER_PORT, DEVICE_SN);
  }
  if (!ok) {
    Serial.println("[MQTT] Gagal connect ke broker.");
    return false;
  }
  mqttConnectOkMillis = millis();
  // mqtt_connect() library selalu clean session (CMQTTCONNECT ...,1) -
  // subscription hilang tiap reconnect, jadi WAJIB subscribe ulang di sini.
  mqttSubscribePerintah();
  return ok;
}

// Ambil sisa baris setelah prefix URC yang baru saja dicocokkan
// waitResponse() (mis. " 0" dari "+CMQTTSTART: 0"), -1 kalau tidak ada.
int mqttAmbilKodeUrc() {
  String sisa;
  if (modem.waitResponse(1000L, sisa, GF("\r\n")) != 1) return -1;
  sisa.trim();
  return sisa.length() > 0 ? sisa.toInt() : -1;
}

// Restart layanan MQTT modem dengan benar: putuskan, lepas client, STOP lalu
// TUNGGU "+CMQTTSTOP:" (URC asinkron), baru START dan tunggu "+CMQTTSTART:".
// Respons tiap langkah dicetak supaya kegagalan modem kelihatan di log.
// Buffer & callback library sudah disiapkan mqtt_begin() saat pertama kali,
// jadi tidak perlu diulang.
bool mqttRestartLayananModem() {
  String r;
  modem.sendAT("+CMQTTDISC=0,60");
  modem.waitResponse(5000L, r);
  r = "";
  modem.sendAT("+CMQTTREL=0");
  modem.waitResponse(3000L, r);

  r = "";
  modem.sendAT("+CMQTTSTOP");
  int8_t st = modem.waitResponse(10000L, r, GF("+CMQTTSTOP:"), GFP(GSM_ERROR));
  int kodeStop = (st == 1) ? mqttAmbilKodeUrc() : -1;
  Serial.printf("[MQTT] CMQTTSTOP -> %s (kode %d)\n",
                st == 1 ? "berhenti" : (st == 2 ? "ERROR (layanan memang sudah berhenti)" : "timeout"), kodeStop);
  delay(200);

  r = "";
  modem.sendAT("+CMQTTSTART");
  st = modem.waitResponse(30000L, r, GF("+CMQTTSTART:"), GFP(GSM_ERROR));
  int kodeStart = (st == 1) ? mqttAmbilKodeUrc() : -1;
  // kode 0 = sukses, 23 = layanan memang sudah jalan (masih bisa dipakai).
  bool ok = (st == 1) && (kodeStart == 0 || kodeStart == 23);
  Serial.printf("[MQTT] CMQTTSTART -> %s (kode %d)\n", ok ? "OK" : (st == 2 ? "ERROR" : (st == 1 ? "GAGAL" : "timeout")), kodeStart);
  return ok;
}

void mqttSubscribePerintah() {
  String topicCmd = String("cmd/") + DEVICE_SN;
  mqttCmdSubscribed = modem.mqtt_subscribe(0, topicCmd.c_str(), 1);
  Serial.printf("[MQTT] subscribe %s (QoS 1) -> %s\n", topicCmd.c_str(), mqttCmdSubscribed ? "OK" : "GAGAL");
  // Perintah retained dikirim broker TEPAT setelah SUBACK. Pemanggil kita
  // (kirimKeBackendMQTT) langsung lanjut publish, dan waitResponse() AT
  // command itu akan membuang URC +CMQTTRX... - jadi tampung dulu di sini
  // (callback cuma antrekan, diproses nanti di layaniPerintahMqtt()).
  if (mqttCmdSubscribed) {
    unsigned long mulai = millis();
    while (millis() - mulai < 1500UL) {
      if (SerialAT.available()) modem.mqtt_handle(100);
      else delay(10);
    }
  }
}

// Kirim lewat GSM - MQTT (Iterasi 3, lihat GSM_TRANSPORT_MQTT di atas untuk
// penjelasan kenapa & syarat sebelum flag ini boleh dinyalakan). Payload di
// sini SUDAH berupa hex mentah (bukan JSON) - lihat encodeTelemetryHex().
bool kirimKeBackendMQTT(const String& payloadHex) {
  if (!mqttPastikanTersambung()) {
    mqttGsmCatatHasil(false);  // jangan coba GSM lagi sebelum backoff habis - lihat [MQTT-CEK]
    return false;
  }

  String topic = String("telemetry/") + DEVICE_SN;
  tandaiGsmRfAktif();  // mqtt_publish() mengirim data lewat RF juga - lihat MPU_GSM_QUIET_MS
  bool ok = modem.mqtt_publish(0, topic.c_str(), payloadHex.c_str());
  modem.mqtt_handle();  // proses ACK/pesan masuk supaya sesi tetap sehat
  Serial.printf("[MQTT] publish %s -> %s\n", topic.c_str(), ok ? "OK" : "GAGAL");
  return ok;
}
#endif

bool kirimKeBackendGSM(const String& payload) {
  if (!modem.https_begin()) {
    Serial.println("[GSM] Gagal inisialisasi AT+HTTPS modem.");
    return false;
  }
  String url = String("https://") + BACKEND_HOST + BACKEND_PATH;
  if (!modem.https_set_url(url)) {
    Serial.println("[GSM] Gagal set URL.");
    modem.https_end();
    return false;
  }
  modem.https_set_content_type("application/json");
  if (!modem.https_add_header("X-API-Key", DEVICE_API_KEY)) {
    Serial.println("[GSM] Gagal set header X-API-Key.");
    modem.https_end();
    return false;
  }

  tandaiGsmRfAktif();  // https_post() mengirim data lewat RF - lihat MPU_GSM_QUIET_MS
  int code = modem.https_post(payload);
  String body = modem.https_body();
  modem.https_end();

  Serial.printf("[GSM] /telemetry -> %d\n", code);
  if (code != 200) Serial.println(body);
  return code == 200;
}

#if WIFI_TRANSPORT_MQTT
// Client TLS+MQTT KHUSUS WiFi - SENGAJA terpisah total dari client TLS yang
// dipakai kirimKeBackendWiFi() (HTTPS, dibuat baru tiap panggilan) karena
// PubSubClient/WiFiClientSecure di sini harus TETAP HIDUP di antara
// pengiriman (itulah inti penghematan MQTT - satu koneksi, banyak publish),
// jadi dideklarasikan sebagai variabel GLOBAL, bukan lokal per fungsi.
WiFiClientSecure mqttWifiTlsClient;
PubSubClient      mqttWifiClient(mqttWifiTlsClient);
bool              mqttWifiConfigured = false;
bool              mqttWifiCmdSubscribed = false;

bool mqttWifiPastikanTersambung() {
  if (!mqttWifiConfigured) {
    mqttWifiTlsClient.setInsecure();  // sertifikat broker self-signed - lihat catatan MQTT_BROKER_HOST di atas
    mqttWifiClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    mqttWifiClient.setCallback(mqttWifiCallbackPerintah);  // lihat [PERINTAH MQTT]
    mqttWifiConfigured = true;
  }
  if (mqttWifiClient.connected()) {
    if (!mqttWifiCmdSubscribed) mqttWifiSubscribePerintah();
    return true;
  }

  mqttWifiCmdSubscribed = false;  // sesi lama hilang - subscription-nya ikut hilang
  // Socket TLS lama bisa basi (WiFi sempat putus/broker restart) - tutup
  // bersih dulu supaya connect berikut benar-benar mulai dari awal.
  mqttWifiTlsClient.stop();
  Serial.printf("[WiFi-MQTT] Menyambungkan ke broker %s:%u...\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  bool ok;
  if (strlen(MQTT_USERNAME) > 0) {
    ok = mqttWifiClient.connect(DEVICE_SN, MQTT_USERNAME, MQTT_PASSWORD);
  } else {
    ok = mqttWifiClient.connect(DEVICE_SN);
  }
  if (!ok) {
    Serial.printf("[WiFi-MQTT] Gagal connect (state=%d).\n", mqttWifiClient.state());
    return false;
  }
  // Clean session - subscription hilang tiap reconnect, jadi WAJIB subscribe
  // ulang setiap kali connect (lihat [PERINTAH MQTT]).
  mqttWifiSubscribePerintah();
  return ok;
}

bool mqttWifiSesiHidup() {
  return mqttWifiConfigured && mqttWifiClient.connected();
}

void mqttWifiJagaKeepalive() {
  if (mqttWifiConfigured) mqttWifiClient.loop();
}

void mqttWifiSubscribePerintah() {
  String topicCmd = String("cmd/") + DEVICE_SN;
  mqttWifiCmdSubscribed = mqttWifiClient.subscribe(topicCmd.c_str(), 1);
  Serial.printf("[WiFi-MQTT] subscribe %s (QoS 1) -> %s\n", topicCmd.c_str(), mqttWifiCmdSubscribed ? "OK" : "GAGAL");
}

// Kirim lewat WiFi - MQTT (lihat WIFI_TRANSPORT_MQTT di atas untuk alasan
// ini diuji/dinyalakan lebih dulu di WiFi sebelum GSM). Payload di sini
// SUDAH berupa hex mentah (bukan JSON) - sama seperti kirimKeBackendMQTT()
// versi GSM, topic & format PERSIS sama supaya backend tidak perlu tahu/
// peduli device kirim lewat WiFi atau GSM.
bool kirimKeBackendWiFiMQTT(const String& payloadHex) {
  if (!mqttWifiPastikanTersambung()) return false;

  String topic = String("telemetry/") + DEVICE_SN;
  bool ok = mqttWifiClient.publish(topic.c_str(), payloadHex.c_str());
  mqttWifiClient.loop();  // proses keepalive/ACK supaya koneksi tetap sehat
  Serial.printf("[WiFi-MQTT] publish %s -> %s\n", topic.c_str(), ok ? "OK" : "GAGAL");
  return ok;
}
#endif

// ==========================================================
// [PERINTAH MQTT] Kanal perintah 2 arah dari server (2026-10-20)
// ==========================================================
// Device subscribe (QoS 1) ke cmd/<DEVICE_SN> di KEDUA jalur (GSM & WiFi),
// lalu balas ack ke cmd-ack/<DEVICE_SN>. Payload JSON teks (bukan hex):
//   {"id":12,"cmd":"interval","interval_s":30,"duration_min":60}
//     -> ack {"id":12,"status":"applied","interval_s":30,"duration_min":60}
//   {"id":13,"cmd":"reset"} -> ack {"id":13,"status":"reset"}
//   {"id":15,"cmd":"test_tamper"} -> ack {"id":15,"status":"applied"}, lalu
//     buzzer + kirim paksa SATU paket dengan bit3 tamper + bit4 tamper_test
//     (lihat mulaiUjiTamper()); ditolak kalau alarm tamper sedang menyala.
//   {"id":16,"cmd":"buzzer","duration_s":5} -> ack {"id":16,"status":"applied",
//     "duration_s":5}, buzzer bunyi terus selama duration_s (1-20) lalu mati
//     sendiri; BUKAN kejadian tamper (tidak kirim telemetry, tanpa bit tamper);
//     ditolak kalau alarm tamper sedang menyala; "reset" ikut mematikannya.
//   selain itu / JSON rusak  -> ack {"id":..,"status":"rejected","reason":".."}
// "interval" mengganti interval kirim di SEMUA mode (BERGERAK/SLEEPY/SLEEP)
// selama duration_min, lalu otomatis kembali normal. Logika mode (ambang
// 3.5 knot / 15 detik) TIDAK diubah - cuma nilai interval-nya yang ditimpa.
// Override HANYA di RAM: reboot = kembali normal. Kirim paksa tamper tetap
// jalan seperti biasa.
// Backend mengirim ulang perintah yang belum di-ack tiap ~2 menit, jadi
// perintah dideduplikasi per id: id yang sudah pernah diproses cukup di-ack
// ulang dengan status yang sama, timer TIDAK diulang.
//
// Callback MQTT (dipanggil dari dalam mqtt_handle()/PubSubClient.loop())
// SENGAJA cuma menyalin pesan ke slot antrean - parsing, penerapan, dan
// publish ack dilakukan di layaniPerintahMqtt() dari loop biasa, supaya
// tidak publish/kirim AT command dari dalam callback library.
#define CMD_INTERVAL_MIN_S     10
#define CMD_INTERVAL_MAX_S     1800
#define CMD_DURASI_MIN_MENIT   1
#define CMD_DURASI_MAX_MENIT   180
#define CMD_BUZZER_MIN_S       1
#define CMD_BUZZER_MAX_S       20   // sama dengan batas alarm tamper (TAMPER_ALARM_MAX_MS)
#define CMD_JALUR_GSM          0
#define CMD_JALUR_WIFI         1

bool          intervalOverrideAktif = false;
unsigned long intervalOverrideMs = 0;
unsigned long intervalOverrideMulaiMillis = 0;
unsigned long intervalOverrideDurasiMs = 0;
long          intervalOverrideCmdId = 0;

// Buzzer jarak jauh (perintah "buzzer") - non-blocking, dimatikan oleh
// layaniPerintahMqtt() begitu durasinya habis. Deteksi tamper TETAP jalan
// normal: kalau tamper sungguhan terjadi selama buzzer ini, buzzer baru
// benar-benar mati setelah KEDUANYA selesai.
bool          buzzerJarakJauhAktif = false;
unsigned long buzzerJarakJauhMulaiMillis = 0;
unsigned long buzzerJarakJauhDurasiMs = 0;
long          buzzerJarakJauhCmdId = 0;

void hentikanBuzzerJarakJauh(const char* sebab) {
  buzzerJarakJauhAktif = false;
  if (!tamperAlarmAktif) digitalWrite(BUZZER_PIN, LOW);  // jangan bungkam alarm tamper yang sedang menyala
  Serial.printf("STATUS: buzzer jarak jauh selesai (id=%ld, %s)\n", buzzerJarakJauhCmdId, sebab);
}

// Slot antrean pesan masuk (1 slot cukup - diproses tiap ~10 ms lewat
// delayLayaniPortal(); kalau penuh, pesan baru dibuang dan backend akan
// mengirimnya ulang sendiri).
char    cmdMasukBuf[257];
bool    cmdMasukAda = false;
uint8_t cmdMasukJalur = CMD_JALUR_GSM;

// Riwayat id yang sudah diproses + ack-nya, untuk dedupe & ack ulang.
#define CMD_RIWAYAT_N 8
long   cmdRiwayatId[CMD_RIWAYAT_N];
String cmdRiwayatAck[CMD_RIWAYAT_N];
int    cmdRiwayatJumlah = 0;
int    cmdRiwayatBerikut = 0;

void simpanPerintahMasuk(const char* topic, const uint8_t* payload, size_t len, uint8_t jalur) {
  String topicCmd = String("cmd/") + DEVICE_SN;
  if (topicCmd != topic) return;  // bukan topic perintah kita
  // Backend menghapus perintah retained dengan publish retained KOSONG -
  // bukan perintah, abaikan tanpa ack.
  bool kosong = true;
  for (size_t i = 0; i < len; i++) {
    if (!isspace(payload[i])) { kosong = false; break; }
  }
  if (kosong) {
    Serial.println("[CMD] retained dihapus (payload kosong), diabaikan");
    return;
  }
  if (cmdMasukAda) {
    Serial.println("[CMD] Slot antrean penuh - pesan baru dibuang (backend akan kirim ulang).");
    return;
  }
  if (len > sizeof(cmdMasukBuf) - 1) len = sizeof(cmdMasukBuf) - 1;
  memcpy(cmdMasukBuf, payload, len);
  cmdMasukBuf[len] = '\0';
  cmdMasukJalur = jalur;
  cmdMasukAda = true;
}

void mqttGsmCallbackPerintah(const char* topic, const uint8_t* payload, uint32_t len) {
  // Buffer library cuma 256 byte (topic+NUL+payload) dan len yang dilapor
  // bisa lebih besar dari yang benar-benar terbaca kalau payload kepotong.
  size_t muat = 256 - (strlen(topic) + 1);
  simpanPerintahMasuk(topic, payload, len > muat ? muat : len, CMD_JALUR_GSM);
}

void mqttWifiCallbackPerintah(char* topic, byte* payload, unsigned int len) {
  simpanPerintahMasuk(topic, payload, len, CMD_JALUR_WIFI);
}

void kirimAckPerintah(uint8_t jalur, const String& ackJson) {
  String topicAck = String("cmd-ack/") + DEVICE_SN;
  bool ok = false;
#if GSM_TRANSPORT_MQTT
  if (jalur == CMD_JALUR_GSM && mqttSudahMulai) {
    tandaiGsmRfAktif();  // publish lewat RF - lihat MPU_GSM_QUIET_MS
    ok = modem.mqtt_publish(0, topicAck.c_str(), ackJson.c_str());
  }
#endif
#if WIFI_TRANSPORT_MQTT
  if (jalur == CMD_JALUR_WIFI && mqttWifiClient.connected()) {
    ok = mqttWifiClient.publish(topicAck.c_str(), ackJson.c_str());
  }
#endif
  Serial.printf("[CMD] ack via %s -> %s %s : %s\n", jalur == CMD_JALUR_GSM ? "GSM" : "WiFi",
                topicAck.c_str(), ok ? "OK" : "GAGAL", ackJson.c_str());
}

void mulaiUjiTamper() {
  tamperAlarmAktif = true;
  tamperAlarmDariUji = true;
  tamperUjiTandaiKirim = true;
  tamperKirimSegera = true;  // jalur kirim paksa yang sama dengan tamper sungguhan
  statusTamperLastMillis = millis();
  tamperAlarmStartMillis = millis();
  tamperLastDisturbanceTime = millis();
  digitalWrite(BUZZER_PIN, HIGH);
  Serial.println("STATUS: UJI TAMPER dari server - buzzer menyala & posisi dikirim paksa (ditandai simulasi).");
}

void prosesPerintahMasuk(const char* json, uint8_t jalur) {
  Serial.printf("[CMD] Diterima via %s: %s\n", jalur == CMD_JALUR_GSM ? "GSM" : "WiFi", json);

  JsonDocument ack;
  String ackJson;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err || !doc.is<JsonObject>()) {
    ack["id"] = nullptr;
    ack["status"] = "rejected";
    ack["reason"] = err ? String("malformed JSON: ") + err.c_str() : String("malformed JSON: not an object");
    serializeJson(ack, ackJson);
    Serial.printf("[CMD] DITOLAK: %s\n", ack["reason"].as<const char*>());
    kirimAckPerintah(jalur, ackJson);
    return;
  }
  if (!doc["id"].is<long>()) {
    ack["id"] = nullptr;
    ack["status"] = "rejected";
    ack["reason"] = "missing or invalid id";
    serializeJson(ack, ackJson);
    Serial.println("[CMD] DITOLAK: missing or invalid id");
    kirimAckPerintah(jalur, ackJson);
    return;
  }
  long id = doc["id"].as<long>();

  for (int i = 0; i < cmdRiwayatJumlah; i++) {
    if (cmdRiwayatId[i] == id) {
      Serial.printf("[CMD] id=%ld duplikat - ack dikirim ulang, tidak diterapkan ulang.\n", id);
      kirimAckPerintah(jalur, cmdRiwayatAck[i]);
      return;
    }
  }

  const char* cmd = doc["cmd"] | "";
  ack["id"] = id;
  if (strcmp(cmd, "interval") == 0) {
    if (!doc["interval_s"].is<long>() || !doc["duration_min"].is<long>()) {
      ack["status"] = "rejected";
      ack["reason"] = "interval_s/duration_min missing or not integer";
    } else {
      long intervalS = doc["interval_s"].as<long>();
      long durasiMenit = doc["duration_min"].as<long>();
      if (intervalS < CMD_INTERVAL_MIN_S || intervalS > CMD_INTERVAL_MAX_S) {
        ack["status"] = "rejected";
        ack["reason"] = "interval_s out of range";
      } else if (durasiMenit < CMD_DURASI_MIN_MENIT || durasiMenit > CMD_DURASI_MAX_MENIT) {
        ack["status"] = "rejected";
        ack["reason"] = "duration_min out of range";
      } else {
        intervalOverrideAktif = true;
        intervalOverrideMs = (unsigned long)intervalS * 1000UL;
        intervalOverrideDurasiMs = (unsigned long)durasiMenit * 60000UL;
        intervalOverrideMulaiMillis = millis();
        intervalOverrideCmdId = id;
        ack["status"] = "applied";
        ack["interval_s"] = intervalS;
        ack["duration_min"] = durasiMenit;
        Serial.printf("[CMD] id=%ld DITERAPKAN: interval kirim %ld detik (semua mode) selama %ld menit.\n",
                      id, intervalS, durasiMenit);
      }
    }
  } else if (strcmp(cmd, "test_tamper") == 0) {
    if (tamperAlarmAktif) {
      ack["status"] = "rejected";
      ack["reason"] = "tamper alarm already active";
    } else {
      mulaiUjiTamper();
      ack["status"] = "applied";
      Serial.printf("[CMD] id=%ld DITERAPKAN: uji tamper (buzzer + kirim paksa dengan bit tamper_test).\n", id);
    }
  } else if (strcmp(cmd, "buzzer") == 0) {
    if (!doc["duration_s"].is<long>() ||
        doc["duration_s"].as<long>() < CMD_BUZZER_MIN_S || doc["duration_s"].as<long>() > CMD_BUZZER_MAX_S) {
      ack["status"] = "rejected";
      ack["reason"] = "duration_s out of range";
    } else if (tamperAlarmAktif) {
      ack["status"] = "rejected";
      ack["reason"] = "tamper alarm already active";
    } else {
      long durasiS = doc["duration_s"].as<long>();
      buzzerJarakJauhAktif = true;
      buzzerJarakJauhMulaiMillis = millis();
      buzzerJarakJauhDurasiMs = (unsigned long)durasiS * 1000UL;
      buzzerJarakJauhCmdId = id;
      digitalWrite(BUZZER_PIN, HIGH);
      ack["status"] = "applied";
      ack["duration_s"] = durasiS;
      Serial.printf("[CMD] id=%ld BUZZER %ld detik\n", id, durasiS);
    }
  } else if (strcmp(cmd, "reset") == 0) {
    if (buzzerJarakJauhAktif) hentikanBuzzerJarakJauh("dihentikan perintah reset");
    intervalOverrideAktif = false;
    ack["status"] = "reset";
    Serial.printf("[CMD] id=%ld RESET: override interval dihapus, kembali ke interval normal.\n", id);
  } else {
    ack["status"] = "rejected";
    ack["reason"] = "unknown cmd";
  }
  if (strcmp(ack["status"].as<const char*>(), "rejected") == 0) {
    Serial.printf("[CMD] id=%ld DITOLAK: %s\n", id, ack["reason"].as<const char*>());
  }

  serializeJson(ack, ackJson);
  cmdRiwayatId[cmdRiwayatBerikut] = id;
  cmdRiwayatAck[cmdRiwayatBerikut] = ackJson;
  cmdRiwayatBerikut = (cmdRiwayatBerikut + 1) % CMD_RIWAYAT_N;
  if (cmdRiwayatJumlah < CMD_RIWAYAT_N) cmdRiwayatJumlah++;
  kirimAckPerintah(jalur, ackJson);
}

// Dipanggil berulang dari delayLayaniPortal() (loop() menghabiskan sebagian
// besar waktunya di sana). Untuk GSM, pesan masuk datang sebagai URC
// "+CMQTTRXSTART..." di UART modem - URC ini HARUS dibaca mqtt_handle()
// sebelum AT command lain (GNSS dsb) sempat membacanya, karena
// waitResponse() TinyGSM membuang URC yang tidak dikenalnya. Cek
// SerialAT.available() dulu supaya mqtt_handle() (yang menunggu sampai
// 100 ms) tidak dipanggil sia-sia saat tidak ada data.
void layaniPerintahMqtt() {
#if GSM_TRANSPORT_MQTT
  if (mqttSudahMulai && SerialAT.available()) modem.mqtt_handle(100);
#endif
  if (cmdMasukAda) {
    prosesPerintahMasuk(cmdMasukBuf, cmdMasukJalur);
    cmdMasukAda = false;
  }
  if (buzzerJarakJauhAktif) {
    if (millis() - buzzerJarakJauhMulaiMillis >= buzzerJarakJauhDurasiMs) {
      hentikanBuzzerJarakJauh("durasi habis");
    } else if (!tamperAlarmAktif) {
      digitalWrite(BUZZER_PIN, HIGH);  // alarm tamper yang baru reda tadi mematikan pin - nyalakan lagi sampai durasi habis
    }
  }
  if (intervalOverrideAktif && millis() - intervalOverrideMulaiMillis >= intervalOverrideDurasiMs) {
    intervalOverrideAktif = false;
    Serial.printf("[CMD] Override interval id=%ld KEDALUWARSA (%lu menit) - kembali ke interval normal.\n",
                  intervalOverrideCmdId, intervalOverrideDurasiMs / 60000UL);
  }
}

// [MQTT-CEK] Cek koneksi MQTT berkala (2026-10-21) - tanpa ini sesi MQTT
// cuma dibuka/disambung ulang saat kirim telemetry (butuh fix GPS, dan di
// mode SLEEP baru 30 menit sekali), jadi perintah cmd/<SN> bisa telat lama.
// Cek ini TIDAK kirim telemetry dan TIDAK butuh fix GPS. Kalau sesi masih
// tersambung, tidak ada apa-apa yang dikirim (cuma keepalive bawaan). Kalau
// putus: sambung ulang + subscribe ulang (termasuk tampung retained 1.5
// detik - lihat mqttSubscribePerintah()).
// TRADE-OFF KUOTA: tiap sambung ulang GSM = handshake TLS + CONNECT +
// SUBSCRIBE (beberapa KB). Supaya sinyal jelek tidak menguras kuota, jeda
// antar-percobaan GAGAL naik bertahap 60 s -> 2 mnt -> 5 mnt -> maks 10 mnt,
// lalu kembali 60 s setelah sukses.
#define MQTT_CEK_KONEKSI_MS     60000UL
#define MQTT_CEK_BACKOFF_2_MS   120000UL
#define MQTT_CEK_BACKOFF_3_MS   300000UL
#define MQTT_CEK_BACKOFF_MAX_MS 600000UL

unsigned long mqttCekTerakhirMillis = 0;
// Backoff TERPISAH per jalur (uji 2026-10-21): modem bisa melapor "data
// seluler aktif" padahal antena dicabut/sinyal sangat jelek, lalu tiap
// connect MQTT GSM gagal setelah blocking ~30 detik. Dengan backoff
// bersama, jalur GSM yang "terdaftar tapi rusak" itu menutup sesi WiFi
// yang sehat dan membuat perintah tidak bisa masuk sama sekali. Sekarang
// GSM tetap diutamakan, tapi selama GSM gagal (dan sedang backoff), WiFi
// yang jadi jalur perintah - dan sesi WiFi baru ditutup SETELAH GSM
// benar-benar tersambung + subscribe.
unsigned long mqttGsmCobaTerakhirMillis = 0;
unsigned long mqttGsmJedaMs = 0;   // 0 = boleh langsung dicoba
unsigned long mqttWifiCobaTerakhirMillis = 0;
unsigned long mqttWifiJedaMs = 0;

unsigned long mqttBackoffBerikut(unsigned long jeda) {
  if (jeda < MQTT_CEK_KONEKSI_MS) return MQTT_CEK_KONEKSI_MS;
  if (jeda < MQTT_CEK_BACKOFF_2_MS) return MQTT_CEK_BACKOFF_2_MS;
  if (jeda < MQTT_CEK_BACKOFF_3_MS) return MQTT_CEK_BACKOFF_3_MS;
  return MQTT_CEK_BACKOFF_MAX_MS;
}

// Catat hasil percobaan sambung MQTT GSM (dari cek berkala MAUPUN dari
// jalur kirim telemetry) - sukses = boleh dicoba kapan saja lagi.
// BUG NYATA (uji 2026-10-21, 09:06-11:20): setelah sinyal seluler sempat
// putus-sambung, AT+NETOPEN? (dasar modem.isGprsConnected()) tetap
// melapor "terbuka" padahal jalur datanya sudah mati - restart layanan
// MQTT berhasil, tapi CONNECT selalu gagal ~11 detik kemudian, berjam-jam,
// dan pastikanGsmTersambung() tidak pernah membuka ulang sesi data karena
// mengira masih tersambung. Jadi kalau MQTT GSM gagal berturut-turut,
// PAKSA tutup sesi data (NETCLOSE) supaya pastikanGsmTersambung() membuka
// ulang dari awal (gprsConnect).
#define MQTT_GSM_GAGAL_PAKSA_DATA_ULANG  2
uint8_t mqttGsmGagalBeruntun = 0;

void mqttGsmCatatHasil(bool sukses) {
  mqttGsmCobaTerakhirMillis = millis();
  mqttGsmJedaMs = sukses ? 0 : mqttBackoffBerikut(mqttGsmJedaMs);
  if (sukses) {
    mqttGsmGagalBeruntun = 0;
    return;
  }
  if (++mqttGsmGagalBeruntun < MQTT_GSM_GAGAL_PAKSA_DATA_ULANG) return;
  mqttGsmGagalBeruntun = 0;
  Serial.printf("[GSM] MQTT gagal %d kali berturut-turut padahal sesi data dilaporkan aktif (sinyal CSQ %d) - sesi data ditutup paksa & dibuka ulang.\n",
                MQTT_GSM_GAGAL_PAKSA_DATA_ULANG, modem.getSignalQuality());
  tandaiGsmRfAktif();
  modem.gprsDisconnect();
  gsmRegistered = false;
  lastGsmAttemptMillis = millis() - GSM_RECONNECT_COOLDOWN_MS;  // biar pastikanGsmTersambung() langsung mencoba
}

// true = MQTT GSM baru saja gagal dan masih dalam masa backoff - jalur
// kirim telemetry sebaiknya langsung pakai WiFi (kalau ada) daripada
// blocking ~30 detik untuk percobaan GSM yang hampir pasti gagal lagi.
bool mqttGsmSedangBackoff() {
  return mqttGsmJedaMs > 0 && millis() - mqttGsmCobaTerakhirMillis < mqttGsmJedaMs;
}

// Dipanggil dari loop() di titik "aman" (tidak sedang ada kirim/AT command
// lain - loop() single-thread), tiap MQTT_CEK_KONEKSI_MS. Prioritas sama
// dengan kirimKeBackend(): GSM dulu kalau gsmRegistered, WiFi cadangan.
// GSM data/jaringan putus -> GSM dilewati, biar pastikanGsmTersambung()
// (dengan cooldown-nya sendiri) yang menangani.
void cekKoneksiMqttBerkala() {
  if (millis() - mqttCekTerakhirMillis < MQTT_CEK_KONEKSI_MS) return;
  mqttCekTerakhirMillis = millis();

  bool gsmSiap = false;
#if GSM_TRANSPORT_MQTT
  if (gsmRegistered) {
    if (mqttSudahMulai && mqttCmdSubscribed && mqttGsmTersambung()) {
      gsmSiap = true;
    } else if (!mqttGsmSedangBackoff()) {
      tandaiGsmRfAktif();  // sambung ulang transmit RF - lihat MPU_GSM_QUIET_MS
      bool ok = mqttPastikanTersambung();
      tandaiGsmRfAktif();
      gsmSiap = ok && mqttCmdSubscribed;
      mqttGsmCatatHasil(gsmSiap);
      Serial.printf("[MQTT-CEK] GSM terputus -> sambung ulang %s, subscribe cmd/%s %s, coba GSM lagi %s\n",
                    ok ? "OK" : "GAGAL", DEVICE_SN, mqttCmdSubscribed ? "OK" : "GAGAL",
                    gsmSiap ? "kapan saja" : (String(mqttGsmJedaMs / 1000UL) + " s lagi").c_str());
    }
  }
#endif

#if WIFI_TRANSPORT_MQTT
  if (gsmSiap) {
    // Client id SAMA (DEVICE_SN) di kedua jalur - broker sudah memindahkan
    // sesi ke GSM; tutup juga sisi WiFi supaya tidak saling rebut.
    if (mqttWifiConfigured && mqttWifiClient.connected()) {
      Serial.println("[MQTT-CEK] GSM tersambung - sesi MQTT WiFi ditutup (client id sama, GSM jalur utama).");
      mqttWifiClient.disconnect();
    }
    return;
  }
  // GSM tidak tersedia / gagal / sedang backoff -> WiFi jadi jalur perintah.
  if (strlen(config.ssid) == 0 || WiFi.status() != WL_CONNECTED) return;
  if (mqttWifiConfigured && mqttWifiCmdSubscribed && mqttWifiClient.connected()) return;
  if (mqttWifiJedaMs > 0 && millis() - mqttWifiCobaTerakhirMillis < mqttWifiJedaMs) return;
  bool ok = mqttWifiPastikanTersambung();
  bool wifiSiap = ok && mqttWifiCmdSubscribed;
  mqttWifiCobaTerakhirMillis = millis();
  mqttWifiJedaMs = wifiSiap ? 0 : mqttBackoffBerikut(mqttWifiJedaMs);
  Serial.printf("[MQTT-CEK] WiFi terputus -> sambung ulang %s, subscribe cmd/%s %s%s\n",
                ok ? "OK" : "GAGAL", DEVICE_SN, mqttWifiCmdSubscribed ? "OK" : "GAGAL",
                wifiSiap ? "" : (String(", coba WiFi lagi ") + String(mqttWifiJedaMs / 1000UL) + " s lagi").c_str());
#endif
}

// Kirim lewat WiFi - endpoint & auth SAMA PERSIS dengan jalur GSM di atas.
// TIDAK lagi sign-in Firebase Auth/tulis ke Firestore terpisah seperti
// sebelumnya - itu bug nyata: data device kepecah ke DUA database berbeda
// tergantung jalur mana yang kebetulan berhasil. Sekarang GSM dan WiFi
// selalu menulis ke database yang SAMA (Postgres di backend FastAPI).
bool kirimKeBackendWiFi(const String& payload) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String("https://") + BACKEND_HOST + BACKEND_PATH;
  if (!http.begin(client, url)) {
    Serial.println("[WiFi] Gagal membuka koneksi ke backend.");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", DEVICE_API_KEY);
  int code = http.POST(payload);
  Serial.printf("[WiFi] /telemetry -> %d\n", code);
  if (code != 200) Serial.println(http.getString());
  http.end();
  return code == 200;
}

// Titik masuk tunggal untuk kirim satu titik (live ATAU susulan dari buffer -
// tidak ada bedanya lagi buat backend ini, lihat catatan "historyOnly" di
// komentar atas file). Coba GSM dulu (kalau tersambung), baru WiFi cadangan.
// tamperAktif: HANYA true untuk titik LIVE saat tamperAlarmAktif sedang
// menyala (lihat kirimPosisiKeBackend()) - kirimSatuRiwayatPosisi() SELALU
// mengirim false, supaya titik lama dari buffer offline tidak salah
// ditandai "diusik" gara-gara ada tamper BARU yang kebetulan aktif persis
// saat buffer itu baru sempat disusulkan.
bool kirimKeBackend(double lat, double lng, double speedKnots, double heading,
                     uint32_t epochUtc, bool tamperAktif) {
  server.handleClient();  // layani portal sebelum blocking HTTPS di bawah

#if GSM_TRANSPORT_MQTT
  // GSM "terdaftar" tapi MQTT-nya baru saja gagal (mis. antena dicabut) -
  // langsung WiFi kalau tersedia, jangan blocking ~30 detik lagi di GSM.
  bool lewatiGsm = mqttGsmSedangBackoff() && strlen(config.ssid) > 0 && WiFi.status() == WL_CONNECTED;
  if (lewatiGsm) Serial.println("[Kirim] MQTT GSM sedang backoff setelah gagal - langsung lewat WiFi.");
  // Uji test_tamper 2026-10-21: paket tamper tertunda 12 detik karena jalur
  // kirim mencoba menyambung ulang MQTT GSM yang mati dulu, padahal sesi
  // MQTT WiFi sedang hidup. Kalau sesi GSM sedang tidak tersambung tapi sesi
  // WiFi tersambung, pakai WiFi sekarang - [MQTT-CEK] yang memulihkan GSM.
  if (!lewatiGsm && gsmRegistered && mqttWifiSesiHidup() && !(mqttSudahMulai && mqttGsmTersambung())) {
    lewatiGsm = true;
    Serial.println("[Kirim] Sesi MQTT GSM sedang putus tapi sesi WiFi hidup - langsung lewat WiFi.");
  }
#else
  bool lewatiGsm = false;
#endif
  if (gsmRegistered && !lewatiGsm) {
#if GSM_TRANSPORT_MQTT
    String payloadHex = encodeTelemetryHex(lat, lng, heading, speedKnots, epochUtc,
                                            /*isWifi=*/false, /*quotaWarning=*/false, tamperAktif);
    if (kirimKeBackendMQTT(payloadHex)) {
      statusLastDataSource = "GSM (MQTT)";
      return true;
    }
#else
    String payloadGsm = buatPayloadTelemetry(lat, lng, speedKnots, heading, epochUtc,
                                              /*isWifi=*/false, /*quotaWarning=*/false, tamperAktif);
    if (kirimKeBackendGSM(payloadGsm)) {
      statusLastDataSource = "GSM";
      return true;
    }
#endif
  }
  if (wifiSiapDipakaiCadangan()) {
    // Kalau GSM baru saja gagal berkali-kali dengan pola "jaringan terdaftar
    // tapi sesi data gagal" (lihat gsmConnect()/quotaLikelyExhausted),
    // kemungkinan besar kuota/pulsa data SIM habis - dititipkan lewat bit
    // quota_warning di field `d` (lihat "PENGHEMATAN BYTE" di komentar atas
    // file) supaya kelihatan di app, karena backend tidak punya field
    // notifikasi terpisah untuk ini.
#if WIFI_TRANSPORT_MQTT
    String payloadHexWifi = encodeTelemetryHex(lat, lng, heading, speedKnots, epochUtc,
                                                /*isWifi=*/true, quotaLikelyExhausted, tamperAktif);
    if (kirimKeBackendWiFiMQTT(payloadHexWifi)) {
      statusLastDataSource = quotaLikelyExhausted ? "WIFI-MQTT (GSM: kuota diduga habis)" : "WIFI-MQTT";
      return true;
    }
#else
    String payloadWifi = buatPayloadTelemetry(lat, lng, speedKnots, heading, epochUtc,
                                              /*isWifi=*/true, quotaLikelyExhausted, tamperAktif);
    if (kirimKeBackendWiFi(payloadWifi)) {
      statusLastDataSource = quotaLikelyExhausted ? "WIFI (GSM: kuota diduga habis)" : "WIFI";
      return true;
    }
#endif
  }
  return false;
}

// BUG NYATA #6 (ditemukan lewat pengujian nyata 2026-09-22, log Serial):
// tamperKirimSegera + fallback fix-kualitas-rendah (BUG NYATA #5) TETAP
// tidak cukup kalau GPS TIDAK PUNYA FIX SAMA SEKALI ("[GPS] Belum ada fix,
// mencari satelit..." terus-menerus) - alarm tamper terbukti menyala bersih
// berkali-kali di log (buzzer, deviasi/rotasi semua benar), TAPI karena
// seluruh blok pengiriman (termasuk fallback BUG NYATA #5) ada di DALAM
// cabang "adaFix" di loop(), tidak ada satu pun yang sempat jalan selama
// tidak ada fix - tamperKirimSegera menunggu selamanya. Situasi ini justru
// SANGAT MUNGKIN terjadi pas alat sungguhan diusik (dibawa masuk
// ruangan/garasi/ditutup sesuatu, GPS kehilangan pandangan langit).
// Solusi: kalau tidak ada fix (atau kualitasnya ditolak) TAPI ada tamper
// yang menunggu DAN alat PERNAH dapat fix valid sejak boot
// (statusLastFixEpoch>0), kirim pakai posisi+waktu TERAKHIR YANG DIKETAHUI
// (bukan 0,0/sampah) - posisi kira-kira jauh lebih berguna untuk peringatan
// keamanan daripada tidak terkirim sama sekali. Kalau alat BELUM PERNAH
// dapat fix sejak boot sama sekali, tidak ada posisi apa pun untuk dikirim
// (batasan fisik, bukan bug) - tetap menunggu sampai fix pertama datang.
void kirimTamperPakaiPosisiTerakhir() {
  tamperKirimSegera = false;
  unsigned long umurDetik = (millis() - statusLastFixCapturedAtMillis) / 1000;
  uint32_t epochPerkiraan = statusLastFixEpoch + (uint32_t)umurDetik;
  Serial.printf("[TAMPER] Tidak ada fix GPS layak saat ini - kirim pakai posisi TERAKHIR YANG DIKETAHUI (Lat=%.6f Lng=%.6f, ~%lus lalu) supaya peringatan tetap sampai ke backend.\n",
                statusLastLat, statusLastLng, umurDetik);
  bool ok = kirimKeBackend(statusLastLat, statusLastLng, 0.0, 0.0, epochPerkiraan, /*tamperAktif=*/true);
  tamperUjiTandaiKirim = false;  // tanda uji cuma untuk satu paket kirim-paksa ini
  if (ok) {
    statusSendOk = true;
    statusSendMillis = millis();
    statusLastSendWasFallbackPosisi = true;
    Serial.println("[TAMPER] Berhasil terkirim pakai posisi terakhir.");
  } else {
    Serial.println("[TAMPER] GAGAL kirim pakai posisi terakhir juga (tidak ada GSM maupun WiFi saat ini) - kejadian ini TIDAK otomatis dicoba ulang, hanya tercatat lokal di buzzer/log Serial.");
  }
}

// tamperAktif SENGAJA diterima sebagai parameter dari pemanggil, BUKAN baca
// langsung variabel global tamperAlarmAktif di sini - lihat "BUG NYATA #4"
// di komentar loop() untuk alasannya (race condition: baca global di titik
// ini bisa saja sudah keburu balik false lagi walau tamperKirimSegera
// dipicu gara-gara alarm SEMPAT menyala).
bool kirimPosisiKeBackend(const GpsData &d, bool tamperAktif) {
  return kirimKeBackend(d.latitude, d.longitude, d.speedKnots,
                        d.courseValid ? d.course : 0.0, utcEpochFromGps(d),
                        tamperAktif);
}

// Kirim SATU titik riwayat yang sempat tersimpan di buffer saat offline.
// tamperAktif SELALU false di sini - lihat catatan di kirimKeBackend().
bool kirimSatuRiwayatPosisi(const BufferedPoint &p) {
  return kirimKeBackend(p.lat, p.lng, p.speedKnots, p.heading, p.epochUtc, /*tamperAktif=*/false);
}

// Simpan satu titik ke buffer RAM (dipanggil kalau pengiriman langsung gagal
// terus setelah KIRIM_MAX_RETRY kali percobaan). Buffer berhenti menerima
// titik baru kalau sudah penuh - data GPS tetap dibaca terus, cuma tidak ada
// tempat lagi untuk menyimpannya sampai buffer dikosongkan lewat flush.
void simpanKeBuffer(const GpsData &d) {
  if (bufferCount >= MAX_BUFFERED_POINTS) {
    Serial.println("[Buffer] Penuh! Titik ini tidak tersimpan (buffer sudah maksimal).");
    return;
  }
  BufferedPoint &p = pointBuffer[bufferCount];
  p.lat = d.latitude;
  p.lng = d.longitude;
  p.speedKnots = d.speedKnots;
  p.heading = d.courseValid ? d.course : 0.0f;
  p.epochUtc = utcEpochFromGps(d);
  bufferCount++;
  Serial.printf("[Buffer] Titik disimpan offline (%d/%d tersimpan).\n", bufferCount, MAX_BUFFERED_POINTS);
}

// Kirim semua titik yang sempat tersimpan saat offline, dipanggil begitu ada
// pengiriman yang berhasil lagi (tandanya koneksi sudah pulih).
void flushBufferedPoints() {
  if (bufferCount == 0) return;
  Serial.printf("[Buffer] Mengirim %d titik yang tersimpan saat offline...\n", bufferCount);

  int terkirim = 0;
  for (int i = 0; i < bufferCount; i++) {
    server.handleClient();  // tetap layani portal di antara kiriman susulan
    if (kirimSatuRiwayatPosisi(pointBuffer[i])) {
      terkirim++;
    } else {
      Serial.printf("[Buffer] Gagal kirim titik ke-%d, dihentikan - sisanya dicoba lagi nanti.\n", i + 1);
      // Geser sisa titik yang belum terkirim ke depan buffer, buang yang sudah sukses.
      int sisa = bufferCount - i;
      memmove(&pointBuffer[0], &pointBuffer[i], sisa * sizeof(BufferedPoint));
      bufferCount = sisa;
      Serial.printf("[Buffer] %d titik terkirim, %d titik masih tersisa di buffer.\n", terkirim, bufferCount);
      return;
    }
  }
  Serial.printf("[Buffer] Semua %d titik berhasil terkirim.\n", terkirim);
  bufferCount = 0;
}

// Coba kirim posisi terkini, kalau gagal coba sambungkan ulang SIM
// card/WiFi lalu kirim ulang, sampai KIRIM_MAX_RETRY kali. Kalau tetap gagal
// semua, titik ini disimpan ke buffer (lihat simpanKeBuffer) supaya tidak
// hilang, dan akan ikut terkirim otomatis begitu koneksi pulih.
void cobaKirimDenganRetry(const GpsData &d, bool tamperAktif) {
  // Susulkan dulu titik-titik lama dari buffer offline (urut dari yang
  // paling lama) SEBELUM kirim titik live sekarang. PENTING untuk backend
  // gps.jejakpintar.cloud: tidak ada konsep historyOnly di server - setiap
  // POST /telemetry SELALU menimpa posisi "terkini" di tabel devices, jadi
  // titik live WAJIB jadi POST TERAKHIR supaya tidak ketimpa balik oleh
  // titik lama yang disusulkan sesudahnya.
  flushBufferedPoints();

  for (int percobaan = 1; percobaan <= KIRIM_MAX_RETRY; percobaan++) {
    if (kirimPosisiKeBackend(d, tamperAktif)) {
      statusSendOk = true;
      statusLastSendWasFallbackPosisi = false;  // ini kiriman NORMAL pakai fix live, bukan fallback
      return;
    }
    Serial.printf("[Kirim] Percobaan ke-%d/%d gagal.\n", percobaan, KIRIM_MAX_RETRY);
    if (percobaan < KIRIM_MAX_RETRY) {
      Serial.println("Mencoba sambungkan ulang SIM card / WiFi sebelum coba lagi...");
      gsmRegistered = gsmConnect();
      pastikanWiFiTersambung();
      delay(2000);
    }
  }
  Serial.println("[Kirim] Tetap gagal setelah beberapa percobaan - titik ini disimpan dulu di ESP32.");
  statusSendOk = false;
  simpanKeBuffer(d);
}

// ==========================================================
// [12] SETUP
// ==========================================================
// Nama manusiawi untuk alasan reset terakhir ESP32 - dicetak di awal
// setup() untuk investigasi "BUG NYATA #8" (mode SLEEP 30 menit tidak
// pernah tercapai di data lapangan 2026-09-26 - dugaan board reboot diam-
// diam tiap ~5 menit, mereset diamSejakMillis, TANPA sempat tercatat
// sebagai baris "bergerak" di riwayat). Kalau memang benar reboot
// berulang, alasannya (brownout/watchdog/dst) akan kelihatan di sini tiap
// kali boot - baris ini HARUS muncul di log Serial setiap kali alat
// "mulai dari awal" (AP + MPU6050 init dsb tercetak lagi) supaya kelihatan
// jelas apakah itu power-on normal (sekali di awal) atau berulang terus.
const char* alasanResetTerakhir() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "POWER-ON (baru dinyalakan/dicolok listrik)";
    case ESP_RST_SW:        return "SOFTWARE RESET (ESP.restart() dipanggil - lihat handleSave())";
    case ESP_RST_PANIC:     return "PANIC/CRASH (exception - lihat detail di atas baris ini kalau ada)";
    case ESP_RST_INT_WDT:   return "INTERRUPT WATCHDOG TIMEOUT";
    case ESP_RST_TASK_WDT:  return "TASK WATCHDOG TIMEOUT (ada kode yang blocking terlalu lama)";
    case ESP_RST_WDT:       return "WATCHDOG TIMEOUT (lainnya)";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (tegangan sempat drop di bawah ambang aman - KEMUNGKINAN BESAR terkait lonjakan arus modem A7670E saat transmit, lihat MPU_GSM_QUIET_MS)";
    case ESP_RST_DEEPSLEEP: return "BANGUN DARI DEEP SLEEP (alat ini tidak pakai deep sleep - kalau muncul ini aneh)";
    default:                return "LAINNYA (jarang terjadi, cek esp_reset_reason() manual)";
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== GNSS + GSM (SIM Card) -> Backend FastAPI ===");
  Serial.printf("[BOOT] Alasan reset terakhir: %s\n", alasanResetTerakhir());

  // ---- Muat konfigurasi dari EEPROM ----
  EEPROM.begin(EEPROM_SIZE);
  if (!loadConfig()) {
    Serial.println("Konfigurasi belum ada / tidak valid. Memakai default.");
  }

  // ---- Nyalakan modem A7670E (GNSS + data seluler) ----
  SerialAT.begin(UART_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  delay(500);

  Serial.println("Menyalakan modem A7670E...");
  modemPowerOn();

  Serial.print("Menunggu respon modem");
  int retry = 0;
  while (!modem.testAT(1000)) {
    Serial.print(".");
    if (retry++ > 30) {
      digitalWrite(BOARD_PWRKEY_PIN, LOW);
      delay(100);
      digitalWrite(BOARD_PWRKEY_PIN, HIGH);
      delay(MODEM_POWERON_PULSE_WIDTH_MS);
      digitalWrite(BOARD_PWRKEY_PIN, LOW);
      retry = 0;
    }
  }
  Serial.println("\nModem merespon!");

  Serial.println("Mengaktifkan GNSS (multi-konstelasi)...");
  if (modem.enableGPS()) {
    modem.setGPSMode(4);  // GPS+BDS+GALILEO+SBAS+QZSS -> HDOP lebih baik
    Serial.println("GNSS aktif. Menunggu fix satelit (area terbuka)...");
  } else {
    Serial.println("Gagal mengaktifkan GNSS!");
  }

  mpuSetup();  // MPU6050 - deteksi gerak kendaraan untuk mode transmisi adaptif

  // ---- 1. WiFi: Access Point SELALU aktif (portal config bisa dibuka kapan
  // saja lewat SSID/IP yang tetap ini - lihat "AKSES PORTAL CONFIG PERMANEN"
  // di komentar atas file), DAN kalau WiFi lokal sudah dikonfigurasi, coba
  // sambung juga sebagai STA supaya portal JUGA bisa diakses lewat WiFi
  // rumah/kantor (via mDNS) tanpa perlu pindah SSID. Dilakukan SEBELUM GSM
  // (kebalikan urutan sebelumnya) supaya portal langsung bisa diakses sejak
  // awal boot, tidak ikut menunggu proses GSM di bawah yang bisa makan waktu.
  // GSM tetap JALUR UTAMA untuk kirim data (lihat kirimKeBackend()) - urutan
  // setup ini tidak mengubah prioritas itu, cuma urutan inisialisasi.
  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_AP_STA);
  setupAP();
  if (strlen(config.ssid) > 0) {
    Serial.println("Mencoba sambungkan WiFi lokal (untuk akses portal & cadangan kirim data)...");
    connectWiFi();  // non-blocking - hasilnya dilaporkan async lewat onWifiEvent()
  } else {
    Serial.println("WiFi lokal belum dikonfigurasi - portal cuma bisa diakses lewat Access Point di atas untuk sekarang.");
  }
  startMDNS();

  // ---- 2. Registrasi ke jaringan seluler (kartu SIM) - jalur kirim data UTAMA ----
  gsmRegistered = gsmConnect();
  // Dicatat di sini juga (bukan cuma di pastikanGsmTersambung()) supaya
  // cooldown GSM_RECONNECT_COOLDOWN_MS langsung berlaku sejak iterasi
  // loop() PERTAMA - tanpa ini, percobaan gsmConnect() kedua akan langsung
  // menyusul tanpa jeda begitu loop() mulai jalan (lastGsmAttemptMillis
  // masih 0 di titik ini), sebelum akhirnya ikut pola cooldown yang benar
  // di percobaan-percobaan berikutnya.
  lastGsmAttemptMillis = millis();
  if (!gsmRegistered) {
    Serial.println("Belum berhasil registrasi ke jaringan seluler. Akan dicoba lagi otomatis di loop().");
  }

  // ---- 3. Portal web konfigurasi (aman sekarang, WiFi.mode() sudah pasti dipanggil di atas) ----
  server.on("/", HTTP_GET, handleRoot);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/style.css", HTTP_GET, handleCSS);
  server.begin();
  Serial.println("HTTP server started");
  cetakInfoAksesPortal();
}

// ==========================================================
// [13] LOOP
// ==========================================================
static unsigned long lastSendTime = 0;
// 0 berarti "sedang bergerak" (belum mulai diam). Selain 0 = waktu (millis())
// saat kendaraan PERTAMA KALI terdeteksi berhenti - dipakai menghitung sudah
// berapa lama diam untuk keputusan SLEEPY vs SLEEP.
static unsigned long diamSejakMillis = 0;
// 0 berarti kecepatan GPS sedang di bawah ambang. Selain 0 = waktu (millis())
// saat kecepatan GPS PERTAMA KALI terbaca di atas ambang secara
// berkelanjutan - dipakai konfirmasi GPS_MOVING_CONFIRM_MS (lihat komentar
// TEMUAN LAPANGAN di definisi GPS_MOVING_CONFIRM_MS di atas).
static unsigned long gpsGerakSejakMillis = 0;

// Info cara akses portal config (+ status kuota) - lihat "AKSES PORTAL
// CONFIG PERMANEN" & "DETEKSI KUOTA SIM HABIS" di komentar atas file.
void cetakInfoAksesPortal() {
  Serial.println("\n=== AKSES PORTAL CONFIG ===");
  String apSsid = strlen(config.hostname) > 0 ? String(config.hostname) : "GPS-TRACKER";
  Serial.printf("Access Point (permanen, selalu aktif): SSID \"%s\", password \"%s\", IP %s\n",
                apSsid.c_str(), AP_PASSWORD, WiFi.softAPIP().toString().c_str());
  if (WiFi.status() == WL_CONNECTED) {
    String staLine = "WiFi lokal: tersambung ke \"" + WiFi.SSID() + "\", IP " + WiFi.localIP().toString();
    if (mdnsHostname.length() > 0) staLine += " (atau http://" + mdnsHostname + ".local)";
    Serial.println(staLine);
  } else if (strlen(config.ssid) > 0) {
    Serial.println("WiFi lokal: belum tersambung (portal tetap bisa lewat Access Point di atas).");
  }
  if (quotaLikelyExhausted) {
    Serial.println("[PERINGATAN] GSM terdaftar di jaringan tapi sesi data gagal berkali-kali - kemungkinan besar KUOTA/PULSA DATA HABIS. Cek isi ulang paket data operator.");
  }
  Serial.println("===========================\n");
}

void loop() {
  server.handleClient();
  pastikanGsmTersambung();   // SIM card - prioritas utama untuk kirim data
  pastikanWiFiTersambung();  // WiFi lokal - selalu dijaga untuk akses portal (lihat komentar fungsi), TERLEPAS status GSM

#if WIFI_TRANSPORT_MQTT
  // TEMUAN LAPANGAN (2026-09-25): tanpa ini, koneksi MQTT WiFi selalu
  // "Menyambungkan ke broker..." ULANG tiap kirim, walau jaraknya cuma
  // puluhan detik (mode BERGERAK) - padahal inti hemat kuota MQTT justru
  // "sekali connect, banyak publish". Sebabnya: PubSubClient.loop() (WAJIB
  // dipanggil BERKALA untuk proses keepalive PINGREQ) sebelumnya cuma
  // dipanggil sesaat SETELAH publish, tidak pernah di antaranya - broker
  // (atau NAT/firewall di tengah jalan) menganggap koneksi mati karena
  // tidak ada keepalive, lalu memutusnya sebelum kiriman berikutnya sempat
  // pakai koneksi yang sama. Panggil di sini (tiap iterasi loop(), murah/
  // no-op kalau belum connect) supaya koneksi antar-kiriman benar-benar
  // terjaga hidup selama gap-nya masih dalam batas keepalive.
  if (mqttWifiConfigured) mqttWifiClient.loop();
#endif
  cekKoneksiMqttBerkala();  // perintah cmd/<SN> tanpa nunggu kirim telemetry - lihat [MQTT-CEK]

  if (millis() - lastStatusReminderMillis >= STATUS_REMINDER_INTERVAL_MS) {
    lastStatusReminderMillis = millis();
    cetakInfoAksesPortal();
  }

  // Dibaca TIAP iterasi, TERLEPAS status fix GPS - supaya baseline MPU
  // (gerak & tamper) tetap "hangat", alarm tamper tetap responsif, dan bisa
  // dikalibrasi (lihat cetakDiagnostikMpu()) walau alat sedang di
  // garasi/dalam ruangan tanpa sinyal GPS sama sekali.
  mpuUpdate();
  bool mpuAktifSekarang = mpuSedangGerak;
  cetakDiagnostikMpu(mpuAktifSekarang);
  tamperUpdate();  // deteksi alat diusik/diangkat + buzzer - lihat bagian [MPU6050 TAMPER]

  GpsData d;
  bool adaFix = bacaGNSS(d);
  server.handleClient();  // layani portal segera setelah AT command GPS selesai

  // BUG NYATA #5 (ditemukan lewat pengujian nyata 2026-09-21, log Serial):
  // tamperKirimSegera betul konsisten membawa nilai tamper yang benar (lihat
  // "BUG NYATA #4"), TAPI pengiriman itu sendiri baru bisa jalan kalau
  // gerbang `fixLayakDipakai()` di bawah ini lolos - dalam satu kejadian
  // nyata, alarm menyala jam 18:47:21, sudah reda lagi jam 18:47:29, TAPI
  // fix GPS yang tersedia saat itu kualitasnya "kurang" (HDOP tinggi) selama
  // beberapa detik berikutnya, DITAMBAH modem kebetulan sedang proses cari
  // sinyal seluler (blocking sampai puluhan detik) - jadi kirim yang
  // seharusnya "SEKARANG JUGA" baru benar-benar terkirim jam 18:47:49,
  // 28 detik kemudian. Solusi bagian ini: kalau ada tamper yang MENUNGGU
  // dikirim, jangan tunggu fix "bersih" - posisi kira-kira jauh lebih
  // berguna untuk peringatan keamanan daripada menunggu presisi sementara
  // waktu terus berjalan (fix presisi menyusul di kiriman terjadwal
  // berikutnya seperti biasa). Tetap syaratkan d.year valid supaya timestamp
  // yang terkirim tidak berupa data sampah dari fix yang belum benar-benar
  // terkunci.
  bool fixBolehDipaksaUntukTamper = tamperKirimSegera && d.year >= 2020;

  if (!adaFix) {
    Serial.println("[GPS] Belum ada fix, mencari satelit...");
    statusGpsFix = false;
    // BUG NYATA #6 - lihat komentar lengkap di kirimTamperPakaiPosisiTerakhir().
    if (tamperKirimSegera) {
      if (statusLastFixEpoch > 0) {
        kirimTamperPakaiPosisiTerakhir();
      } else if (millis() - lastTamperNoFixWarnMillis >= TAMPER_DIAGNOSTIC_INTERVAL_MS) {
        lastTamperNoFixWarnMillis = millis();
        Serial.println("[TAMPER] Ingin kirim SEKARANG tapi belum pernah dapat fix GPS sama sekali sejak boot - tidak ada posisi apa pun untuk dikirim, menunggu fix pertama.");
      }
    }
  } else if (!fixLayakDipakai(d) && !fixBolehDipaksaUntukTamper) {
    Serial.printf("[GPS] Fix ada tapi kualitas kurang (sat=%d, HDOP=%.2f). Menunggu fix lebih baik...\n",
                  d.satTotal, d.hdop);
    statusGpsFix = false;
    // Fix ADA tapi ditolak fixLayakDipakai() DAN belum pernah dapat fix
    // valid sebelumnya (kalau sudah pernah, fixBolehDipaksaUntukTamper di
    // atas sudah menangkapnya duluan lewat jalur BUG NYATA #5) - pakai
    // posisi cache kalau ada, sama seperti cabang !adaFix di atas.
    if (tamperKirimSegera && statusLastFixEpoch > 0) {
      kirimTamperPakaiPosisiTerakhir();
    }
  } else {
    if (fixBolehDipaksaUntukTamper && !fixLayakDipakai(d)) {
      Serial.printf("[TAMPER] Fix kualitas kurang (sat=%d, HDOP=%.2f) TAPI dipakai paksa - tamper menunggu kiriman, posisi kira-kira lebih baik daripada menunda.\n",
                    d.satTotal, d.hdop);
    }
    statusGpsFix = true;
    statusLastLat = d.latitude;
    statusLastLng = d.longitude;
    statusLastFixEpoch = utcEpochFromGps(d);
    statusLastFixCapturedAtMillis = millis();

    // Mode transmisi adaptif 3 TINGKAT - GPS SATU-SATUNYA acuan (lihat
    // TEMUAN LAPANGAN di definisi GPS_MOVING_CONFIRM_MS di atas untuk alasan
    // MPU6050 sengaja TIDAK LAGI dipakai di sini). Penjelasan lengkap 3
    // tingkatnya ada di definisi TRANSMIT_INTERVAL_MOVING_MS/STILL_MS/
    // SLEEP_MS dan SLEEPY_KE_SLEEP_MS di atas.
    bool gpsGerakSaatIni = d.speedKnots > SPEED_MOVING_THRESHOLD_KNOTS;
    if (gpsGerakSaatIni) {
      if (gpsGerakSejakMillis == 0) gpsGerakSejakMillis = millis();  // baru saja mulai di atas ambang
    } else {
      gpsGerakSejakMillis = 0;
    }
    // Baru dipercaya BERGERAK sungguhan kalau sudah bertahan di atas ambang
    // minimal GPS_MOVING_CONFIRM_MS - satu fix noise sesaat tidak cukup.
    bool sedangBergerak = gpsGerakSaatIni &&
        (millis() - gpsGerakSejakMillis >= GPS_MOVING_CONFIRM_MS);

    unsigned long interval;
    const char* modeLabel;
    if (sedangBergerak) {
      diamSejakMillis = 0;  // sedang bergerak - reset penghitung "sudah berapa lama diam"
      interval = TRANSMIT_INTERVAL_MOVING_MS;
      modeLabel = "BERGERAK";
    } else {
      if (diamSejakMillis == 0) diamSejakMillis = millis();  // baru saja mulai diam
      unsigned long durasiDiamMs = millis() - diamSejakMillis;
      if (durasiDiamMs < SLEEPY_KE_SLEEP_MS) {
        interval = TRANSMIT_INTERVAL_STILL_MS;
        modeLabel = "SLEEPY";
      } else {
        interval = TRANSMIT_INTERVAL_SLEEP_MS;
        modeLabel = "SLEEP";
      }
    }
    // Perintah MQTT "interval" dari server menimpa interval di SEMUA mode
    // selama masih aktif - label/logika mode di atas tetap seperti biasa.
    if (intervalOverrideAktif) interval = intervalOverrideMs;
    statusModeLabel = modeLabel;  // dibaca dashboard portal - lihat handleStatus()

    // Diagnostik KHUSUS investigasi "BUG NYATA #8" (SLEEP 30 menit tidak
    // pernah tercapai di data lapangan) - cetak tiap 30 detik SELAMA diam
    // (modeLabel != BERGERAK) supaya kelihatan jelas apakah diamSejakMillis
    // benar-benar terus bertambah tanpa reset sampai lewat 15 menit, atau
    // tiba-tiba "lompat" balik ke nilai kecil (indikasi reboot/reset diam-
    // diam - lihat alasanResetTerakhir() di setup() untuk konfirmasi lebih
    // lanjut kalau memang itu penyebabnya).
    if (!sedangBergerak) {
      static unsigned long lastModeDiagMillis = 0;
      if (millis() - lastModeDiagMillis >= 30000UL) {
        lastModeDiagMillis = millis();
        Serial.printf("[MODE kalibrasi] mode=%s durasiDiamMs=%lu (%.1f menit) diamSejakMillis=%lu intervalMs=%lu\n",
                      modeLabel, (millis() - diamSejakMillis), (millis() - diamSejakMillis) / 60000.0,
                      diamSejakMillis, interval);
      }
    }

    // Alarm tamper BARU saja menyala (lihat tamperKirimSegera di [MPU6050
    // TAMPER]) - paksa kirim SEKARANG, jangan tunggu interval mode biasa
    // (bisa sampai 30 menit di mode SLEEP, padahal justru saat itulah
    // tamper paling mungkin terjadi - kendaraan terparkir/tidak dipakai).
    bool kirimPaksaTamper = tamperKirimSegera;
    if (kirimPaksaTamper) {
      tamperKirimSegera = false;
      Serial.println("[TAMPER] Memaksa kirim posisi+status sekarang juga (alarm baru menyala).");
    }

    // BUG NYATA #4 (ditemukan lewat pengujian nyata 2026-09, log Serial +
    // database dicek langsung - alarm bunyi/tercatat "alarmAktif=ya" tapi
    // payload yang benar-benar terkirim malah Tamper=tidak): tamperKirimSegera
    // cuma penanda "tolong paksa kirim", BUKAN pembawa nilai tamper itu
    // sendiri - nilai yang benar-benar dikirim dulu dibaca dari variabel
    // GLOBAL tamperAlarmAktif di titik pengiriman. Kalau pengiriman sempat
    // tertunda beberapa iterasi loop() (mis. fix GPS belum siap persis saat
    // rising edge terjadi), tamperUpdate() yang terus jalan tiap iterasi bisa
    // saja SUDAH menganggap alat tenang lagi (TAMPER_RECOVER_MS cuma 1 detik)
    // dan mengembalikan tamperAlarmAktif ke false SEBELUM pengiriman yang
    // tertunda itu sempat jalan - akibatnya kejadian tamper yang SUNGGUHAN
    // terjadi malah terkirim dengan bit tamper=0, hilang sama sekali dari
    // riwayat. Solusi: tangkap "apakah tamper sungguhan terjadi" TEPAT di
    // titik keputusan kirim (gabungan status LIVE saat ini ATAU alarm baru
    // saja dipicu), simpan ke variabel lokal, lalu pakai variabel itu untuk
    // payload DAN log - bukan baca ulang tamperAlarmAktif yang sudah mungkin
    // berubah.
    // Alarm hasil UJI (test_tamper) hanya ditandai di paket kirim-paksanya
    // sendiri - paket terjadwal selama buzzer uji menyala tetap tamper=0.
    bool tamperUntukDikirim = (tamperAlarmAktif && !tamperAlarmDariUji) || kirimPaksaTamper;

    if (kirimPaksaTamper || millis() - lastSendTime >= interval) {
      Serial.println("\n=== GPS DATA (LAYAK KIRIM) ===");
      Serial.printf("Lat=%.6f Lng=%.6f Speed=%.2f knot Heading=%.1f Sat=%d HDOP=%.2f Mode=%s Tamper=%s\n",
                    d.latitude, d.longitude, d.speedKnots,
                    d.course, d.satTotal, d.hdop, modeLabel, tamperUntukDikirim ? "YA" : "tidak");

      cobaKirimDenganRetry(d, tamperUntukDikirim);  // isi statusSendOk sendiri, & buffer otomatis kalau gagal semua
      if (kirimPaksaTamper) tamperUjiTandaiKirim = false;  // tanda uji cuma untuk satu paket kirim-paksa ini
      lastSendTime = millis();
      statusSendMillis = millis();
    }
  }

  delayLayaniPortal(500);
}
