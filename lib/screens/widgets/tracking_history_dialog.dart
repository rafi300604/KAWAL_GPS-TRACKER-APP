import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../models/device_vessel.dart';
import '../../services/device_service.dart';

/// Centered modal matching the reference app's `TrackingHistoryKapal.vue`:
/// pick a day range (1-30) and fetch. This dialog only fetches — the
/// resulting points are handed back via [Navigator.pop] and the slider/
/// playback UI lives inline in the map's bottom toolbar (in `HomeScreen`),
/// exactly like the reference keeps that state in the parent, not the modal.
class TrackingHistoryDialog extends StatefulWidget {
  final DeviceVessel vessel;
  final DeviceService deviceService;

  const TrackingHistoryDialog({
    super.key,
    required this.vessel,
    required this.deviceService,
  });

  @override
  State<TrackingHistoryDialog> createState() => _TrackingHistoryDialogState();
}

class _TrackingHistoryDialogState extends State<TrackingHistoryDialog> {
  static const int _minDays = 1;
  static const int _maxDays = 30;

  int _days = 1;
  bool _loading = false;
  String? _error;

  // Input hari bisa diisi DUA cara: tombol +/- stepper, ATAU ketik langsung
  // di field-nya - keduanya harus tetap sinkron satu sama lain.
  late final TextEditingController _daysController =
      TextEditingController(text: '$_days');
  final FocusNode _daysFocusNode = FocusNode();

  @override
  void initState() {
    super.initState();
    // Waktu field kehilangan fokus (user pindah ke field lain / tap di luar),
    // rapikan: kalau kosong atau di luar rentang 1-30, kembalikan ke nilai
    // valid terdekat supaya tidak ada state "rusak" yang kelihatan diam-diam
    // lolos ke _fetch().
    _daysFocusNode.addListener(() {
      if (!_daysFocusNode.hasFocus) _applyDays(_days);
    });
  }

  @override
  void dispose() {
    _daysController.dispose();
    _daysFocusNode.dispose();
    super.dispose();
  }

  /// Set [_days] ke [value] yang sudah di-clamp ke [_minDays]..[_maxDays],
  /// dan samakan tampilan teks di field-nya. Dipakai oleh tombol +/- MAUPUN
  /// buat merapikan input manual saat field kehilangan fokus.
  void _applyDays(int value) {
    final clamped = value.clamp(_minDays, _maxDays);
    setState(() => _days = clamped);
    _daysController.text = '$clamped';
    _daysController.selection = TextSelection.collapsed(offset: _daysController.text.length);
  }

  Future<void> _fetch() async {
    // Jaga-jaga terakhir: kalau field manual masih fokus & belum sempat
    // di-blur (mis. langsung tap Fetch dari keyboard), pastikan _days tetap
    // sudah di-clamp ke rentang valid sebelum benar-benar fetch.
    _applyDays(_days);
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final points = await widget.deviceService
          .watchHistory(sn: widget.vessel.sn, days: _days)
          .first;
      if (mounted) Navigator.of(context).pop(points);
    } catch (e) {
      if (mounted) setState(() => _error = 'Gagal mengambil history: $e');
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('Fleet Tracking History'),
      content: SizedBox(
        width: 360,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(widget.vessel.name,
                style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 16),
            // Label di baris sendiri, stepper (-/angka/+) di baris terpisah
            // di bawahnya - bukan satu Row sempit yang memuat semuanya
            // sekaligus, supaya tidak overflow di dialog lebar terbatas
            // (sudah terbukti overflow waktu label+2 IconButton+angka
            // dipaksa muat satu baris).
            const Text('Pilih rentang waktu:'),
            const SizedBox(height: 4),
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                IconButton(
                  tooltip: 'Kurangi',
                  onPressed:
                      (_loading || _days <= _minDays) ? null : () => _applyDays(_days - 1),
                  icon: const Icon(Icons.remove_circle_outline),
                ),
                SizedBox(
                  width: 64,
                  child: TextField(
                    controller: _daysController,
                    focusNode: _daysFocusNode,
                    enabled: !_loading,
                    textAlign: TextAlign.center,
                    style: Theme.of(context).textTheme.titleMedium,
                    keyboardType: TextInputType.number,
                    inputFormatters: [
                      FilteringTextInputFormatter.digitsOnly,
                      LengthLimitingTextInputFormatter(2), // maks 30 - 2 digit cukup
                    ],
                    decoration: const InputDecoration(
                      isDense: true,
                      contentPadding: EdgeInsets.symmetric(vertical: 8),
                      border: OutlineInputBorder(),
                    ),
                    // Nilai di-apply live sambil ngetik (bukan tunggu blur)
                    // supaya tombol +/- di sebelahnya langsung ikut update
                    // enabled/disabled-nya - tapi teks di field TIDAK
                    // dipaksa ditulis ulang di sini (beda dari _applyDays)
                    // supaya kursor tidak lompat-lompat saat sedang mengetik.
                    // Perapian akhir (clamp + tulis ulang teks) baru terjadi
                    // saat field kehilangan fokus, lewat listener di initState.
                    onChanged: (v) {
                      final parsed = int.tryParse(v);
                      if (parsed != null) setState(() => _days = parsed);
                    },
                    onSubmitted: (_) => _applyDays(_days),
                  ),
                ),
                IconButton(
                  tooltip: 'Tambah',
                  onPressed:
                      (_loading || _days >= _maxDays) ? null : () => _applyDays(_days + 1),
                  icon: const Icon(Icons.add_circle_outline),
                ),
              ],
            ),
            Center(
              child: Text(
                'Maksimal 30 hari',
                style: Theme.of(context)
                    .textTheme
                    .bodySmall
                    ?.copyWith(color: Theme.of(context).colorScheme.onSurfaceVariant),
              ),
            ),
            if (_error != null) ...[
              const SizedBox(height: 12),
              Text(_error!, style: TextStyle(color: Theme.of(context).colorScheme.error)),
            ],
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: _loading ? null : () => Navigator.of(context).pop(),
          child: const Text('Batal'),
        ),
        FilledButton(
          onPressed: _loading ? null : _fetch,
          child: _loading
              ? const SizedBox(
                  width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
              : const Text('Fetch'),
        ),
      ],
    );
  }
}
