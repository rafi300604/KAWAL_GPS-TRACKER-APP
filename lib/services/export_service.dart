import 'dart:typed_data';

import 'package:excel/excel.dart';
import 'package:share_plus/share_plus.dart';

import '../models/position_point.dart';
import '../utils/formatters.dart';

/// Builds tracking-history exports as .xlsx and hands them to the platform
/// share sheet — the mobile equivalent of the reference app's browser-
/// download Excel export (vue-json-excel3). Built from in-memory bytes via
/// [XFile.fromData] so the same code works on mobile (native share sheet)
/// and web (browser download), with no platform-specific file I/O.
class ExportService {
  Future<void> exportTrackingHistory({
    required List<PositionPoint> points,
    required String deviceName,
    required String deviceSn,
    required int days,
    String? devicePowerSource,
  }) async {
    final excel = Excel.createExcel();
    final sheetName = excel.getDefaultSheet()!;
    final sheet = excel[sheetName];

    // DeviceVessel.powerSource default ke string kosong (bukan null) kalau
    // backend tidak pernah melaporkannya - treat itu sama seperti "tidak
    // ada fallback" (bukan fallback ke string kosong, yang bikin selnya
    // kelihatan kosong padahal maksudnya "tidak diketahui").
    final fallbackPowerSource =
        (devicePowerSource != null && devicePowerSource.isNotEmpty) ? devicePowerSource : null;

    sheet.appendRow([
      TextCellValue('Kapal'),
      TextCellValue('SN'),
      TextCellValue('Waktu'),
      TextCellValue('Lat'),
      TextCellValue('Lng'),
      TextCellValue('Kecepatan (km/jam)'),
      TextCellValue('Arah (derajat)'),
      TextCellValue('Sumber Daya'),
      TextCellValue('Sumber Data'),
    ]);

    for (final p in points) {
      sheet.appendRow([
        TextCellValue(deviceName),
        TextCellValue(deviceSn),
        TextCellValue(dateTimeFormat.format(p.timestamp)),
        DoubleCellValue(p.lat),
        DoubleCellValue(p.lng),
        DoubleCellValue(p.speedKnots * 1.852), // knot -> km/jam, sama seperti tampilan UI
        DoubleCellValue(p.heading),
        // p.powerSource null berarti titik ini direkam SEBELUM backend mulai
        // menyimpan power_source per-transmisi (migrasi 0004) - bukan
        // berarti device-nya tidak punya sumber daya. Sumber dayanya HAMPIR
        // PASTI belum berubah dari yang sekarang diketahui (device fisik
        // tetap terpasang ke sumber daya yang sama, tidak berpindah-pindah
        // per transmisi) - jadi fallback ke [fallbackPowerSource] (nilai
        // terkini device) jauh lebih berguna daripada "-" polos, yang
        // kelihatan seperti data hilang padahal cuma belum pernah dicatat.
        TextCellValue(p.powerSource ?? fallbackPowerSource ?? '-'),
        TextCellValue(p.dataSource ?? '-'),
      ]);
    }

    final bytes = excel.encode();
    if (bytes == null) return;

    final timestamp = DateTime.now().millisecondsSinceEpoch;
    final fileName = 'analisa-tracking-$deviceSn-${days}hari-$timestamp.xlsx';
    final xFile = XFile.fromData(
      Uint8List.fromList(bytes),
      name: fileName,
      mimeType: 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet',
    );

    await Share.shareXFiles(
      [xFile],
      text: 'Riwayat tracking $deviceName ($days hari)',
    );
  }
}
