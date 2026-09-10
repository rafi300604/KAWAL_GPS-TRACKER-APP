const { onRequest } = require("firebase-functions/v2/https");
const { defineSecret } = require("firebase-functions/params");
const logger = require("firebase-functions/logger");
const admin = require("firebase-admin");
const crypto = require("crypto");

admin.initializeApp();
const db = admin.firestore();

// Kunci rahasia pendek yang dipakai ESP32 (menggantikan idToken Firebase Auth
// yang panjangnya ~900 karakter). Diset lewat:
//   firebase functions:secrets:set DEVICE_API_KEY
const DEVICE_API_KEY = defineSecret("DEVICE_API_KEY");

function timingSafeEqualStr(a, b) {
  const bufA = Buffer.from(String(a));
  const bufB = Buffer.from(String(b));
  if (bufA.length !== bufB.length) return false;
  return crypto.timingSafeEqual(bufA, bufB);
}

function isFiniteNumber(v) {
  return typeof v === "number" && Number.isFinite(v);
}

// POST /ingestPosition
// Header:  X-Device-Key: <DEVICE_API_KEY>
// Body (JSON):
//   sn           string   wajib - serial number device, dipakai sebagai doc id devices/{sn}
//   lat, lng     number   wajib
//   heading      number   opsional (default 0)
//   speedKnots   number   opsional (default 0)
//   name         string   opsional - nama tampilan device
//   powerSource  string   opsional
//   dataSource   string   opsional
//   timestamp    string   opsional, ISO 8601 - waktu fix GPS sebenarnya (dari device).
//                         Kalau tidak dikirim, dipakai waktu server.
//   historyOnly  boolean  opsional - kalau true, HANYA menambah titik ke
//                         devices/{sn}/positions, TIDAK meng-update posisi
//                         "live" di devices/{sn}. Dipakai saat menyusulkan
//                         titik yang sempat tersimpan di buffer offline ESP32,
//                         supaya titik lama itu tidak menimpa posisi terkini.
exports.ingestPosition = onRequest(
  { region: "asia-southeast2", secrets: [DEVICE_API_KEY], cors: false },
  async (req, res) => {
    if (req.method !== "POST") {
      res.status(405).json({ ok: false, error: "method not allowed" });
      return;
    }

    const providedKey = req.get("X-Device-Key") || "";
    if (!timingSafeEqualStr(providedKey, DEVICE_API_KEY.value())) {
      logger.warn("ingestPosition: API key salah/hilang");
      res.status(401).json({ ok: false, error: "unauthorized" });
      return;
    }

    const body = req.body || {};
    const sn = typeof body.sn === "string" ? body.sn.trim() : "";
    const lat = Number(body.lat);
    const lng = Number(body.lng);

    if (!sn || !isFiniteNumber(lat) || !isFiniteNumber(lng)) {
      res.status(400).json({ ok: false, error: "sn, lat, lng wajib diisi dan valid" });
      return;
    }

    const heading = isFiniteNumber(Number(body.heading)) ? Number(body.heading) : 0;
    const speedKnots = isFiniteNumber(Number(body.speedKnots)) ? Number(body.speedKnots) : 0;

    let timestamp = admin.firestore.FieldValue.serverTimestamp();
    if (typeof body.timestamp === "string" && body.timestamp.length > 0) {
      const parsed = new Date(body.timestamp);
      if (!Number.isNaN(parsed.getTime())) {
        timestamp = admin.firestore.Timestamp.fromDate(parsed);
      }
    }

    try {
      const batch = db.batch();
      const deviceRef = db.collection("devices").doc(sn);
      const positionRef = deviceRef.collection("positions").doc();

      batch.set(
        positionRef,
        { lat, lng, heading, speedKnots, timestamp },
        { merge: false }
      );

      if (!body.historyOnly) {
        const deviceFields = {
          sn,
          lat,
          lng,
          heading,
          speedKnots,
          status: "active",
          lastTransmittedAt: timestamp,
        };
        if (typeof body.name === "string" && body.name.length > 0) deviceFields.name = body.name;
        if (typeof body.powerSource === "string") deviceFields.powerSource = body.powerSource;
        if (typeof body.dataSource === "string") deviceFields.dataSource = body.dataSource;
        batch.set(deviceRef, deviceFields, { merge: true });
      }

      await batch.commit();
      res.status(200).json({ ok: true, id: positionRef.id });
    } catch (err) {
      logger.error("ingestPosition: gagal menulis ke Firestore", err);
      res.status(500).json({ ok: false, error: "internal error" });
    }
  }
);
