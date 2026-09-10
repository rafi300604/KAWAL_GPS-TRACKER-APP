import 'package:flutter/material.dart';

/// Standard header for a floating feature panel: an optional leading icon, a
/// title, an optional trailing action (e.g. "Tambah"), and an always-present
/// close button. [onClose] is called directly — these panels are floating
/// overlays on the map now (matching the reference app), not modal routes,
/// so there is nothing to `Navigator.pop`.
class PanelHeader extends StatelessWidget {
  final String title;
  final IconData? icon;
  final Widget? leading;
  final Widget? trailing;
  final VoidCallback onClose;
  final Color? foregroundColor;

  const PanelHeader({
    super.key,
    required this.title,
    required this.onClose,
    this.icon,
    this.leading,
    this.trailing,
    this.foregroundColor,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final fg = foregroundColor ?? theme.textTheme.titleLarge?.color;
    return Row(
      children: [
        if (leading != null) ...[
          leading!,
          const SizedBox(width: 8),
        ] else if (icon != null) ...[
          Icon(icon, color: foregroundColor ?? theme.colorScheme.primary),
          const SizedBox(width: 8),
        ],
        Expanded(
          child: Text(title, style: theme.textTheme.titleLarge?.copyWith(color: fg)),
        ),
        if (trailing != null) ...[trailing!, const SizedBox(width: 4)],
        IconButton(
          tooltip: 'Tutup',
          onPressed: onClose,
          icon: Icon(Icons.close, color: foregroundColor),
        ),
      ],
    );
  }
}
