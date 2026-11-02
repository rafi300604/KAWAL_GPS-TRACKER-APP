import 'package:flutter/material.dart';

/// Panel (ETA, tracking toolbar, dll) muncul/hilang dengan geser+fade halus,
/// bukan langsung "pop" instan lewat `if (condition) Widget` biasa. Dipakai
/// di home_screen.dart menggantikan `if` langsung di dalam list children -
/// [child] selalu widget yang SAMA tipenya selama [visible] true, jadi
/// AnimatedSwitcher TIDAK ikut retrigger animasi tiap kali datanya berubah
/// (mis. posisi waypoint ETA ter-update) - cuma saat [visible] sendiri
/// berubah (panelnya baru dibuka/ditutup).
class AnimatedPanel extends StatelessWidget {
  final bool visible;
  final Widget child;

  const AnimatedPanel({super.key, required this.visible, required this.child});

  @override
  Widget build(BuildContext context) {
    return AnimatedSwitcher(
      duration: const Duration(milliseconds: 260),
      switchInCurve: Curves.easeOutCubic,
      switchOutCurve: Curves.easeInCubic,
      transitionBuilder: (transitionChild, animation) => FadeTransition(
        opacity: animation,
        child: SlideTransition(
          position: Tween<Offset>(begin: const Offset(0, 0.15), end: Offset.zero)
              .animate(animation),
          child: transitionChild,
        ),
      ),
      child: visible ? child : const SizedBox.shrink(),
    );
  }
}
