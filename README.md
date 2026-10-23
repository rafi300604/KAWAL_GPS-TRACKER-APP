# KAWAL — GPS Tracker App

GPS tracking system built for a skripsi (undergraduate thesis) — a Flutter
app (Web, Android, iOS) that shows a device's live position on a map,
lets you drop waypoints and see ETA, and replays its movement history —
fed by a custom ESP32 GPS tracker over cellular data.

## Architecture

```
┌─────────────────┐   GSM (cellular)   ┌──────────────────────┐
│  ESP32 Wrover B  │ ─────────────────▶│  FastAPI + PostgreSQL │
│  + A7670E GNSS/  │   POST /telemetry  │  (backend/, Docker,   │
│  GSM modem       │   X-API-Key auth   │  behind Caddy HTTPS)  │
└─────────────────┘                    └───────────┬───────────┘
                                                     │ X-Admin-Key (device
                                                     │ position/history) +
                                                     │ JWT (login/users/
                                                     │ waypoints)
                                                     ▼
                                        ┌──────────────────────┐
                                        │     Flutter App        │
                                        │  Web / Android / iOS  │
                                        │  (lib/), "KAWAL"       │
                                        └──────────────────────┘
```

- **Everything** — device position/history, login, user management, and
  waypoints — lives in the FastAPI/PostgreSQL backend (`backend/`). The
  ESP32 posts telemetry to it directly over GSM; the app polls it every 5s
  for live position/waypoints and fetches history on demand. See
  [`FIRMWARE_CONTRACT.md`](FIRMWARE_CONTRACT.md) for the full device↔backend
  API contract and the story of how/why it ended up this way (short version:
  a Firebase Auth ID token is too long for the modem's AT-command interface,
  so the device authenticates with a short static API key instead).
- **Three separate auth schemes on the backend, by design** — see
  [`backend/README.md`](backend/README.md#ringkasan-desain):
  - `X-API-Key` (static, shared) — the ESP32 posting telemetry.
  - `X-Admin-Key` (static, shared, embedded in the app) — reading device
    position/history (`GET /devices`, `/devices/{sn}`,
    `/telemetry/{sn}/history`).
  - `Authorization: Bearer <JWT>` (per-user, from `POST /auth/login`) —
    login, user management (`/users*`), and waypoints
    (`/devices/{sn}/waypoints*`).
- **Firebase is fully retired.** Login used to be Firebase Auth and
  waypoints/users lived in Firestore; both were migrated to the backend
  above on 2026-09-10. `functions/`, `firebase.json`,
  `firestore.rules`/`indexes.json`, and `lib/firebase_options.dart` have
  been deleted — they're no longer part of this project. The Android/iOS
  native Firebase config files (`google-services.json`,
  `GoogleService-Info.plist`) are still present only because removing them
  requires also removing the `com.google.gms.google-services` Gradle
  plugin; nothing in the Dart code pulls in the Firebase SDK anymore, so
  they're inert.

## Features

- Live map (OpenStreetMap via `flutter_map`) with a rotating device marker,
  colored green/red for active (data in the last 24h) vs. inactive.
- Waypoints — add by tapping the map or entering coordinates, with ETA
  computed along the actual road route (OSRM) rather than a straight line.
- Tracking history — pick a day range (1–30, stepper + manual entry),
  auto-plays the route from start to finish with a moving marker, replay
  button, and Excel export.
- Dark/light theme, persisted.
- Responsive layout: permanent sidebar on wide screens, drawer on narrow
  ones — same feature set on web, Android, and iOS.
- Login (email/password against the backend, JWT stored via
  `SharedPreferences`) and admin user management (list/search/filter,
  create, edit name/role) — all backend-native, no Firebase Auth.
- Splash screen (KAWAL logo) on launch while the saved session is restored.

## Project structure

```
lib/                  Flutter app source
  screens/            Home map screen, admin, splash, widgets (panels, dialogs)
  services/           Auth/device/ETA/export services, routing (OSRM)
  models/              Data models (device, position, waypoint, route, app_user)
  providers/           Auth + theme state (ChangeNotifier)
  config/               Backend base URL + API keys
backend/               FastAPI + PostgreSQL service (Docker) — telemetry,
                        auth/login, users, waypoints
test/                   Unit tests (ETA/distance/duration calculations)
FIRMWARE_CONTRACT.md    ESP32 ↔ backend API contract
assets/icons/           App icons + splash logo (KAWAL branding)
```

## Getting started

```bash
flutter pub get
cp secrets.example.json secrets.json   # isi kunci API dari .env backend (lihat di bawah)
flutter run --dart-define-from-file=secrets.json
flutter build ios --profile --dart-define-from-file=secrets.json
flutter test                 # run unit tests
flutter analyze              # static analysis
```

For the backend, see [`backend/README.md`](backend/README.md) (Docker
Compose setup, environment variables, full API reference including
`/auth/login`, `/users`, and `/devices/{sn}/waypoints`). For the ESP32
firmware contract, see [`FIRMWARE_CONTRACT.md`](FIRMWARE_CONTRACT.md); the firmware source itself is in
[`firmware/`](firmware/README.md).

## Security note

No credentials are stored in this repository:

- **App** — `lib/config/backend_config.dart` reads the backend's device/admin API keys at build time
  (`String.fromEnvironment`) from `secrets.json`, passed with `--dart-define-from-file=secrets.json`.
  `secrets.json` is git-ignored; copy `secrets.example.json` and fill in the values from the backend
  `.env`. The optional `TESTER_EMAIL` / `TESTER_PASSWORD` entries enable the "Login Cepat" shortcut in
  debug/profile builds only.
- **Firmware** — `firmware/skripsi/secrets.h` (git-ignored) holds the MQTT and device credentials; see
  `firmware/skripsi/secrets.example.h`.

The admin key still ends up inside the built app binary (device-read endpoints have no per-user auth by
design; see "Architecture" above), so do not distribute builds publicly. Login/users/waypoints use proper
per-user JWT auth. Older commits contained a tester password that has since been changed and API keys of a retired
server that is no longer used.
