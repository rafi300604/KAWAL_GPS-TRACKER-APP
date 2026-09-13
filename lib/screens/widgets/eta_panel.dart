import 'package:flutter/material.dart';

import '../../models/device_vessel.dart';
import '../../models/route_result.dart';
import '../../models/waypoint.dart';
import '../../services/eta_calculator.dart';
import '../../utils/formatters.dart';

/// Per-waypoint row data, normalized regardless of whether it came from a
/// real road route (OSRM) or the straight-line fallback.
class _RowData {
  final Waypoint waypoint;
  final double distanceKm;
  final double hours;
  final String durationLabel;
  final EtaLevel level;

  const _RowData({
    required this.waypoint,
    required this.distanceKm,
    required this.hours,
    required this.durationLabel,
    required this.level,
  });
}

/// Floating "Waypoint ETA" panel — mirrors the reference app's
/// `WaypointETA.vue`: a small card anchored to the map's bottom-right
/// corner, combining the waypoint list (with a per-item delete button) and
/// the distance/ETA summary in one place.
///
/// Distance/time now come from an actual road-following driving route
/// (OSRM — the free/open equivalent of Google Maps Directions), computed in
/// [route] by the caller (`HomeScreen`, so the same route also drives the
/// map's road-following polyline). If routing fails (offline, or OSRM finds
/// no drivable route), this falls back to a straight-line estimate and says
/// so, rather than silently showing a number the user might mistake for a
/// real road distance.
class EtaPanel extends StatelessWidget {
  final DeviceVessel vessel;
  final List<Waypoint> waypoints;
  final RouteResult? route;
  final bool routeLoading;
  final bool routeFailed;
  final VoidCallback onClose;
  final void Function(Waypoint waypoint) onRemove;

  const EtaPanel({
    super.key,
    required this.vessel,
    required this.waypoints,
    required this.route,
    required this.routeLoading,
    required this.routeFailed,
    required this.onClose,
    required this.onRemove,
  });

  Color _levelColor(EtaLevel level) {
    switch (level) {
      case EtaLevel.good:
        return const Color(0xFF2E9E5B);
      case EtaLevel.warn:
        return const Color(0xFFE0A83E);
      case EtaLevel.bad:
        return const Color(0xFFD64545);
    }
  }

  // `route` bisa non-null tapi SUDAH TIDAK COCOK LAGI dengan [waypoints]
  // saat ini - home_screen sengaja tidak langsung membuang rute lama ke
  // null begitu daftar waypoint berubah (supaya tidak "berkedip" kosong
  // selama rute baru masih dihitung), jadi selalu validasi jumlah leg-nya
  // cocok dengan jumlah waypoint sebelum dipakai. Tanpa ini, ringkasan Total
  // Jarak/Waktu bisa nampilin angka rute LAMA (mis. untuk 2 waypoint) padahal
  // yang tersisa sekarang cuma 1 - baris per-waypoint sudah benar (fallback
  // garis lurus), tapi totalnya jadi tidak sinkron dan salah.
  bool get _routeIsCurrent => route != null && route!.legs.length == waypoints.length;

  List<_RowData> _buildRows() {
    if (_routeIsCurrent) {
      return List.generate(waypoints.length, (i) {
        final leg = route!.legs[i];
        return _RowData(
          waypoint: waypoints[i],
          distanceKm: leg.distanceKm,
          hours: leg.durationHours,
          durationLabel: formatDuration(leg.durationHours),
          level: etaLevelForHours(leg.durationHours),
        );
      });
    }

    // Fallback: straight-line distance from the vessel to each waypoint
    // independently (OSRM unreachable, or found no drivable route).
    return waypoints.map((w) {
      final eta = computeStraightLineEtaKm(
        fromLat: vessel.lat,
        fromLng: vessel.lng,
        toLat: w.lat,
        toLng: w.lng,
        speedKnots: vessel.speedKnots,
      );
      return _RowData(
        waypoint: w,
        distanceKm: eta.distanceKm,
        hours: eta.hours,
        durationLabel: eta.durationLabel,
        level: eta.level,
      );
    }).toList();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final rows = _buildRows();
    final usingFallback = !_routeIsCurrent && waypoints.isNotEmpty;

    return Container(
      width: 300,
      constraints: const BoxConstraints(maxHeight: 460),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface,
        borderRadius: BorderRadius.circular(12),
        boxShadow: const [BoxShadow(color: Colors.black26, blurRadius: 16)],
        border: Border.all(color: theme.colorScheme.outlineVariant),
      ),
      clipBehavior: Clip.antiAlias,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
            color: theme.colorScheme.primary,
            child: Row(
              children: [
                Expanded(
                  child: Text('ETA',
                      style: theme.textTheme.titleMedium
                          ?.copyWith(color: theme.colorScheme.onPrimary)),
                ),
                IconButton(
                  tooltip: 'Tutup',
                  onPressed: onClose,
                  icon: Icon(Icons.close, color: theme.colorScheme.onPrimary),
                  visualDensity: VisualDensity.compact,
                ),
              ],
            ),
          ),
          if (waypoints.isEmpty)
            const Padding(
              padding: EdgeInsets.symmetric(vertical: 28, horizontal: 16),
              child: Center(child: Text('Belum ada waypoint', style: TextStyle(fontStyle: FontStyle.italic))),
            )
          else
            // WAJIB tetap tampilkan rows (pakai fallback garis lurus,
            // dihitung lokal - tidak ada request jaringan sama sekali) SAAT
            // routeLoading masih true, bukan cuma spinner kosong. Waypoint
            // yang baru disimpan harus langsung kelihatan jarak/ETA-nya -
            // menunggu rute jalan (OSRM, bisa 1-3+ detik / bisa gagal kalau
            // offline) sebelum menampilkan APA PUN bikin panel kelihatan
            // "lama muncul" padahal datanya sendiri sudah ada dari awal.
            // Begitu rute jalan selesai dihitung, angkanya otomatis
            // diperhalus (straight-line -> jarak jalan sungguhan) lewat
            // rebuild biasa (route berubah dari null ke RouteResult).
            Flexible(
              child: SingleChildScrollView(
                child: Column(
                  children: [
                    if (routeLoading)
                      Container(
                        width: double.infinity,
                        color: theme.colorScheme.primaryContainer.withValues(alpha: 0.4),
                        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                        child: Row(
                          children: [
                            const SizedBox(
                              width: 12,
                              height: 12,
                              child: CircularProgressIndicator(strokeWidth: 2),
                            ),
                            const SizedBox(width: 8),
                            Expanded(
                              child: Text(
                                'Menyempurnakan ke rute jalan... (jarak garis lurus dulu)',
                                style: theme.textTheme.bodySmall?.copyWith(fontStyle: FontStyle.italic),
                              ),
                            ),
                          ],
                        ),
                      )
                    else if (usingFallback)
                      Container(
                        width: double.infinity,
                        color: theme.colorScheme.errorContainer.withValues(alpha: 0.5),
                        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                        child: Text(
                          'Rute jalan tidak ditemukan — jarak garis lurus ditampilkan.',
                          style: theme.textTheme.bodySmall
                              ?.copyWith(color: theme.colorScheme.onErrorContainer),
                        ),
                      ),
                    ...List.generate(rows.length, (i) {
                      final row = rows[i];
                      return Container(
                        padding: const EdgeInsets.fromLTRB(16, 10, 8, 10),
                        decoration: BoxDecoration(
                          border: Border(
                            bottom: BorderSide(color: theme.colorScheme.outlineVariant),
                          ),
                        ),
                        child: Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            CircleAvatar(
                              radius: 12,
                              backgroundColor: theme.colorScheme.primary,
                              child: Text('${i + 1}',
                                  style: TextStyle(color: theme.colorScheme.onPrimary, fontSize: 12)),
                            ),
                            const SizedBox(width: 10),
                            Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Text(row.waypoint.name,
                                      style: theme.textTheme.bodyMedium
                                          ?.copyWith(fontWeight: FontWeight.w600)),
                                  const SizedBox(height: 4),
                                  // Flexible + ellipsis di KEDUA teks (bukan Wrap) - Wrap
                                  // cuma mencegah overflow di level ROW (kalau gabungan
                                  // "Jarak: ..." dan "ETA: ..." kepanjangan, pindah baris),
                                  // tapi SATU chunk teks yang sendirian sudah lebih lebar
                                  // dari panel (mis. font sistem iOS + pengaturan aksesibilitas
                                  // teks besar) tetap overflow sendiri - itu sebabnya tulisan
                                  // "RIGHT OVERFLOWED" masih muncul di iOS walau sudah pakai
                                  // Wrap. Flexible+ellipsis di Text memberi jaminan MUTLAK:
                                  // teks otomatis mengecil/terpotong "..." pas-pasan sesuai
                                  // ruang yang benar-benar tersedia, RenderFlex TIDAK PERNAH
                                  // overflow lagi berapa pun besar font/text-scale perangkat.
                                  Row(
                                    children: [
                                      Flexible(
                                        child: Text(
                                          'Jarak: ${row.distanceKm.toStringAsFixed(2)} km',
                                          style: theme.textTheme.bodySmall,
                                          overflow: TextOverflow.ellipsis,
                                          maxLines: 1,
                                        ),
                                      ),
                                      const SizedBox(width: 8),
                                      Container(
                                        width: 8,
                                        height: 8,
                                        decoration: BoxDecoration(
                                          color: _levelColor(row.level),
                                          shape: BoxShape.circle,
                                        ),
                                      ),
                                      const SizedBox(width: 4),
                                      Flexible(
                                        child: Text(
                                          'ETA: ${row.durationLabel}',
                                          style: theme.textTheme.bodySmall,
                                          overflow: TextOverflow.ellipsis,
                                          maxLines: 1,
                                        ),
                                      ),
                                    ],
                                  ),
                                ],
                              ),
                            ),
                            IconButton(
                              tooltip: 'Hapus waypoint',
                              icon: const Icon(Icons.close, size: 16),
                              visualDensity: VisualDensity.compact,
                              onPressed: () => onRemove(row.waypoint),
                            ),
                          ],
                        ),
                      );
                    }),
                    _summarySection(context, rows),
                  ],
                ),
              ),
            ),
        ],
      ),
    );
  }

  Widget _summarySection(BuildContext context, List<_RowData> rows) {
    final theme = Theme.of(context);
    final totalDistance = _routeIsCurrent
        ? route!.totalDistanceKm
        : rows.fold<double>(0, (sum, r) => sum + r.distanceKm);
    final totalHours = _routeIsCurrent
        ? route!.totalDurationHours
        : rows.map((r) => r.hours).where((h) => h.isFinite).fold<double>(0, (sum, h) => sum + h);
    final totalLabel = formatDuration(totalHours);
    final arrival = totalHours > 0 && totalHours.isFinite
        ? DateTime.now().add(Duration(seconds: (totalHours * 3600).round()))
        : null;

    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(14),
      color: theme.colorScheme.primaryContainer.withValues(alpha: 0.4),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _summaryRow(context, 'Total Jarak', '${totalDistance.toStringAsFixed(2)} km'),
          _summaryRow(context, 'Total Waktu', totalLabel),
          _summaryRow(context, 'Waktu Tiba',
              arrival != null ? shortDateTimeFormat.format(arrival) : '-'),
        ],
      ),
    );
  }

  Widget _summaryRow(BuildContext context, String label, String value) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      // Flexible+ellipsis di label MAUPUN value (bukan cuma Expanded di
      // label) - kalau value sendiri (mis. "Waktu Tiba" dengan tanggal+jam)
      // sudah kepanjangan pada font/text-scale tertentu, Row tetap overflow
      // walau labelnya sudah aman di Expanded. Ini jaminan mutlak: RenderFlex
      // tidak akan overflow lagi di kombinasi label+value apa pun.
      child: Row(
        children: [
          Flexible(
            child: Text(label,
                style: theme.textTheme.bodySmall,
                overflow: TextOverflow.ellipsis,
                maxLines: 1),
          ),
          const SizedBox(width: 8),
          Flexible(
            child: Text(value,
                textAlign: TextAlign.right,
                overflow: TextOverflow.ellipsis,
                maxLines: 1,
                style: theme.textTheme.bodySmall?.copyWith(fontWeight: FontWeight.w700)),
          ),
        ],
      ),
    );
  }
}
