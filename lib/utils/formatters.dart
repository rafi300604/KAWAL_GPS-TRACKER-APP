import 'package:intl/intl.dart';

final DateFormat dateTimeFormat = DateFormat('yyyy-MM-dd HH:mm:ss');
final DateFormat shortDateTimeFormat = DateFormat('yyyy-MM-dd | HH:mm');

String formatCoordinate(double value) => value.toStringAsFixed(6);

// Waktu notifikasi/alarm SELALU ditampilkan dalam WIB (UTC+7; Indonesia tidak
// pakai DST), terlepas dari zona waktu perangkat/browser yang membuka app.
const Duration _wibOffset = Duration(hours: 7);

/// [t] (UTC maupun lokal) sebagai jam dinding WIB.
DateTime toWib(DateTime t) => t.toUtc().add(_wibOffset);

/// "yyyy-MM-dd HH:mm:ss WIB".
String formatWib(DateTime t) => '${dateTimeFormat.format(toWib(t))} WIB';

final RegExp _utcStamp = RegExp(r'(\d{4}-\d{2}-\d{2}) (\d{2}:\d{2}:\d{2}) UTC');

/// Mengganti stempel "yyyy-MM-dd HH:mm:ss UTC" di dalam teks notifikasi
/// (ditulis backend versi lama, dan tersimpan di riwayat) menjadi WIB.
String utcStampsToWib(String text) => text.replaceAllMapped(
      _utcStamp,
      (m) => formatWib(DateTime.parse('${m[1]}T${m[2]}Z')),
    );
