import '../config/backend_config.dart';

class PositionPoint {
  final String id;
  final double lat;
  final double lng;
  final double speedKnots;
  final double heading;
  final DateTime timestamp;

  const PositionPoint({
    required this.id,
    required this.lat,
    required this.lng,
    required this.speedKnots,
    required this.heading,
    required this.timestamp,
  });

  /// Dari respons backend FastAPI (`GET /telemetry/{sn}/history`) — lihat
  /// `backend/app/schemas.py:TelemetryOut`.
  factory PositionPoint.fromJson(Map<String, dynamic> json) {
    return PositionPoint(
      id: json['id'].toString(),
      lat: (json['lat'] as num?)?.toDouble() ?? 0,
      lng: (json['lng'] as num?)?.toDouble() ?? 0,
      speedKnots: (json['speedKnots'] as num?)?.toDouble() ?? 0,
      heading: (json['heading'] as num?)?.toDouble() ?? 0,
      timestamp: json['timestamp'] != null
          ? parseBackendUtc(json['timestamp'] as String)
          : DateTime.now(),
    );
  }
}
