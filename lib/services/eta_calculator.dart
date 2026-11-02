import 'dart:math';

/// ETA color bands, matching the reference app's thresholds.
enum EtaLevel { good, warn, bad }

class EtaResult {
  final double distanceNm;
  final double hours; // travel time in hours, Infinity-safe (see [hoursFor])
  final String durationLabel;
  final DateTime? arrival;
  final EtaLevel level;

  const EtaResult({
    required this.distanceNm,
    required this.hours,
    required this.durationLabel,
    required this.arrival,
    required this.level,
  });

  double get distanceKm => distanceNm * 1.852;
}

/// Great-circle distance between two lat/lng points, in kilometers.
/// Haversine formula, Earth radius = 6371 km.
double computeDistanceKm(double lat1, double lng1, double lat2, double lng2) {
  const earthRadiusKm = 6371.0;
  final dLat = _toRadians(lat2 - lat1);
  final dLng = _toRadians(lng2 - lng1);
  final a = sin(dLat / 2) * sin(dLat / 2) +
      cos(_toRadians(lat1)) *
          cos(_toRadians(lat2)) *
          sin(dLng / 2) *
          sin(dLng / 2);
  final c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return earthRadiusKm * c;
}

/// Great-circle distance between two lat/lng points, in nautical miles.
double computeDistanceNm(double lat1, double lng1, double lat2, double lng2) {
  return computeDistanceKm(lat1, lng1, lat2, lng2) / 1.852;
}

double _toRadians(double degrees) => degrees * pi / 180;

/// Formats a duration in hours the same way the reference app does:
/// <1h -> "N menit", <24h -> "Hj Mm", else -> "Dh Hj".
String formatDuration(double hours) {
  if (!hours.isFinite || hours < 0) return '-';
  if (hours < 1) {
    final minutes = (hours * 60).round();
    return '$minutes menit';
  } else if (hours < 24) {
    final h = hours.floor();
    final m = ((hours - h) * 60).round();
    return '${h}j ${m}m';
  } else {
    final d = (hours / 24).floor();
    final h = (hours % 24).floor();
    return '${d}h ${h}j';
  }
}

EtaLevel etaLevelForHours(double hours) {
  if (!hours.isFinite) return EtaLevel.bad;
  final totalMinutes = hours * 60;
  if (totalMinutes <= 30) return EtaLevel.good;
  if (totalMinutes <= 180) return EtaLevel.warn;
  return EtaLevel.bad;
}

/// Computes distance/ETA from a vessel position to a waypoint at the given speed (knots).
EtaResult computeEta({
  required double fromLat,
  required double fromLng,
  required double toLat,
  required double toLng,
  required double speedKnots,
  DateTime? now,
}) {
  final distanceNm = computeDistanceNm(fromLat, fromLng, toLat, toLng);
  final hours = speedKnots > 0 ? distanceNm / speedKnots : double.infinity;
  final arrival =
      hours.isFinite ? (now ?? DateTime.now()).add(Duration(seconds: (hours * 3600).round())) : null;
  return EtaResult(
    distanceNm: distanceNm,
    hours: hours,
    durationLabel: formatDuration(hours),
    arrival: arrival,
    level: etaLevelForHours(hours),
  );
}

// Jalan sungguhan hampir tidak pernah lurus sempurna - jarak garis lurus
// (haversine) SELALU lebih pendek dari jarak jalan sungguhan. Faktor "detour"
// ini (dikalikan ke jarak garis lurus) adalah perkiraan kasar jarak jalan
// kota pada umumnya, dipakai cuma di fallback ini (saat OSRM tidak
// terjangkau/tidak ketemu rute) supaya jarak & ETA-nya tidak jauh lebih
// optimis dibanding kalau rute jalan sungguhan berhasil didapat.
const double _kStraightLineDetourFactor = 1.3;

// Kecepatan rata-rata mobil di jalan kota Indonesia (memperhitungkan lampu
// merah/kemacetan/belokan, BUKAN kecepatan maksimal jalan bebas hambatan) -
// dipakai sebagai pembagi ETA di fallback ini. SENGAJA bukan kecepatan GPS
// device SAAT INI ([DeviceVessel.speedKnots]) - kecepatan instan itu BUKAN
// prediktor yang baik buat kecepatan rata-rata SISA perjalanan (bisa 0 kalau
// device sedang berhenti, atau tinggi kalau kebetulan lagi di jalan lurus
// sebentar) - itu salah satu penyebab ETA sebelumnya kelihatan "terlalu
// cepat".
const double _kFallbackAverageSpeedKmh = 25.0;

/// Straight-line (haversine) distance/ETA in km, used only as a fallback when
/// the road-routing service (OSRM) is unreachable or finds no route. Applies
/// [_kStraightLineDetourFactor] to the raw distance and divides by
/// [_kFallbackAverageSpeedKmh] (a fixed realistic urban average, not the
/// vessel's live GPS speed) — see constants above for why.
EtaResult computeStraightLineEtaKm({
  required double fromLat,
  required double fromLng,
  required double toLat,
  required double toLng,
  DateTime? now,
}) {
  final distanceKm = computeDistanceKm(fromLat, fromLng, toLat, toLng) * _kStraightLineDetourFactor;
  final hours = distanceKm / _kFallbackAverageSpeedKmh;
  final arrival =
      hours.isFinite ? (now ?? DateTime.now()).add(Duration(seconds: (hours * 3600).round())) : null;
  return EtaResult(
    distanceNm: distanceKm / 1.852, // stored as NM; use `.distanceKm` to read it back in km
    hours: hours,
    durationLabel: formatDuration(hours),
    arrival: arrival,
    level: etaLevelForHours(hours),
  );
}

/// Converts Degrees/Minutes/Seconds to a signed decimal coordinate, rounded to 8dp.
/// Throws [FormatException] with an Indonesian message on invalid input, matching
/// the reference app's validation for the DMS waypoint entry form.
double convertDmsToDecimal({
  required int degrees,
  required int minutes,
  required double seconds,
  required String direction,
}) {
  if (degrees < 0 || degrees > 180) {
    throw const FormatException('Derajat harus di antara 0 dan 180');
  }
  if (minutes < 0 || minutes > 59) {
    throw const FormatException('Menit harus di antara 0 dan 59');
  }
  if (seconds < 0 || seconds >= 60) {
    throw const FormatException('Detik harus di antara 0 dan 59.999');
  }
  final dir = direction.toUpperCase();
  if (!['N', 'S', 'E', 'W'].contains(dir)) {
    throw const FormatException('Arah harus salah satu dari N, S, E, atau W');
  }

  var decimal = degrees + minutes / 60 + seconds / 3600;
  if (dir == 'S' || dir == 'W') decimal = -decimal;
  return (decimal * 1e8).round() / 1e8;
}
