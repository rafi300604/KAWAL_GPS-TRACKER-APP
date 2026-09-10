import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;

import '../config/backend_config.dart';
import '../models/device_vessel.dart';
import '../models/position_point.dart';
import '../models/waypoint.dart';
import 'auth_service.dart';

/// Posisi/status device (live + riwayat) DAN waypoints, semuanya dari
/// backend FastAPI sendiri (`gps.jejakpintar.cloud`, lihat
/// FIRMWARE_CONTRACT.md) — Firestore sudah tidak dipakai sama sekali lagi di
/// class ini (waypoints dulu di Firestore, sekarang di tabel `waypoints`
/// backend, dilindungi login user biasa lewat JWT - lihat AuthService).
class DeviceService {
  static const _pollInterval = Duration(seconds: 5);

  Map<String, String> get _adminHeaders => {
        'X-Admin-Key': BackendConfig.adminApiKey,
      };

  Map<String, String> get _userJsonHeaders => {
        'Content-Type': 'application/json',
        ...AuthService.bearerHeader,
      };

  // CATATAN PENTING: UI memanggil watchAllDevices()/watchDevice() langsung di
  // dalam build() (pola yang aman untuk Firestore .snapshots(), karena murah
  // dipanggil ulang) - tapi stream di sini melakukan polling HTTP sungguhan.
  // Kalau tidak di-cache, SETIAP rebuild widget (dipicu apa pun - animasi
  // peta, provider, jam "X detik lalu") akan membuat loop polling BARU yang
  // tidak pernah benar-benar berhenti (request HTTP yang sudah terlanjur
  // dikirim browser tidak bisa dibatalkan meski stream lamanya dibuang) -
  // ini pernah menyebabkan ribuan request menumpuk dalam hitungan menit.
  // Jadi stream-nya WAJIB dibuat sekali saja per instance/sn dan dipakai
  // ulang (broadcast, supaya bisa dipakai lebih dari satu StreamBuilder
  // sekaligus - home_screen dan device_sidebar berbagi instance yang sama).
  Stream<List<DeviceVessel>>? _allDevicesStream;
  final Map<String, Stream<DeviceVessel?>> _deviceStreams = {};

  /// Semua device terdaftar, untuk daftar di sidebar — di-poll berkala
  /// karena endpoint REST ini bukan realtime stream seperti Firestore
  /// snapshot listener.
  Stream<List<DeviceVessel>> watchAllDevices() {
    return _allDevicesStream ??= _pollAllDevices().asBroadcastStream();
  }

  Stream<List<DeviceVessel>> _pollAllDevices() async* {
    while (true) {
      try {
        final res = await http
            .get(Uri.parse('${BackendConfig.baseUrl}/devices'), headers: _adminHeaders)
            .timeout(const Duration(seconds: 10));
        if (res.statusCode == 200) {
          final list = jsonDecode(res.body) as List;
          yield list
              .map((e) => DeviceVessel.fromJson(e as Map<String, dynamic>))
              .toList()
            ..sort((a, b) => a.name.compareTo(b.name));
        } else {
          debugPrint('[DeviceService] GET /devices -> HTTP ${res.statusCode}: ${res.body}');
        }
      } catch (e) {
        // Hiccup jaringan sesaat - lewati tick ini, coba lagi di tick berikutnya,
        // jangan sampai stream mati gara-gara satu request gagal. TAPI tetap
        // dicetak (bukan diam-diam ditelan) supaya masalah menetap (mis. CORS
        // diblokir browser, DNS salah, dst) kelihatan di console, bukan cuma
        // "loading terus" tanpa petunjuk sama sekali.
        debugPrint('[DeviceService] watchAllDevices gagal: $e');
      }
      await Future.delayed(_pollInterval);
    }
  }

  Stream<DeviceVessel?> watchDevice(String sn) {
    return _deviceStreams.putIfAbsent(sn, () => _pollDevice(sn).asBroadcastStream());
  }

  Stream<DeviceVessel?> _pollDevice(String sn) async* {
    while (true) {
      try {
        final res = await http
            .get(Uri.parse('${BackendConfig.baseUrl}/devices/$sn'), headers: _adminHeaders)
            .timeout(const Duration(seconds: 10));
        if (res.statusCode == 200) {
          yield DeviceVessel.fromJson(jsonDecode(res.body) as Map<String, dynamic>);
        } else if (res.statusCode == 404) {
          yield null;
        } else {
          debugPrint('[DeviceService] GET /devices/$sn -> HTTP ${res.statusCode}: ${res.body}');
        }
      } catch (e) {
        debugPrint('[DeviceService] watchDevice($sn) gagal: $e');
      }
      await Future.delayed(_pollInterval);
    }
  }

  // Sama seperti watchAllDevices()/watchDevice() - broadcast stream yang
  // di-cache per sn, supaya rebuild widget tidak memicu polling loop baru
  // tiap kali (lihat catatan panjang di atas watchAllDevices()).
  final Map<String, Stream<List<Waypoint>>> _waypointStreams = {};

  Stream<List<Waypoint>> watchWaypoints(String sn) {
    return _waypointStreams.putIfAbsent(sn, () => _pollWaypoints(sn).asBroadcastStream());
  }

  Stream<List<Waypoint>> _pollWaypoints(String sn) async* {
    while (true) {
      try {
        final res = await http
            .get(
              Uri.parse('${BackendConfig.baseUrl}/devices/$sn/waypoints'),
              headers: AuthService.bearerHeader,
            )
            .timeout(const Duration(seconds: 10));
        if (res.statusCode == 200) {
          final list = jsonDecode(res.body) as List;
          yield list.map((e) => Waypoint.fromJson(e as Map<String, dynamic>)).toList();
        } else {
          debugPrint(
              '[DeviceService] GET /devices/$sn/waypoints -> HTTP ${res.statusCode}: ${res.body}');
        }
      } catch (e) {
        debugPrint('[DeviceService] watchWaypoints($sn) gagal: $e');
      }
      await Future.delayed(_pollInterval);
    }
  }

  Future<void> addWaypoint({
    required String sn,
    required String name,
    required double lat,
    required double lng,
  }) async {
    final res = await http
        .post(
          Uri.parse('${BackendConfig.baseUrl}/devices/$sn/waypoints'),
          headers: _userJsonHeaders,
          body: jsonEncode({'name': name, 'lat': lat, 'lng': lng}),
        )
        .timeout(const Duration(seconds: 10));
    if (res.statusCode != 200) {
      throw Exception('Gagal menambah waypoint (HTTP ${res.statusCode}): ${res.body}');
    }
  }

  Future<void> removeWaypoint({required String sn, required String waypointId}) async {
    final res = await http
        .delete(
          Uri.parse('${BackendConfig.baseUrl}/devices/$sn/waypoints/$waypointId'),
          headers: AuthService.bearerHeader,
        )
        .timeout(const Duration(seconds: 10));
    if (res.statusCode != 204) {
      throw Exception('Gagal menghapus waypoint (HTTP ${res.statusCode}): ${res.body}');
    }
  }

  /// Riwayat posisi dalam [days] hari terakhir, terurut dari yang paling
  /// lama. Backend membatasi hasil MAKSIMAL 500 baris per request (lihat
  /// `backend/app/main.py:telemetry_history`) — kalau device mengirim sangat
  /// sering dan [days] besar, titik yang lebih tua dari 500 baris terbaru
  /// tidak akan ikut tampil (batasan backend, bukan bug di app).
  Stream<List<PositionPoint>> watchHistory({required String sn, required int days}) async* {
    final res = await http
        .get(
          Uri.parse('${BackendConfig.baseUrl}/telemetry/$sn/history?limit=500'),
          headers: _adminHeaders,
        )
        .timeout(const Duration(seconds: 15));
    if (res.statusCode != 200) {
      throw Exception('Gagal mengambil riwayat (HTTP ${res.statusCode})');
    }
    final cutoff = DateTime.now().subtract(Duration(days: days));
    final list = jsonDecode(res.body) as List;
    final points = list
        .map((e) => PositionPoint.fromJson(e as Map<String, dynamic>))
        .where((p) => p.timestamp.isAfter(cutoff))
        .toList()
      ..sort((a, b) => a.timestamp.compareTo(b.timestamp));
    yield points;
  }

}
