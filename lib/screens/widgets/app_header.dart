import 'package:flutter/material.dart';

/// Top header bar — matches the reference app's `.header`/`.header-left`/
/// `.header-right` layout: logo + app name on the left, account pill +
/// logout button on the right. Always visible, part of the fixed page
/// chrome (not an AppBar/Drawer), matching the reference's persistent
/// desktop header.
///
/// On narrow (phone) screens, set [isCompact] and [onMenuPressed]: the
/// account pill collapses to just the avatar, the logo/title shrink, and a
/// hamburger button appears to open the device list — the reference's
/// desktop header simply doesn't fit a phone's width otherwise (this is
/// what caused the header to overflow on Android/iOS).
class AppHeader extends StatelessWidget {
  final String appName;
  final String userName;
  final String userRole;
  final bool isAdmin;
  final ThemeMode themeMode;
  final ValueChanged<ThemeMode> onThemeModeChanged;
  final VoidCallback onOpenAdmin;
  final VoidCallback onLogout;
  final bool isCompact;
  final VoidCallback? onMenuPressed;

  const AppHeader({
    super.key,
    required this.appName,
    required this.userName,
    required this.userRole,
    required this.isAdmin,
    required this.themeMode,
    required this.onThemeModeChanged,
    required this.onOpenAdmin,
    required this.onLogout,
    this.isCompact = false,
    this.onMenuPressed,
  });

  IconData get _themeIcon => switch (themeMode) {
        ThemeMode.light => Icons.light_mode_outlined,
        ThemeMode.dark => Icons.dark_mode_outlined,
        ThemeMode.system => Icons.brightness_auto_outlined,
      };

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final initial = userName.isNotEmpty ? userName[0].toUpperCase() : '?';
    final logoSize = isCompact ? 36.0 : 44.0;

    return Container(
      height: 64,
      padding: EdgeInsets.symmetric(horizontal: isCompact ? 10 : 20),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface,
        borderRadius: BorderRadius.circular(20),
        boxShadow: const [BoxShadow(color: Colors.black12, blurRadius: 12)],
      ),
      child: Row(
        children: [
          if (onMenuPressed != null)
            IconButton(
              tooltip: 'Daftar Alat',
              onPressed: onMenuPressed,
              icon: const Icon(Icons.menu),
            ),
          ClipRRect(
            borderRadius: BorderRadius.circular(12),
            child: Image.asset(
              'assets/icons/kawal_icon.png',
              width: logoSize,
              height: logoSize,
              fit: BoxFit.cover,
            ),
          ),
          SizedBox(width: isCompact ? 10 : 16),
          Expanded(
            child: Text(
              appName,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: (isCompact ? theme.textTheme.titleSmall : theme.textTheme.titleLarge)
                  ?.copyWith(fontWeight: FontWeight.w700, letterSpacing: 0.6),
            ),
          ),
          PopupMenuButton<ThemeMode>(
            tooltip: 'Tema tampilan',
            icon: Icon(_themeIcon),
            initialValue: themeMode,
            onSelected: onThemeModeChanged,
            itemBuilder: (context) => [
              _themeMenuItem(ThemeMode.light, Icons.light_mode_outlined, 'Terang'),
              _themeMenuItem(ThemeMode.dark, Icons.dark_mode_outlined, 'Gelap'),
              _themeMenuItem(ThemeMode.system, Icons.brightness_auto_outlined, 'Ikuti Sistem'),
            ],
          ),
          if (isAdmin)
            IconButton(
              tooltip: 'Kelola Pengguna',
              onPressed: onOpenAdmin,
              icon: const Icon(Icons.admin_panel_settings_outlined),
            ),
          SizedBox(width: isCompact ? 4 : 8),
          if (isCompact)
            Tooltip(
              message: '$userName${userRole.isNotEmpty ? ' ($userRole)' : ''}',
              child: CircleAvatar(
                radius: 16,
                backgroundColor: theme.colorScheme.primary,
                child: Text(initial,
                    style: TextStyle(
                        color: theme.colorScheme.onPrimary, fontWeight: FontWeight.w700)),
              ),
            )
          else
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
              decoration: BoxDecoration(
                color: theme.colorScheme.primaryContainer.withValues(alpha: 0.5),
                borderRadius: BorderRadius.circular(999),
                border: Border.all(color: theme.colorScheme.primary.withValues(alpha: 0.2)),
              ),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  CircleAvatar(
                    radius: 16,
                    backgroundColor: theme.colorScheme.primary,
                    child: Text(initial,
                        style: TextStyle(
                            color: theme.colorScheme.onPrimary, fontWeight: FontWeight.w700)),
                  ),
                  const SizedBox(width: 10),
                  Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(userName,
                          style:
                              theme.textTheme.bodyMedium?.copyWith(fontWeight: FontWeight.w700)),
                      Text(userRole.toUpperCase(),
                          style: theme.textTheme.bodySmall?.copyWith(
                              color: theme.colorScheme.primary, fontWeight: FontWeight.w600)),
                    ],
                  ),
                ],
              ),
            ),
          SizedBox(width: isCompact ? 4 : 12),
          IconButton.filledTonal(
            tooltip: 'Keluar',
            onPressed: onLogout,
            style: IconButton.styleFrom(
              backgroundColor: theme.colorScheme.errorContainer,
              foregroundColor: theme.colorScheme.onErrorContainer,
            ),
            icon: const Icon(Icons.logout),
          ),
        ],
      ),
    );
  }

  PopupMenuItem<ThemeMode> _themeMenuItem(ThemeMode value, IconData icon, String label) {
    final isSelected = value == themeMode;
    return PopupMenuItem(
      value: value,
      child: Row(
        children: [
          Icon(icon, size: 18),
          const SizedBox(width: 10),
          Text(label),
          if (isSelected) ...[
            const Spacer(),
            const Icon(Icons.check, size: 18),
          ],
        ],
      ),
    );
  }
}
