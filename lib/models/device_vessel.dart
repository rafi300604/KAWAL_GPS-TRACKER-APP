import '../config/backend_config.dart';

class DeviceVessel {
  final String sn;
  final String name;
  final double lat;
  final double lng;
  final double heading;
  final double speedKnots;
  final String status; // 'active' | 'inactive' | 'unknown'
  final String powerSource;
  final String dataSource;
  final String? wifiSsid;
  final String? ipv4;
  final DateTime lastTransmittedAt;

  /// Modem GSM tidak punya cara membaca sisa kuota SIM asli dari operator,
  /// jadi firmware (skripsi.ino) tidak pernah kirim angka kuota - alih-alih,
  /// [dataSource] sendiri bisa berisi string deskriptif seperti
  /// "WIFI (GSM: kuota diduga habis)" saat firmware mendeteksi heuristik itu
  /// (sesi data GSM gagal berkali-kali padahal jaringan seluler terdaftar
  /// normal). Cocokkan kata kunci yang sama dengan QUOTA_WARNING_KEYWORD di
  /// `backend/app/main.py` supaya UI dan pembuatan notifikasi di backend
  /// selalu sepakat kapan ini dianggap "peringatan kuota" - lihat
  /// FIRMWARE_CONTRACT.md.
  bool get hasQuotaWarning => dataSource.toLowerCase().contains('kuota');

  /// Alat dianggap AKTIF kalau data terakhir masuk dalam 24 jam terakhir -
  /// dihitung dari [lastTransmittedAt] vs waktu sekarang, BUKAN dari field
  /// [status]. [status] datang dari backend FastAPI dan pada praktiknya
  /// SELALU null (firmware tidak pernah mengirim field ini di payload
  /// /telemetry), jadi kalau dipakai untuk indikator akan selalu jatuh ke
  /// "unknown"/abu-abu terlepas dari alat sebenarnya aktif atau tidak -
  /// itu sebabnya indikator status di UI pakai ini, bukan [status].
  bool get isActive => DateTime.now().difference(lastTransmittedAt) <= const Duration(hours: 24);

  const DeviceVessel({
    required this.sn,
    required this.name,
    required this.lat,
    required this.lng,
    required this.heading,
    required this.speedKnots,
    required this.status,
    required this.powerSource,
    required this.dataSource,
    this.wifiSsid,
    this.ipv4,
    required this.lastTransmittedAt,
  });

  /// Dari respons backend FastAPI (`GET /devices`, `GET /devices/{sn}`) —
  /// lihat `backend/app/schemas.py:DeviceState`. Backend ini tidak
  /// melaporkan `wifiSsid`/`ipv4` sama sekali, jadi keduanya selalu null
  /// (UI sudah menangani null itu dengan aman — lihat vessel_info_panel.dart).
  factory DeviceVessel.fromJson(Map<String, dynamic> json) {
    return DeviceVessel(
      sn: json['sn'] as String,
      name: json['name'] as String? ?? json['sn'] as String,
      lat: (json['lat'] as num?)?.toDouble() ?? 0,
      lng: (json['lng'] as num?)?.toDouble() ?? 0,
      heading: (json['heading'] as num?)?.toDouble() ?? 0,
      speedKnots: (json['speedKnots'] as num?)?.toDouble() ?? 0,
      status: json['status'] as String? ?? 'unknown',
      powerSource: json['powerSource'] as String? ?? '',
      dataSource: json['dataSource'] as String? ?? '',
      wifiSsid: json['wifiSsid'] as String?,
      ipv4: json['ipv4'] as String?,
      lastTransmittedAt: json['lastTransmittedAt'] != null
          ? parseBackendUtc(json['lastTransmittedAt'] as String)
          : DateTime.now(),
    );
  }
}
