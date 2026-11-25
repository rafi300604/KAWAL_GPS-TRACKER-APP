# Firmware KAWAL — ESP32 LilyGO T-Call A7670E

`skripsi/skripsi.ino` adalah firmware perangkat pelacak (ESP32-01): baca GNSS modul SIMCom A7670E, kirim
telemetry lewat MQTT (GSM atau WiFi) ke broker backend, deteksi tamper (MPU6050 + buzzer), dan terima
perintah server lewat topic `cmd/<SN>` (interval, reset, buzzer, test_tamper). Kontrak data: lihat
`../FIRMWARE_CONTRACT.md`.

Ringkasan perilaku:

- **Jalur kirim.** GSM/LTE lewat MQTT modem A7670E adalah jalur utama, WiFi lewat MQTT ESP32
  (PubSubClient) cadangan. Keduanya TLS ke `mqttskripsigps.jejakpintar.cloud:8883`. Kalau dua-duanya putus,
  titik disimpan di antrean berjenjang: SRAM (200 titik) sekaligus berkas LittleFS di flash (16 byte per titik,
  partisi 1408 KB), jadi antrean selamat walau alat reboot/mati. Begitu jalur pulih titik disusulkan dari yang paling
  lama, lalu baru titik live. Diuji 25 Nov 2026 (Tabel 4.2): 205/205 titik tersusul, reboot saat offline 50/50.
- **Build uji.** `#define UJI_JARINGAN 1` mengaktifkan perintah MQTT `lte_off` / `wifi_off` (memutus jalur selama
  `duration_s`, dipakai uji Tabel 4.2). Firmware harian memakai 0.
- **Interval.** BERGERAK 30 s (kecepatan GPS > 3,5 knot selama 15 s), SLEEPY 5 menit (baru berhenti),
  SLEEP 30 menit (diam 15 menit).
- **Tamper.** MPU6050 (DLPF 21 Hz) hanya dievaluasi saat tidak BERGERAK. Alarm menyala kalau deviasi dan
  rotasi melewati ambang selama ≥ 800 ms; buzzer bunyi dan titik tamper langsung dikirim.
- **Portal.** Access point selalu aktif; portal web untuk nama alat, WiFi, hostname, dan APN.

Status koneksi: setiap selesai connect alat publish `online` ke `status/<SN>` dan memasang Last Will `offline`
di topic yang sama (keepalive GSM 30 s), jadi dashboard tahu kalau daya alat dicabut. Broker wajib punya baris ACL
`pattern write status/%c` - tanpa itu broker menolak koneksi alat yang memasang Will.

## Kredensial

Kredensial (`DEVICE_API_KEY`, `MQTT_USERNAME`, `MQTT_PASSWORD`, `AP_PASSWORD`) **tidak** ada di repo.
Salin `skripsi/secrets.example.h` menjadi `skripsi/secrets.h` lalu isi nilai asli. `secrets.h` sudah
di-`.gitignore`.

## Compile & upload

```bash
cd firmware/skripsi
arduino-cli compile --fqbn esp32:esp32:esp32 .
arduino-cli upload  --fqbn esp32:esp32:esp32 -p /dev/cu.usbserial-XXXX .
```

Core yang dipakai: `esp32:esp32` 3.3.12. Salinan ini diambil dari folder sketch Arduino peneliti
(`~/Documents/Arduino/koding/skripsi/`); perbarui file ini setiap firmware di sana berubah.
