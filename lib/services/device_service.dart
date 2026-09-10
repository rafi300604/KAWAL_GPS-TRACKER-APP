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

  // StreamController (bukan cuma Stream mentah dari async*) supaya
  // addWaypoint()/removeWaypoint() bisa DORONG hasil terbaru langsung ke
  // listener yang sudah ada (lewat _refreshWaypointsNow()) begitu POST/DELETE
  // sukses - tanpa ini, UI (panel ETA, rute di peta) baru ikut berubah di
  // tick polling berikutnya (bisa sampai 5 detik), kelihatan lambat/nge-lag
  // padahal datanya sudah tersimpan di backend. Tetap di-cache per sn (sama
  // seperti watchAllDevices()/watchDevice()) supaya rebuild widget tidak
  // memicu polling loop baru tiap kali.
  final Map<String, StreamController<List<Waypoint>>> _waypointControllers = {};

  // Salinan lokal daftar waypoint terakhir per sn - dipakai addWaypoint()/
  // removeWaypoint() supaya bisa langsung SISIPKAN/BUANG satu item dan
  // dorong hasilnya ke stream TANPA request GET tambahan (lihat
  // _pushWaypoints()). Selalu disinkronkan ulang dari respons polling asli
  // (_pollWaypointsInto()) supaya tidak pernah menyimpang dari backend kalau
  // ada perubahan dari sumber lain (device lain, sesi lain).
  final Map<String, List<Waypoint>> _waypointCache = {};

  // Stream.multi (bukan controller.stream langsung) - broadcast stream BIASA
  // tidak pernah memutar ulang event lama ke listener yang baru subscribe
  // belakangan. Itu bug nyata di sini: EtaPanel di home_screen baru
  // subscribe SETELAH _showEtaPanel di-set true, yaitu SETELAH
  // addWaypoint()/_pushWaypoints() sudah mendorong data terbaru ke
  // controller - jadi listener baru itu KELEWATAN dorongan itu dan sempat
  // nampilin "Belum ada waypoint" sampai event berikutnya datang (bisa
  // sampai 5 detik lagi), padahal datanya sendiri sudah ada dari awal.
  // Stream.multi menjalankan callback-nya per listener (tidak seperti
  // broadcast stream biasa), jadi listener BARU MANA PUN - kapan pun dia
  // subscribe - selalu langsung dapat _waypointCache[sn] (kalau ada) sebagai
  // event pertama, baru dilanjut forward event live dari controller.
  // Instance-nya di-cache per sn (sama seperti watchAllDevices()/
  // watchDevice()) supaya widget yang manggil watchWaypoints() tiap build()
  // tidak memicu resubscribe/listen-cancel berulang ke controller.
  final Map<String, Stream<List<Waypoint>>> _waypointBroadcastStreams = {};

  Stream<List<Waypoint>> watchWaypoints(String sn) {
    final controller = _waypointControllers.putIfAbsent(sn, () {
      final c = StreamController<List<Waypoint>>.broadcast();
      _pollWaypointsInto(sn, c);
      return c;
    });
    return _waypointBroadcastStreams.putIfAbsent(sn, () {
      return Stream<List<Waypoint>>.multi((emitter) {
        final cached = _waypointCache[sn];
        if (cached != null) emitter.add(cached);
        final sub = controller.stream.listen(
          emitter.add,
          onError: emitter.addError,
          onDone: emitter.close,
        );
        emitter.onCancel = sub.cancel;
      });
    });
  }

  Future<List<Waypoint>> _fetchWaypoints(String sn) async {
    final res = await http
        .get(
          Uri.parse('${BackendConfig.baseUrl}/devices/$sn/waypoints'),
          headers: AuthService.bearerHeader,
        )
        .timeout(const Duration(seconds: 10));
    if (res.statusCode != 200) {
      throw Exception(
          '[DeviceService] GET /devices/$sn/waypoints -> HTTP ${res.statusCode}: ${res.body}');
    }
    final list = jsonDecode(res.body) as List;
    return list.map((e) => Waypoint.fromJson(e as Map<String, dynamic>)).toList();
  }

  void _pollWaypointsInto(String sn, StreamController<List<Waypoint>> controller) async {
    while (!controller.isClosed) {
      try {
        final points = await _fetchWaypoints(sn);
        _waypointCache[sn] = points;
        if (!controller.isClosed) controller.add(points);
      } catch (e) {
        debugPrint('[DeviceService] watchWaypoints($sn) gagal: $e');
      }
      await Future.delayed(_pollInterval);
    }
  }

  /// Dorong daftar waypoint terbaru langsung ke stream yang sudah ada (kalau
  /// ada listener aktif) - tanpa request jaringan sama sekali, jadi UI
  /// (panel ETA, rute di peta) ikut berubah SAAT ITU JUGA begitu
  /// addWaypoint()/removeWaypoint() sukses, bukan nunggu tick polling
  /// berikutnya (bisa sampai 5 detik) ATAU nunggu satu GET tambahan.
  void _pushWaypoints(String sn, List<Waypoint> points) {
    _waypointCache[sn] = points;
    final controller = _waypointControllers[sn];
    if (controller != null && !controller.isClosed) controller.add(points);
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
    // Backend membalas objek waypoint yang baru dibuat (lengkap dengan id
    // dari server) - pakai itu langsung, tidak perlu GET ulang cuma untuk
    // tahu apa yang baru saja kita simpan sendiri.
    final created = Waypoint.fromJson(jsonDecode(res.body) as Map<String, dynamic>);
    final current = _waypointCache[sn] ?? const <Waypoint>[];
    _pushWaypoints(sn, [...current, created]);
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
    final current = _waypointCache[sn] ?? const <Waypoint>[];
    _pushWaypoints(sn, current.where((w) => w.id != waypointId).toList());
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
