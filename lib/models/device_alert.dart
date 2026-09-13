import '../config/backend_config.dart';

/// Notifikasi otomatis device (mis. "kuota SIM menipis") - dibuat backend
/// sendiri (lihat `backend/app/main.py:ingest_telemetry`), bukan ditulis
/// langsung oleh firmware. Dari `GET /devices/{sn}/alerts`.
class DeviceAlert {
  final String id;
  final String deviceSn;
  final String type;
  final String message;
  final double? value;
  final DateTime createdAt;

  const DeviceAlert({
    required this.id,
    required this.deviceSn,
    required this.type,
    required this.message,
    required this.value,
    required this.createdAt,
  });

  factory DeviceAlert.fromJson(Map<String, dynamic> json) {
    return DeviceAlert(
      id: json['id'].toString(),
      deviceSn: json['deviceSn'] as String? ?? '',
      type: json['type'] as String? ?? '',
      message: json['message'] as String? ?? '',
      value: (json['value'] as num?)?.toDouble(),
      createdAt: json['createdAt'] != null
          ? parseBackendUtc(json['createdAt'] as String)
          : DateTime.now(),
    );
  }
}
