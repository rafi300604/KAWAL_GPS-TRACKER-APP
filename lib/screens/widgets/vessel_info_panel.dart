import 'dart:async';

import 'package:flutter/material.dart';

import '../../models/device_vessel.dart';
import '../../utils/compass.dart';
import '../../utils/formatters.dart';
import '../../utils/time_ago.dart';
import 'panel_header.dart';

/// Floating "Panel Informasi Kapal" — mirrors the reference app's
/// sidebar-embedded info card (gradient header, detail rows) but as a
/// floating overlay on the map, since this is a phone app. Shown
/// automatically whenever a device is selected; [onClose] fully clears the
/// selection (matching the reference's `clearSelectedShip`), it does not
/// just hide this one card.
class VesselInfoPanel extends StatefulWidget {
  final DeviceVessel vessel;
  final VoidCallback onClose;

  const VesselInfoPanel({super.key, required this.vessel, required this.onClose});

  @override
  State<VesselInfoPanel> createState() => _VesselInfoPanelState();
}

class _VesselInfoPanelState extends State<VesselInfoPanel> {
  late final Timer _ticker;

  @override
  void initState() {
    super.initState();
    // Re-render every second so "last seen" keeps counting up live.
    _ticker = Timer.periodic(const Duration(seconds: 1), (_) => setState(() {}));
  }

  @override
  void dispose() {
    _ticker.cancel();
    super.dispose();
  }

  // Hijau = kirim data dalam 24 jam terakhir (aktif), merah = tidak (lihat
  // DeviceVessel.isActive - field status dari backend selalu null, jadi
  // tidak dipakai lagi untuk ini).
  Color _statusColor(bool isActive) =>
      isActive ? const Color(0xFF2E9E5B) : const Color(0xFFD64545);

  @override
  Widget build(BuildContext context) {
    final v = widget.vessel;
    final theme = Theme.of(context);

    return Card(
      margin: EdgeInsets.zero,
      elevation: 4,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 8, 8, 16),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            PanelHeader(
              title: v.name,
              // BoxFit.contain (bukan cover) - device_icon.png foto mobil
              // landscape (1536x1024, lebih lebar dari tinggi), kalau
              // dipaksa cover di kotak 28x28 bagian depan/belakang mobil
              // kepotong clip lingkaran (kelihatan jelek/terpotong).
              // contain memastikan seluruh mobil kelihatan utuh, dengan
              // sedikit ruang kosong di atas/bawah alih-alih kepotong.
              leading: ClipOval(
                child: Image.asset(
                  'assets/icons/device_icon.png',
                  width: 28,
                  height: 28,
                  fit: BoxFit.contain,
                ),
              ),
              onClose: widget.onClose,
              trailing: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Container(
                    width: 10,
                    height: 10,
                    decoration: BoxDecoration(
                      color: _statusColor(v.isActive),
                      shape: BoxShape.circle,
                    ),
                  ),
                  const SizedBox(width: 6),
                  Text(v.isActive ? 'Aktif' : 'Tidak Aktif', style: theme.textTheme.bodySmall),
                ],
              ),
            ),
            Text('SN: ${v.sn}', style: theme.textTheme.bodySmall),
            const Divider(height: 20),
            _infoRow(context, Icons.explore_outlined, 'Posisi',
                '${formatCoordinate(v.lat)}, ${formatCoordinate(v.lng)}'),
            _infoRow(context, Icons.speed_outlined, 'Kecepatan',
                '${(v.speedKnots * 1.852).toStringAsFixed(1)} km/jam'),
            _infoRow(context, Icons.navigation_outlined, 'Arah',
                '${v.heading.toStringAsFixed(0)}° (${formatHeadingCompass(v.heading)})'),
            _infoRow(context, Icons.bolt_outlined, 'Sumber Daya', v.powerSource),
            _infoRow(context, Icons.wifi_outlined, 'Sumber Data',
                v.dataSource + (v.wifiSsid != null ? ' (${v.wifiSsid})' : '')),
            if (v.dataSource == 'WIFI' && v.ipv4 != null)
              _infoRow(context, Icons.lan_outlined, 'IPv4', v.ipv4!),
            _infoRow(context, Icons.schedule_outlined, 'Update Terakhir',
                formatDeltaFromNow(v.lastTransmittedAt)),
          ],
        ),
      ),
    );
  }

  Widget _infoRow(BuildContext context, IconData icon, String label, String value) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, size: 16, color: theme.colorScheme.onSurfaceVariant),
          const SizedBox(width: 8),
          SizedBox(
            width: 100,
            child: Text(label,
                style: theme.textTheme.bodySmall
                    ?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
          ),
          Expanded(child: Text(value, style: theme.textTheme.bodyMedium)),
        ],
      ),
    );
  }
}
