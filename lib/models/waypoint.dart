import 'package:cloud_firestore/cloud_firestore.dart';

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

  factory Waypoint.fromFirestore(DocumentSnapshot<Map<String, dynamic>> doc) {
    final data = doc.data() ?? {};
    return Waypoint(
      id: doc.id,
      name: data['name'] as String? ?? '',
      lat: (data['lat'] as num?)?.toDouble() ?? 0,
      lng: (data['lng'] as num?)?.toDouble() ?? 0,
      createdAt: (data['createdAt'] as Timestamp?)?.toDate() ?? DateTime.now(),
    );
  }

  Map<String, dynamic> toFirestore() {
    return {
      'name': name,
      'lat': lat,
      'lng': lng,
      'createdAt': Timestamp.fromDate(createdAt),
    };
  }
}
