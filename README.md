# KAWAL — GPS Tracker App

GPS tracking system built for a skripsi (undergraduate thesis): a custom ESP32 tracker sends its position over
cellular (with WiFi as backup) to a FastAPI backend, and the Flutter app (Web, Android, iOS) shows the live
position, tamper alarms, connection status, waypoints with ETA, and the movement history.

This repo holds the app, the device firmware, and a mirror of the backend. The deployed backend lives in
its own repo, [rafi300604/kawal-gps-backend](https://github.com/rafi300604/kawal-gps-backend).

## Architecture

```
┌───────────────────────────┐  MQTT over TLS :8883                ┌──────────────────────────────┐
│ ESP32 (LilyGO T-Call      │  telemetry/<SN>  status/<SN>        │ mosquitto broker             │
│ A7670E) + MPU6050 + buzzer│  cmd-ack/<SN>  ───────────────────▶ │ (user/pass + per-device ACL) │
│                           │  ◀─────────────────── cmd/<SN>      └──────────────┬───────────────┘
│ GSM/LTE primary,          │                                                    │ internal :1883
│ WiFi backup, RAM buffer   │                                     ┌──────────────▼───────────────┐
└───────────────────────────┘                                     │ FastAPI + MQTT subscriber    │
                                                                  │ PostgreSQL                   │
┌───────────────────────────┐  HTTPS REST, polled every 1 s       │ mqttskripsigps.              │
│ Flutter app "KAWAL"       │ ──────────────────────────────────▶ │   jejakpintar.cloud          │
│ Web / Android / iOS       │  X-Admin-Key (device data)          │ (behind the VPS nginx)       │
│                           │  JWT (login, users, waypoints)      └──────────────────────────────┘
└───────────────────────────┘
        │ map tiles: OpenStreetMap · road routing: OSRM · place search: Nominatim
```

- **Device → backend over MQTT.** Each point is a 17-byte binary record sent as 34 hex characters to
  `telemetry/<SN>`: position, speed, heading, GPS time, and flags (GSM/WiFi, battery/vehicle power,
  low-quota warning, tamper). One TLS session carries many publishes, which saves cellular data compared
  with one HTTPS request per point.
- **Two-way commands.** The backend can send `interval`, `reset`, `buzzer`, and `test_tamper` to
  `cmd/<SN>`; the device answers on `cmd-ack/<SN>`.
- **Online/offline.** The device publishes `online` to `status/<SN>` after every connect and registers a
  Last Will `offline`. If power is cut or the signal is lost, the broker publishes `offline` itself, and the
  app shows the device as Offline.
- **App → backend over REST.** The app polls `GET /devices` and `/devices/{sn}/alerts` every second.
  History is fetched on demand with `?since=`, so a long range is drawn in full.
- **Three auth schemes, by design:**
  - `X-API-Key` (shared) — device telemetry over HTTPS, kept as an alternative path.
  - `X-Admin-Key` (shared, compiled into the app) — device state, history, alerts, commands.
  - `Authorization: Bearer <JWT>` (per user, from `POST /auth/login`) — users and waypoints.

Full contract: [`FIRMWARE_CONTRACT.md`](FIRMWARE_CONTRACT.md). Backend details:
[`backend/README.md`](backend/README.md). Firmware: [`firmware/README.md`](firmware/README.md).

### Device behaviour

| Mode | When | Send interval |
|---|---|---|
| BERGERAK | GPS speed > 3.5 knots for 15 s | 30 s |
| SLEEPY | Just stopped | 5 min |
| SLEEP | Stopped for 15 min | 30 min |

- **Path selection.** GSM (MQTT through the A7670E) is the primary path; WiFi (MQTT through the ESP32) is
  the backup. With no path at all, up to 200 points are buffered in RAM and sent oldest-first once a path
  returns, before the live point.
- **Tamper.** An MPU6050 (21 Hz low-pass) detects forced handling while the vehicle is not moving. The
  buzzer sounds and a tamper point is sent at once.
- **Power source.** A divider on the vehicle-power node tells vehicle power (AKI) from the 2×18650 backup
  battery.
- **Config portal.** An always-on access point serves a web portal for the device name, WiFi, hostname, and APN settings.

## Features

- **Live map.** OpenStreetMap via `flutter_map` with a rotating device marker. The status shows
  Aktif / Tidak Aktif (no data for 24 h) / Offline (connection lost).
- **Alerts.** Pop-ups for tamper (with an alarm sound and a map marker), remote buzzer, low SIM quota, and
  device disconnected or reconnected. The full list is in the alert history dialog.
- **Waypoints.** Add one by tapping the map, typing coordinates, or searching a place name (Nominatim). ETA
  follows the road route (OSRM), not a straight line.
- **Tracking history.** Pick 1–30 days. The route auto-plays from start to finish with a moving marker and
  can be replayed. Excel export includes data source, power source, and tamper per point.
- **Accounts.** Login with email/password (JWT). Admins can list, search, create, and edit users.
- **Interface.** Dark/light theme (persisted). Responsive layout: permanent sidebar on wide screens, drawer
  on narrow ones, with the same features on web, Android, and iOS.

## Project structure

```
lib/                    Flutter app source
  screens/              Home map screen, login, splash, admin (users), widgets (panels, dialogs)
  services/             Auth, device polling, ETA, routing (OSRM), geocoding (Nominatim), Excel export
  models/               Device, position, alert, waypoint, route, place, app user
  providers/            Auth + theme state (ChangeNotifier)
  config/               Backend URL + API keys (read from secrets.json at build time)
  theme/, utils/        Theme, formatters, compass, relative time
assets/                 App icons, splash logo, tamper alarm sound
firmware/skripsi/       ESP32 firmware (skripsi.ino) + secrets.example.h
backend/                Mirror of rafi300604/kawal-gps-backend (deploy from that repo, not from here)
test/                   Unit tests (ETA/distance/duration, formatters)
FIRMWARE_CONTRACT.md    Device ↔ backend ↔ app contract
secrets.example.json    Template for the git-ignored secrets.json
run.sh                  `flutter run` with secrets.json already passed
```

## Getting started

```bash
flutter pub get
cp secrets.example.json secrets.json   # fill in the API keys from the backend .env
./run.sh -d chrome                     # = flutter run --dart-define-from-file=secrets.json -d chrome
flutter build ios --release --dart-define-from-file=secrets.json
flutter test
flutter analyze
```

VS Code passes `secrets.json` automatically (`.vscode/settings.json`), so F5 works too. Builds made without
`secrets.json` get empty API keys and every backend request returns 401.

On a physical iPhone, install with `flutter build ios` and then `xcrun devicectl device install app`, not
`flutter run`. Stale `flutter run` launches can leave the app stuck on a white screen.

## Security note

No credentials are stored in this repository:

- **App:** `lib/config/backend_config.dart` reads the device/admin API keys at build time
  (`String.fromEnvironment`) from `secrets.json`. `secrets.json` is git-ignored; copy
  `secrets.example.json` and fill in the values from the backend `.env`. The optional `TESTER_EMAIL` /
  `TESTER_PASSWORD` entries enable the "Login Cepat" shortcut in debug/profile builds only.
- **Firmware:** `firmware/skripsi/secrets.h` (git-ignored) holds the MQTT and device credentials; see
  `firmware/skripsi/secrets.example.h`.

The admin key still ends up inside the built app binary, because the device-read endpoints have no per-user
auth by design. Do not distribute builds publicly. Login, users, and waypoints use proper per-user JWT auth.

Older commits contained:
- a tester password that has since been changed;
- API keys of a retired server that is no longer used.

**Firebase is retired.** Login and data moved to the backend on 2026-09-10. `google-services.json` and
`GoogleService-Info.plist` remain only because the Android build still applies the
`com.google.gms.google-services` Gradle plugin. No Dart code uses Firebase.
