# GPS Tracker App

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
                                                     │ GET (X-Admin-Key)
                                                     ▼
┌─────────────────┐   Firebase Auth    ┌──────────────────────┐
│  Firebase Auth   │◀──────────────────│     Flutter App        │
│  (login)         │                    │  Web / Android / iOS  │
└─────────────────┘                    │  (lib/)                │
┌─────────────────┐   Firestore        │                        │
│  Cloud Firestore │◀──────────────────│  device position/      │
│  (waypoints only)│                    │  history via polling  │
└─────────────────┘                    └──────────────────────┘
```

- **Device position & history** live in the FastAPI/PostgreSQL backend
  (`backend/`) — the ESP32 posts to it directly over GSM; the app polls it
  every 5s for live position and fetches history on demand. See
  [`FIRMWARE_CONTRACT.md`](FIRMWARE_CONTRACT.md) for the full device↔backend
  API contract and the story of how/why it ended up this way (short version:
  a Firebase Auth ID token is too long for the modem's AT-command interface,
  so the device authenticates with a short static API key instead).
- **Login and waypoints** stay on Firebase (Auth + Firestore) — those never
  needed to move off Firebase, only the high-frequency device telemetry did.
- `functions/` is an earlier, now-unused Cloud Functions approach (kept for
  reference) — superseded by the `backend/` FastAPI service.

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
- Admin user management (Firebase Auth + Firestore `users` collection).

## Project structure

```
lib/                  Flutter app source
  screens/            Home map screen, admin, widgets (panels, dialogs)
  services/           Device/ETA/export services, routing (OSRM)
  models/              Data models (device, position, waypoint, route)
  providers/           Auth + theme state (ChangeNotifier)
  config/               Backend base URL + API keys
backend/               FastAPI + PostgreSQL telemetry service (Docker)
functions/              Legacy Firebase Cloud Function (unused, kept for reference)
test/                   Unit tests (ETA/distance/duration calculations)
FIRMWARE_CONTRACT.md    ESP32 ↔ backend API contract
firestore.rules         Firestore security rules (users, waypoints)
```

## Getting started

```bash
flutter pub get
flutter run                 # picks a connected device/emulator/Chrome
flutter test                 # run unit tests
flutter analyze              # static analysis
```

Firebase config (`firebase_options.dart`, `google-services.json`,
`GoogleService-Info.plist`) is already included, pointing at the project's
existing Firebase project (Auth + Firestore).

For the backend, see [`backend/README.md`](backend/README.md) (Docker
Compose setup, environment variables, API reference). For the ESP32
firmware contract, see [`FIRMWARE_CONTRACT.md`](FIRMWARE_CONTRACT.md).

## Security note

This is a thesis/demo project — `lib/config/backend_config.dart` has the
backend's device/admin API keys embedded directly in the client for
simplicity (there's no per-user backend auth scheme). **Keep this repo
private**, or replace those with build-time secrets (e.g. `--dart-define`)
before making it public.
