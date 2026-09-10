/// Backend FastAPI (gps.jejakpintar.cloud) — lihat FIRMWARE_CONTRACT.md dan
/// backend/README.md untuk detail kontrak lengkap.
///
/// PERINGATAN KEAMANAN: [adminApiKey] tertanam di client (bisa diekstrak dari
/// APK/bundle web) karena backend ini belum punya skema auth per-pengguna —
/// cuma dua API key statis (device & admin). Ini cukup untuk skripsi/demo,
/// tapi kalau backend dipakai lebih serius, tambahkan endpoint login/token
/// bergantung sesi di server supaya admin key tidak perlu ada di client.
class BackendConfig {
  static const String host = 'gps.jejakpintar.cloud';
  static const String baseUrl = 'https://$host';
  static const String deviceApiKey = 'krBixvn9dojknW2Lbj2rpDwuluEb61T/7D8V9HBanPM=';
  static const String adminApiKey = 'RTw5GgM3B+EhOvQnqfovOJxyJ4oqf1H+N6ajWCJHhwU=';
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
