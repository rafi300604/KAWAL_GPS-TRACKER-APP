import 'package:flutter/material.dart';

import '../../models/device_vessel.dart';
import '../../services/device_service.dart';
import '../../utils/time_ago.dart';
import 'vessel_info_panel.dart';

/// Permanent sidebar panel listing every registered device (`devices`
/// collection) — matches the reference app's `<aside class="sidebar">`:
/// always visible, no open/close toggle, embedded directly next to the map
/// rather than a `Drawer` overlay. The selected device's info card sits at
/// the top of this same panel, exactly like the reference's sidebar-embedded
/// "Panel Informasi Kapal" above its ship list.
class DeviceSidebar extends StatelessWidget {
  final DeviceService deviceService;
  final String? selectedSn;
  final DeviceVessel? selectedDevice;
  final ValueChanged<DeviceVessel> onSelect;
  final VoidCallback onClearSelection;

  const DeviceSidebar({
    super.key,
    required this.deviceService,
    required this.selectedSn,
    required this.selectedDevice,
    required this.onSelect,
    required this.onClearSelection,
  });

  // Hijau = kirim data dalam 24 jam terakhir (aktif), merah = tidak (lihat
  // DeviceVessel.isActive - field status dari backend selalu null, jadi
  // tidak dipakai lagi untuk ini).
  Color _statusColor(bool isActive) =>
      isActive ? const Color(0xFF2E9E5B) : const Color(0xFFD64545);

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Container(
      decoration: BoxDecoration(
        color: theme.colorScheme.surface,
        borderRadius: BorderRadius.circular(20),
        boxShadow: const [BoxShadow(color: Colors.black12, blurRadius: 12)],
      ),
      clipBehavior: Clip.antiAlias,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(20, 20, 20, 12),
            child: Row(
              children: [
                Icon(Icons.satellite_alt, color: theme.colorScheme.primary),
                const SizedBox(width: 10),
                Text('Daftar Alat', style: theme.textTheme.titleLarge),
              ],
            ),
          ),
          if (selectedDevice != null)
            Padding(
              padding: const EdgeInsets.fromLTRB(12, 0, 12, 12),
              child: VesselInfoPanel(vessel: selectedDevice!, onClose: onClearSelection),
            ),
          const Divider(height: 1),
          Expanded(
            child: StreamBuilder<List<DeviceVessel>>(
              stream: deviceService.watchAllDevices(),
              builder: (context, snapshot) {
                if (snapshot.connectionState == ConnectionState.waiting) {
                  return const Center(child: CircularProgressIndicator());
                }
                final devices = snapshot.data ?? [];
                if (devices.isEmpty) {
                  return Padding(
                    padding: const EdgeInsets.all(24),
                    child: Text(
                      'Belum ada alat yang terdaftar.',
                      style: theme.textTheme.bodyMedium
                          ?.copyWith(color: theme.colorScheme.onSurfaceVariant),
                    ),
                  );
                }
                // Material(type: transparency) di sini WAJIB: Container
                // pembungkus sidebar (di atas) punya BoxDecoration ber-warna
                // (surface) yang jadi DecoratedBox tepat di ANTARA ListTile
                // dan Material terdekat (Scaffold) - itu bikin ink
                // splash/selectedTileColor ListTile tidak kelihatan sama
                // sekali (assertion "ListTile background color or ink
                // splashes may be invisible" dari Flutter sendiri). Bungkus
                // di sini kasih ListTile Material terdekat yang "bersih".
                return Material(
                  type: MaterialType.transparency,
                  child: ListView.builder(
                    itemCount: devices.length,
                    itemBuilder: (context, index) {
                      final device = devices[index];
                      final isSelected = device.sn == selectedSn;
                      return ListTile(
                        selected: isSelected,
                        selectedTileColor:
                            theme.colorScheme.primaryContainer.withValues(alpha: 0.4),
                        // BoxFit.contain (bukan cover) - device_icon.png foto
                        // mobil landscape, cover di 28x28 bikin depan/
                        // belakang mobil kepotong clip lingkaran. contain
                        // memastikan mobil kelihatan utuh, warna status
                        // (backgroundColor CircleAvatar) tetap kelihatan
                        // mengisi sisa ruang kosong.
                        leading: CircleAvatar(
                          backgroundColor: _statusColor(device.isActive).withValues(alpha: 0.18),
                          child: ClipOval(
                            child: Image.asset(
                              'assets/icons/device_icon.png',
                              width: 28,
                              height: 28,
                              fit: BoxFit.contain,
                            ),
                          ),
                        ),
                        title: Text(device.name),
                        subtitle: Text(
                          'SN: ${device.sn} • ${formatDeltaFromNow(device.lastTransmittedAt)}',
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                        ),
                        onTap: () => onSelect(device),
                      );
                    },
                  ),
                );
              },
            ),
          ),
        ],
      ),
    );
  }
}
