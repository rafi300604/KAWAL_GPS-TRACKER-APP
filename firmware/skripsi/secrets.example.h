// secrets.example.h — salin menjadi secrets.h (di folder yang sama) lalu isi nilai asli.
// secrets.h sengaja tidak di-commit karena berisi kredensial produksi.
#pragma once

// Shared secret jalur HTTPS POST /telemetry (= DEVICE_API_KEY di .env backend).
const char* DEVICE_API_KEY = "ISI_DEVICE_API_KEY";

// Kredensial broker MQTT (listener 8883, TLS; lihat passwd + acl broker).
const char*    MQTT_USERNAME    = "ISI_USERNAME_MQTT";
const char*    MQTT_PASSWORD    = "ISI_PASSWORD_MQTT";

// Password WiFi access point portal konfigurasi alat (minimal 8 karakter).
#define AP_PASSWORD  "ISI_PASSWORD_AP_MIN_8"
