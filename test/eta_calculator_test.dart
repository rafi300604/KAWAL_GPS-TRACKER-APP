import 'package:flutter_test/flutter_test.dart';
import 'package:skripsi_gps_tracker/services/eta_calculator.dart';

void main() {
  group('computeDistanceNm', () {
    test('same point is zero distance', () {
      expect(computeDistanceNm(-6.2, 106.8, -6.2, 106.8), closeTo(0, 1e-9));
    });

    test('1 degree of latitude is ~60 NM', () {
      final nm = computeDistanceNm(0, 0, 1, 0);
      expect(nm, closeTo(60.0, 1.0));
    });
  });

  group('computeDistanceKm', () {
    test('same point is zero distance', () {
      expect(computeDistanceKm(-6.2, 106.8, -6.2, 106.8), closeTo(0, 1e-9));
    });

    test('1 degree of latitude is ~111.2 km', () {
      final km = computeDistanceKm(0, 0, 1, 0);
      expect(km, closeTo(111.2, 1.0));
    });

    test('is 1.852x the nautical-mile distance', () {
      final km = computeDistanceKm(0, 0, 1, 1);
      final nm = computeDistanceNm(0, 0, 1, 1);
      expect(km, closeTo(nm * 1.852, 1e-6));
    });
  });

  group('computeStraightLineEtaKm', () {
    test('zero speed yields an unreachable ETA with no arrival time', () {
      final result = computeStraightLineEtaKm(
        fromLat: 0,
        fromLng: 0,
        toLat: 1,
        toLng: 0,
        speedKnots: 0,
      );
      expect(result.hours, double.infinity);
      expect(result.arrival, isNull);
      expect(result.level, EtaLevel.bad);
    });

    test('distanceKm reflects km, not nautical miles', () {
      final result = computeStraightLineEtaKm(
        fromLat: 0,
        fromLng: 0,
        toLat: 1,
        toLng: 0,
        speedKnots: 60,
      );
      expect(result.distanceKm, closeTo(111.2, 1.0));
    });

    test('speed in knots is converted to km/h for travel time', () {
      final now = DateTime(2026, 1, 1, 0, 0, 0);
      // ~111.2 km at 60 knots (~111.12 km/h) should take ~1 hour.
      final result = computeStraightLineEtaKm(
        fromLat: 0,
        fromLng: 0,
        toLat: 1,
        toLng: 0,
        speedKnots: 60,
        now: now,
      );
      expect(result.hours, closeTo(1.0, 0.05));
      expect(result.arrival, isNotNull);
    });
  });

  group('formatDuration', () {
    test('under an hour formats as minutes', () {
      expect(formatDuration(0.5), '30 menit');
    });

    test('under a day formats as hours and minutes', () {
      expect(formatDuration(2.25), '2j 15m');
    });

    test('a day or more formats as days and hours', () {
      expect(formatDuration(26.0), '1h 2j');
    });

    test('non-finite duration is a dash', () {
      expect(formatDuration(double.infinity), '-');
    });
  });

  group('etaLevelForHours', () {
    test('<=30 minutes is good', () {
      expect(etaLevelForHours(0.4), EtaLevel.good);
    });

    test('<=180 minutes is warn', () {
      expect(etaLevelForHours(2.5), EtaLevel.warn);
    });

    test('>180 minutes is bad', () {
      expect(etaLevelForHours(4.0), EtaLevel.bad);
    });

    test('unreachable (zero speed) is bad', () {
      expect(etaLevelForHours(double.infinity), EtaLevel.bad);
    });
  });

  group('computeEta', () {
    test('zero speed yields an unreachable ETA with no arrival time', () {
      final result = computeEta(
        fromLat: 0,
        fromLng: 0,
        toLat: 1,
        toLng: 0,
        speedKnots: 0,
      );
      expect(result.hours, double.infinity);
      expect(result.arrival, isNull);
      expect(result.level, EtaLevel.bad);
    });

    test('positive speed yields a finite ETA and arrival time', () {
      final now = DateTime(2026, 1, 1, 0, 0, 0);
      final result = computeEta(
        fromLat: 0,
        fromLng: 0,
        toLat: 1,
        toLng: 0,
        speedKnots: 60,
        now: now,
      );
      expect(result.hours, closeTo(1.0, 0.05));
      expect(result.arrival, isNotNull);
      expect(result.arrival!.isAfter(now), isTrue);
    });
  });

  group('convertDmsToDecimal', () {
    test('converts a northern/eastern coordinate to positive decimal', () {
      final value = convertDmsToDecimal(
        degrees: 6,
        minutes: 12,
        seconds: 0,
        direction: 'N',
      );
      expect(value, closeTo(6.2, 1e-6));
    });

    test('south/west directions negate the result', () {
      final south = convertDmsToDecimal(
        degrees: 6,
        minutes: 12,
        seconds: 0,
        direction: 'S',
      );
      expect(south, closeTo(-6.2, 1e-6));

      final west = convertDmsToDecimal(
        degrees: 106,
        minutes: 48,
        seconds: 0,
        direction: 'W',
      );
      expect(west, closeTo(-106.8, 1e-6));
    });

    test('rejects out-of-range degrees', () {
      expect(
        () => convertDmsToDecimal(degrees: 200, minutes: 0, seconds: 0, direction: 'N'),
        throwsFormatException,
      );
    });

    test('rejects out-of-range minutes', () {
      expect(
        () => convertDmsToDecimal(degrees: 10, minutes: 60, seconds: 0, direction: 'N'),
        throwsFormatException,
      );
    });

    test('rejects out-of-range seconds', () {
      expect(
        () => convertDmsToDecimal(degrees: 10, minutes: 0, seconds: 60, direction: 'N'),
        throwsFormatException,
      );
    });

    test('rejects an invalid direction', () {
      expect(
        () => convertDmsToDecimal(degrees: 10, minutes: 0, seconds: 0, direction: 'X'),
        throwsFormatException,
      );
    });
  });
}
