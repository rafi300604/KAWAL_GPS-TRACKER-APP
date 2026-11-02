import 'dart:convert';

import 'package:http/http.dart' as http;

import '../models/place_result.dart';

/// Cari koordinat dari nama tempat, lewat Nominatim - mesin geocoding
/// resmi OpenStreetMap, pasangan alami dari tile OSM yang sudah dipakai
/// `flutter_map` di app ini (sama seperti RoutingService pakai OSRM, mesin
/// routing open-source yang juga berbasis data OSM). Gratis, tanpa API key,
/// TAPI kebijakan pemakaiannya (https://operations.osmfoundation.org/policies/nominatim/)
/// mewajibkan dua hal yang dijaga di sini:
///  1. Header `User-Agent` yang jelas mengidentifikasi aplikasi (bukan
///     default kosong dari paket `http` - request tanpa ini bisa ditolak).
///  2. Maksimal ~1 request/detik - dijaga oleh caller (AddWaypointDialog)
///     lewat debounce saat user mengetik, BUKAN di sini, supaya class ini
///     tetap "panggil = langsung jalan" tanpa throttle tersembunyi yang
///     bisa mengejutkan caller lain nanti.
class GeocodingService {
  static const _baseUrl = 'https://nominatim.openstreetmap.org/search';
  static const _userAgent = 'KawalGpsTracker-Skripsi/1.0';

  Future<List<PlaceResult>> search(String query) async {
    final trimmed = query.trim();
    if (trimmed.isEmpty) return [];

    final url = Uri.parse(_baseUrl).replace(queryParameters: {
      'q': trimmed,
      'format': 'json',
      'limit': '5',
      // Indonesia dulu di hasil (mayoritas device dipakai di sini), tapi
      // tetap global kalau tidak match - Nominatim pakai ini cuma sebagai
      // prioritas urutan, bukan filter keras (lihat param `countrycodes`
      // yang sengaja TIDAK dipakai di sini kalau mau filter keras).
      'accept-language': 'id',
    });

    final http.Response response;
    try {
      response = await http
          .get(url, headers: {'User-Agent': _userAgent})
          .timeout(const Duration(seconds: 10));
    } catch (_) {
      return [];
    }
    if (response.statusCode != 200) return [];

    try {
      final list = jsonDecode(response.body) as List;
      return list.map((e) {
        final m = e as Map<String, dynamic>;
        return PlaceResult(
          displayName: m['display_name'] as String,
          lat: double.parse(m['lat'] as String),
          lng: double.parse(m['lon'] as String),
        );
      }).toList();
    } catch (_) {
      return [];
    }
  }
}
