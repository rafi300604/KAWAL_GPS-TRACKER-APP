import 'package:flutter_test/flutter_test.dart';
import 'package:skripsi_gps_tracker/utils/formatters.dart';

void main() {
  group('formatWib', () {
    test('converts a UTC instant to WIB (UTC+7)', () {
      expect(formatWib(DateTime.utc(2026, 10, 20, 12, 53, 0)), '2026-10-20 19:53:00 WIB');
    });

    test('rolls over to the next day', () {
      expect(formatWib(DateTime.utc(2026, 10, 18, 21, 59, 30)), '2026-10-19 04:59:30 WIB');
    });
  });

  group('utcStampsToWib', () {
    test('rewrites the UTC stamp in an old tamper alert message', () {
      expect(
        utcStampsToWib('Device ESP32-01 diusik (tamper) pada 2026-10-20 12:53:00 UTC'),
        'Device ESP32-01 diusik (tamper) pada 2026-10-20 19:53:00 WIB',
      );
    });

    test('leaves messages without a UTC stamp (incl. new WIB ones) unchanged', () {
      const quota = 'Device ESP32-01 melaporkan: GSM kuota habis';
      const wib = 'Device ESP32-01 diusik (tamper) pada 2026-10-20 19:53:00 WIB';
      expect(utcStampsToWib(quota), quota);
      expect(utcStampsToWib(wib), wib);
    });
  });
}
