import 'package:latlong2/latlong.dart';

/// One leg of a multi-stop route — the road-following segment between two
/// consecutive points (e.g. vessel -> waypoint 1, or waypoint 1 -> waypoint 2).
class RouteLeg {
  final double distanceMeters;
  final double durationSeconds;

  const RouteLeg({required this.distanceMeters, required this.durationSeconds});

  double get distanceKm => distanceMeters / 1000;
  double get durationHours => durationSeconds / 3600;
}

/// A full driving route through an ordered list of points (vessel position
/// followed by each waypoint, in order), as returned by a road-routing
/// engine (OSRM) — mirrors what Google Maps Directions returns: the actual
/// road-following geometry plus real distance/duration per leg and overall.
class RouteResult {
  final List<LatLng> geometry;
  final double totalDistanceMeters;
  final double totalDurationSeconds;
  final List<RouteLeg> legs;

  const RouteResult({
    required this.geometry,
    required this.totalDistanceMeters,
    required this.totalDurationSeconds,
    required this.legs,
  });

  double get totalDistanceKm => totalDistanceMeters / 1000;
  double get totalDurationHours => totalDurationSeconds / 3600;
}
