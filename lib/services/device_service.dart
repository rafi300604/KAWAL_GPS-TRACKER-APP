import 'dart:async';
import 'dart:convert';
import 'dart:math';

import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;

import '../config/backend_config.dart';
import '../models/device_vessel.dart';
import '../models/position_point.dart';
import '../models/waypoint.dart';

/// Posisi/status device (live + riwayat) datang dari backend FastAPI sendiri
/// (`gps.jejakpintar.cloud`, lihat FIRMWARE_CONTRACT.md) — ini yang ditulis
/// firmware ESP32 lewat GSM/WiFi. Waypoints TETAP di Firestore
/// (`devices/{sn}/waypoints`) karena backend baru tidak punya endpoint itu
/// sama sekali dan waypoints murni fitur sisi-app, tidak pernah disentuh
/// firmware.
class DeviceService {
  final FirebaseFirestore _db = FirebaseFirestore.instance;

  static const _pollInterval = Duration(seconds: 5);

  CollectionReference<Map<String, dynamic>> get _devicesRef =>
      _db.collection('devices');

  Map<String, String> get _adminHeaders => {
        'X-Admin-Key': BackendConfig.adminApiKey,
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

  Stream<List<Waypoint>> watchWaypoints(String sn) {
    return _devicesRef
        .doc(sn)
        .collection('waypoints')
        .orderBy('createdAt')
        .snapshots()
        .map((snap) => snap.docs.map(Waypoint.fromFirestore).toList());
  }

  Future<void> addWaypoint({
    required String sn,
    required String name,
    required double lat,
    required double lng,
  }) {
    final waypoint = Waypoint(
      id: '',
      name: name,
      lat: lat,
      lng: lng,
      createdAt: DateTime.now(),
    );
    return _devicesRef.doc(sn).collection('waypoints').add(waypoint.toFirestore());
  }

  Future<void> removeWaypoint({required String sn, required String waypointId}) {
    return _devicesRef.doc(sn).collection('waypoints').doc(waypointId).delete();
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

  /// Debug-only demo aid: nudges the primary device's position a small
  /// random step by POSTing to the same `/telemetry` endpoint the real
  /// firmware uses, so the app is demoable without the ESP32 hardware. See
  /// FIRMWARE_CONTRACT.md for the real contract.
  Future<void> simulateStep(DeviceVessel current) async {
    final rand = Random();
    final newLat = current.lat + (rand.nextDouble() - 0.5) * 0.01;
    final newLng = current.lng + (rand.nextDouble() - 0.5) * 0.01;
    final newHeading = (current.heading + (rand.nextDouble() - 0.5) * 30) % 360;
    final newSpeed = (5 + rand.nextDouble() * 10).toDouble();

    final res = await http
        .post(
          Uri.parse('${BackendConfig.baseUrl}/telemetry'),
          headers: {
            'Content-Type': 'application/json',
            'X-API-Key': BackendConfig.deviceApiKey,
          },
          body: jsonEncode({
            'sn': current.sn,
            'lat': newLat,
            'lng': newLng,
            'heading': newHeading,
            'speedKnots': newSpeed,
            'timestamp': DateTime.now().toUtc().toIso8601String(),
            'name': current.name,
            'powerSource': current.powerSource.isEmpty ? 'BATTERY' : current.powerSource,
            'dataSource': 'WIFI',
            'status': 'active',
          }),
        )
        .timeout(const Duration(seconds: 10));

    if (res.statusCode != 200) {
      throw Exception('Simulasi gagal (HTTP ${res.statusCode}): ${res.body}');
    }
  }
}
