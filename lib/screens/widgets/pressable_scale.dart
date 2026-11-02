import 'package:flutter/material.dart';

/// Membungkus [child] dengan animasi "mengecil sedikit lalu kembali" saat
/// ditekan (umum dipakai app modern buat feedback taktil tombol, di luar
/// ripple/splash bawaan Material) - dipakai di tombol Waypoint/ETA/History,
/// kontrol TrackingToolbar, dan marker device supaya semua terasa "hidup"
/// saat disentuh, bukan cuma ripple datar bawaan Flutter.
///
/// Membungkus (bukan mengganti) [child] - [child] tetap widget asli (mis.
/// FilledButton) dengan semua logika onPressed-nya sendiri; wrapper ini cuma
/// mendeteksi tap-down/up/cancel lewat [Listener] (tidak ikut menyerap
/// gesture dari child) untuk memicu animasi skala di atasnya.
class PressableScale extends StatefulWidget {
  final Widget child;
  final double scaleDown;

  const PressableScale({
    super.key,
    required this.child,
    this.scaleDown = 0.94,
  });

  @override
  State<PressableScale> createState() => _PressableScaleState();
}

class _PressableScaleState extends State<PressableScale> {
  bool _pressed = false;

  void _setPressed(bool value) {
    if (_pressed != value) setState(() => _pressed = value);
  }

  @override
  Widget build(BuildContext context) {
    return Listener(
      onPointerDown: (_) => _setPressed(true),
      onPointerUp: (_) => _setPressed(false),
      onPointerCancel: (_) => _setPressed(false),
      child: AnimatedScale(
        scale: _pressed ? widget.scaleDown : 1.0,
        duration: const Duration(milliseconds: 120),
        curve: Curves.easeOut,
        child: widget.child,
      ),
    );
  }
}
