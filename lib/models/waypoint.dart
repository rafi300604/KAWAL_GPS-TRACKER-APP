import '../config/backend_config.dart';

class Waypoint {
  final String id;
  final String name;
  final double lat;
  final double lng;
  final DateTime createdAt;

  const Waypoint({
    required this.id,
    required this.name,
    required this.lat,
    required this.lng,
    required this.createdAt,
  });

  /// Dari respons backend FastAPI (`GET/POST /devices/{sn}/waypoints`) —
  /// lihat `backend/app/schemas.py:WaypointOut`.
  factory Waypoint.fromJson(Map<String, dynamic> json) {
    return Waypoint(
      id: json['id'].toString(),
      name: json['name'] as String? ?? '',
      lat: (json['lat'] as num?)?.toDouble() ?? 0,
      lng: (json['lng'] as num?)?.toDouble() ?? 0,
      createdAt: json['createdAt'] != null
          ? parseBackendUtc(json['createdAt'] as String)
          : DateTime.now(),
    );
  }
}
