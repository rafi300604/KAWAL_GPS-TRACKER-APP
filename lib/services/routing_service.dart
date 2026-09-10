import 'dart:convert';

import 'package:http/http.dart' as http;
import 'package:latlong2/latlong.dart';

import '../models/route_result.dart';

/// Calls the public OSRM (Open Source Routing Machine) API to compute a
/// real, road-following driving route through an ordered list of points —
/// the free/open-source equivalent of Google Maps Directions: given a start
/// point and one or more stops, it returns the actual route along
/// car-drivable roads (not a straight line), plus real distance/duration
/// based on road speeds, exactly like Google Maps' routing engine does.
class RoutingService {
  static const String _baseUrl = 'https://router.project-osrm.org/route/v1/driving';

  /// [points] must have at least 2 entries: the start (vessel position)
  /// followed by each waypoint, in the order they should be visited.
  /// Returns null if no road route could be found (e.g. the points are not
  /// reachable by road, or the routing service is unreachable) — callers
  /// should fall back to a straight-line estimate in that case.
  Future<RouteResult?> fetchDrivingRoute(List<LatLng> points) async {
    if (points.length < 2) return null;

    final coords = points.map((p) => '${p.longitude},${p.latitude}').join(';');
    final url = Uri.parse('$_baseUrl/$coords?overview=full&geometries=geojson&steps=false');

    final http.Response response;
    try {
      response = await http.get(url).timeout(const Duration(seconds: 10));
    } catch (_) {
      return null; // network error / timeout — caller falls back to straight-line
    }
    if (response.statusCode != 200) return null;

    final Map<String, dynamic> data;
    try {
      data = jsonDecode(response.body) as Map<String, dynamic>;
    } catch (_) {
      return null;
    }
    if (data['code'] != 'Ok') return null;

    try {
      final routes = data['routes'] as List?;
      if (routes == null || routes.isEmpty) return null;
      final route = routes.first as Map<String, dynamic>;

      final coordinates = (route['geometry']['coordinates'] as List)
          .map((c) => LatLng(
                ((c as List)[1] as num).toDouble(),
                (c[0] as num).toDouble(),
              ))
          .toList();

      final legs = (route['legs'] as List)
          .map((l) => RouteLeg(
                distanceMeters: (l['distance'] as num).toDouble(),
                durationSeconds: (l['duration'] as num).toDouble(),
              ))
          .toList();

      return RouteResult(
        geometry: coordinates,
        totalDistanceMeters: (route['distance'] as num).toDouble(),
        totalDurationSeconds: (route['duration'] as num).toDouble(),
        legs: legs,
      );
    } catch (_) {
      return null; // unexpected response shape — caller falls back to straight-line
    }
  }
}
