# Firmware KAWAL — ESP32 LilyGO T-Call A7670E

`skripsi/skripsi.ino` adalah firmware perangkat pelacak (ESP32-01): baca GNSS modul SIMCom A7670E, kirim
telemetry lewat MQTT (GSM atau WiFi) ke broker backend, deteksi tamper (MPU6050 + buzzer), dan terima
perintah server lewat topic `cmd/<SN>` (interval, reset, buzzer, test_tamper). Kontrak data: lihat
`../FIRMWARE_CONTRACT.md`.

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
