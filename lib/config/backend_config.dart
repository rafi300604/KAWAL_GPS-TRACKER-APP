/// Backend FastAPI (gps.jejakpintar.cloud) — lihat FIRMWARE_CONTRACT.md dan
/// backend/README.md untuk detail kontrak lengkap.
///
/// PERINGATAN KEAMANAN: [adminApiKey] tertanam di client (bisa diekstrak dari
/// APK/bundle web) karena backend ini belum punya skema auth per-pengguna —
/// cuma dua API key statis (device & admin). Ini cukup untuk skripsi/demo,
/// tapi kalau backend dipakai lebih serius, tambahkan endpoint login/token
/// bergantung sesi di server supaya admin key tidak perlu ada di client.
class BackendConfig {
  static const String host = 'mqttskripsigps.jejakpintar.cloud';
  static const String baseUrl = 'https://$host';
  // Kunci API TIDAK ditulis di source code: dibaca saat build dari secrets.json
  // (di-.gitignore) lewat `--dart-define-from-file=secrets.json`. Contoh isi:
  // secrets.example.json. Tanpa file itu, permintaan ke backend akan ditolak (401).
  static const String deviceApiKey = String.fromEnvironment('DEVICE_API_KEY');
  static const String adminApiKey = String.fromEnvironment('ADMIN_API_KEY');
}

/// Parse timestamp dari respons backend sebagai UTC secara eksplisit.
///
/// Firmware selalu mengirim timestamp dengan akhiran "Z" (UTC), tapi kolom
/// database bisa jadi menyimpannya sebagai naive timestamp — kalau begitu
/// backend akan mengembalikannya TANPA offset zona waktu sama sekali, dan
/// `DateTime.parse` polos di Dart akan salah mengira itu waktu LOKAL, bukan
/// UTC (selisih beberapa jam tergantung zona waktu perangkat). Untuk
/// mencegah itu, tambahkan "Z" kalau string belum punya penanda zona waktu.
DateTime parseBackendUtc(String value) {
  final hasZone = RegExp(r'([Zz]|[+-]\d{2}:?\d{2})$').hasMatch(value);
  return DateTime.parse(hasZone ? value : '${value}Z').toLocal();
}
