import 'package:flutter/material.dart';

import '../../models/device_alert.dart';
import '../../services/device_service.dart';
import '../../utils/formatters.dart';

/// Riwayat notifikasi (mis. "alat diusik", kuota SIM menipis) untuk satu
/// device — beda dari SnackBar di home_screen.dart (yang cuma muncul
/// sekilas untuk alert BARU), dialog ini menampilkan SEMUA alert yang
/// pernah tercatat, jadi tetap bisa dicek "jam berapa alat X diusik"
/// meski notifikasi sekilasnya sudah terlewat/hilang.
class AlertHistoryDialog extends StatelessWidget {
  final DeviceService deviceService;
  final String deviceSn;
  final String deviceName;

  const AlertHistoryDialog({
    super.key,
    required this.deviceService,
    required this.deviceSn,
    required this.deviceName,
  });

  IconData _iconFor(String type) => switch (type) {
        'tamper' => Icons.warning_rounded,
        'remote_alarm' => Icons.campaign_rounded,
        'offline' => Icons.power_off_rounded,
        'online' => Icons.power_rounded,
        _ => Icons.sim_card_alert_outlined,
      };

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: Row(
        children: [
          const Icon(Icons.notifications_outlined),
          const SizedBox(width: 8),
          Expanded(
            child: Text('Riwayat Notifikasi — $deviceName', overflow: TextOverflow.ellipsis),
          ),
        ],
      ),
      content: SizedBox(
        width: 420,
        height: 400,
        child: StreamBuilder<List<DeviceAlert>>(
          stream: deviceService.watchAlerts(deviceSn),
          builder: (context, snapshot) {
            final alerts = snapshot.data ?? const <DeviceAlert>[];
            if (snapshot.connectionState == ConnectionState.waiting && alerts.isEmpty) {
              return const Center(child: CircularProgressIndicator());
            }
            if (alerts.isEmpty) {
              return const Center(child: Text('Belum ada notifikasi untuk alat ini.'));
            }
            final sorted = [...alerts]..sort((a, b) => b.createdAt.compareTo(a.createdAt));
            return ListView.separated(
              itemCount: sorted.length,
              separatorBuilder: (_, _) => const Divider(height: 1),
              itemBuilder: (context, i) {
                final alert = sorted[i];
                final isTamper = alert.type == 'tamper';
                return ListTile(
                  leading: Icon(
                    _iconFor(alert.type),
                    color: isTamper
                        ? Colors.red
                        : alert.type == 'remote_alarm'
                            ? Colors.blue[700]
                            : Colors.amber[800],
                  ),
                  title: Text(alert.displayMessage),
                  subtitle: Text(formatWib(alert.createdAt)),
                  dense: true,
                );
              },
            );
          },
        ),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.of(context).pop(), child: const Text('Tutup')),
      ],
    );
  }
}
