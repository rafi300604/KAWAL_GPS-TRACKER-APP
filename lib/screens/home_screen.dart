import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:latlong2/latlong.dart';
import 'package:provider/provider.dart';

import '../models/device_alert.dart';
import '../models/device_vessel.dart';
import '../models/position_point.dart';
import '../models/route_result.dart';
import '../models/waypoint.dart';
import '../providers/auth_provider.dart';
import '../providers/theme_provider.dart';
import '../services/device_service.dart';
import '../services/export_service.dart';
import '../services/routing_service.dart';
import 'admin/manage_users_screen.dart';
import 'widgets/add_waypoint_dialog.dart';
import 'widgets/app_header.dart';
import 'widgets/device_sidebar.dart';
import 'widgets/eta_panel.dart';
import 'widgets/tracking_history_dialog.dart';
import 'widgets/tracking_toolbar.dart';

// Default camera when no device has ever reported a position — the whole
// Indonesian archipelago, matching the reference app's initial map view.
const LatLng _kIndonesiaCenter = LatLng(-2, 118);
const double _kIndonesiaZoom = 5;
const double _kDeviceFocusZoom = 15;

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> with SingleTickerProviderStateMixin {
  final DeviceService _deviceService = DeviceService();
  final ExportService _exportService = ExportService();
  final RoutingService _routingService = RoutingService();
  final MapController _mapController = MapController();
  final GlobalKey<ScaffoldState> _scaffoldKey = GlobalKey<ScaffoldState>();
  late final AnimationController _flyAnimController;

  String? _selectedSn;
  bool _hasAutoSelected = false;
  bool _showEtaPanel = false;

  // Notifikasi otomatis (mis. "kuota SIM menipis") - lihat _watchAlertsFor().
  // _seenAlertIds dijaga di level State (bukan per-subscription) supaya ID
  // yang sudah pernah muncul tidak dinotifikasi ulang kalau user
  // pindah-pindah lalu balik lagi ke device yang sama.
  StreamSubscription<List<DeviceAlert>>? _alertSub;
  final Set<String> _seenAlertIds = {};
  bool _alertBaselineReady = false;

  // Road-following route (OSRM) from the selected device to its waypoints,
  // in order — drives BOTH the map's road-following polyline and the ETA
  // panel's distance/time figures, so they're always showing the same route.
  RouteResult? _route;
  bool _routeLoading = false;
  bool _routeFailed = false;
  String? _lastRoutedKey;

  // Tracking-history playback state, lifted up from the fetch dialog so it
  // lives in the map's inline toolbar — matching the reference app, where
  // `TrackingHistoryKapal.vue` only fetches and the slider/playback state
  // lives in the parent (App.vue), not the fetch modal.
  List<PositionPoint> _historyPoints = [];
  int _sliderIndex = 0;
  bool _autoPlaying = false;
  bool _playbackComplete = false;
  Timer? _historyTimer;

  @override
  void initState() {
    super.initState();
    _flyAnimController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 700),
    );
  }

  @override
  void dispose() {
    _flyAnimController.dispose();
    _historyTimer?.cancel();
    _alertSub?.cancel();
    super.dispose();
  }

  /// Subscribe ke notifikasi device ini - baris PERTAMA yang diterima
  /// dijadikan baseline "sudah dilihat" tanpa memunculkan apa pun (supaya
  /// riwayat alert LAMA tidak diperlakukan seolah baru terjadi begitu app
  /// dibuka), baris-baris SETELAHNYA yang belum pernah dilihat baru
  /// ditampilkan sebagai SnackBar.
  void _watchAlertsFor(String sn) {
    _alertSub?.cancel();
    _alertBaselineReady = false;
    _alertSub = _deviceService.watchAlerts(sn).listen((alerts) {
      if (!mounted) return;
      if (!_alertBaselineReady) {
        _seenAlertIds.addAll(alerts.map((a) => a.id));
        _alertBaselineReady = true;
        return;
      }
      final newOnes = alerts.where((a) => !_seenAlertIds.contains(a.id));
      for (final alert in newOnes) {
        _seenAlertIds.add(alert.id);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Row(
              children: [
                const Icon(Icons.sim_card_alert_outlined, color: Colors.white),
                const SizedBox(width: 12),
                Expanded(child: Text(alert.message)),
              ],
            ),
            backgroundColor: const Color(0xFFD64545),
            duration: const Duration(seconds: 8),
          ),
        );
      }
    });
  }

  /// Smoothly animates the map camera to [dest] at [destZoom].
  void _flyTo(LatLng dest, double destZoom) {
    final camera = _mapController.camera;
    final latTween = Tween<double>(begin: camera.center.latitude, end: dest.latitude);
    final lngTween = Tween<double>(begin: camera.center.longitude, end: dest.longitude);
    final zoomTween = Tween<double>(begin: camera.zoom, end: destZoom);

    void listener() {
      final t = Curves.easeInOutCubic.transform(_flyAnimController.value);
      _mapController.move(
        LatLng(latTween.transform(t), lngTween.transform(t)),
        zoomTween.transform(t),
      );
    }

    _flyAnimController
      ..removeListener(listener)
      ..reset()
      ..addListener(listener)
      ..forward().whenCompleteOrCancel(() => _flyAnimController.removeListener(listener));
  }

  void _resetTrackingState() {
    _historyTimer?.cancel();
    _historyPoints = [];
    _sliderIndex = 0;
    _autoPlaying = false;
    _playbackComplete = false;
  }

  void _resetRouteState() {
    _route = null;
    _routeLoading = false;
    _routeFailed = false;
    _lastRoutedKey = null;
  }

  /// Selecting a device: flies the camera to it and shows its info card,
  /// matching the reference's `focusShip`. Any in-progress tracking-history
  /// playback for the previous device is cleared.
  void _selectDevice(DeviceVessel device) {
    setState(() {
      _selectedSn = device.sn;
      _showEtaPanel = false;
      _resetTrackingState();
      _resetRouteState();
    });
    _watchAlertsFor(device.sn);
    _flyTo(LatLng(device.lat, device.lng), _kDeviceFocusZoom);
  }

  /// Closing the info card fully deselects the device (matching the
  /// reference's `clearSelectedShip`) — Waypoint/ETA/History all disappear
  /// since they depend on a selection, and any drawn track is cleared.
  void _clearSelection() {
    setState(() {
      _selectedSn = null;
      _showEtaPanel = false;
      _resetTrackingState();
      _resetRouteState();
    });
    _alertSub?.cancel();
    _alertSub = null;
  }

  /// Fetches (or re-fetches) the road-following route from the vessel to its
  /// waypoints, in order — the free/open equivalent of what Google Maps does
  /// when you set a destination: find the actual drivable road path, plus
  /// real distance/time along it. Re-runs whenever the waypoint set changes
  /// or the vessel has moved meaningfully (~10m), so the ETA keeps updating
  /// like a live navigation app — but is otherwise a no-op to avoid hammering
  /// the routing service on every tiny GPS jitter while stationary.
  Future<void> _maybeFetchRoute(DeviceVessel vessel, List<Waypoint> waypoints) async {
    final roundedLat = vessel.lat.toStringAsFixed(4);
    final roundedLng = vessel.lng.toStringAsFixed(4);
    final waypointKey = waypoints.map((w) => w.id).join(',');
    final routeKey = '${vessel.sn}|$roundedLat,$roundedLng|$waypointKey';
    if (_lastRoutedKey == routeKey) return;
    _lastRoutedKey = routeKey;

    if (waypoints.isEmpty) {
      if (_route != null || _routeLoading || _routeFailed) {
        setState(() {
          _route = null;
          _routeLoading = false;
          _routeFailed = false;
        });
      }
      return;
    }

    setState(() {
      _routeLoading = true;
      _routeFailed = false;
    });
    final points = [
      LatLng(vessel.lat, vessel.lng),
      ...waypoints.map((w) => LatLng(w.lat, w.lng)),
    ];
    final result = await _routingService.fetchDrivingRoute(points);
    if (!mounted || _lastRoutedKey != routeKey) return; // stale response, a newer fetch is in flight
    setState(() {
      _route = result;
      _routeLoading = false;
      _routeFailed = result == null;
    });
  }

  Future<void> _addWaypoint(DeviceVessel vessel) async {
    final draft = await showDialog<WaypointDraft>(
      context: context,
      builder: (_) => const AddWaypointDialog(),
    );
    if (draft == null) return;
    await _deviceService.addWaypoint(
      sn: vessel.sn,
      name: draft.name,
      lat: draft.lat,
      lng: draft.lng,
    );
    // Langsung buka panel ETA begitu waypoint tersimpan - device_service
    // sudah mendorong data waypoint terbaru ke stream sebelum Future ini
    // selesai (lihat _refreshWaypointsNow()), jadi EtaPanel yang baru saja
    // muncul di sini langsung dapat data yang benar, bukan daftar kosong.
    if (mounted) setState(() => _showEtaPanel = true);
  }

  Future<void> _openTrackingHistoryDialog(DeviceVessel vessel) async {
    final points = await showDialog<List<PositionPoint>>(
      context: context,
      builder: (_) => TrackingHistoryDialog(vessel: vessel, deviceService: _deviceService),
    );
    if (points == null) return;
    _historyTimer?.cancel();
    setState(() {
      _historyPoints = points;
      _sliderIndex = 0;
      _autoPlaying = false;
      // Default aman kalau titiknya kurang dari 2 (tidak ada yang bisa
      // diputar) - kalau >=2, _startAutoPlay() di bawah langsung mengubah
      // ini jadi false dan mulai animasi.
      _playbackComplete = true;
    });
    // Begitu fetch selesai, LANGSUNG mainkan dari titik pertama sampai
    // terakhir (bukan langsung pinned/"selesai" di titik akhir seperti
    // sebelumnya) - tombol "Putar ulang" baru muncul SETELAH playback ini
    // betul-betul sampai ke titik terakhir (lihat _startAutoPlay()).
    if (points.length >= 2) {
      _startAutoPlay(fromStart: true);
    }
  }

  void _onSliderChanged(double value) {
    _historyTimer?.cancel();
    setState(() {
      _sliderIndex = value.round();
      _autoPlaying = false;
      _playbackComplete = _sliderIndex >= _historyPoints.length - 1;
    });
  }

  void _startAutoPlay({bool fromStart = false}) {
    if (_historyPoints.length < 2) return;
    setState(() {
      if (fromStart) _sliderIndex = 0;
      _autoPlaying = true;
      _playbackComplete = false;
    });
    _historyTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      setState(() {
        if (_sliderIndex >= _historyPoints.length - 1) {
          _autoPlaying = false;
          _playbackComplete = true;
          _historyTimer?.cancel();
        } else {
          _sliderIndex++;
        }
      });
    });
  }

  void _pauseAutoPlay() {
    _historyTimer?.cancel();
    setState(() => _autoPlaying = false);
  }

  /// Tombol X di toolbar riwayat. Perilakunya beda tergantung status:
  /// - Playback MASIH berjalan (tombol Replay belum muncul): berhenti dan
  ///   pin ke titik terakhir (toolbar/jalur/marker riwayat TETAP tampil,
  ///   tombol Replay langsung muncul) - sama seperti sebelumnya.
  /// - Playback SUDAH selesai (tombol Replay sudah muncul): X di sini
  ///   artinya KELUAR dari fitur tracking history sepenuhnya - toolbar,
  ///   marker mobil merah, dan garis oranye semua hilang dari peta, balik
  ///   ke tombol Waypoint/ETA/History biasa. Device yang sedang dipilih
  ///   TETAP terpilih (tidak ikut ke-deselect) - cuma fitur history-nya
  ///   yang ditutup.
  /// Ukuran font/padding tombol Waypoint/ETA/History dikecilkan SEDIKIT,
  /// KHUSUS di iOS - font sistem iOS merender label yang persis sama lebih
  /// lebar daripada Roboto di Android, jadi 3 tombol yang di Android SELALU
  /// muat dalam satu baris bisa terpaksa pindah ke baris kedua di iOS (tata
  /// letak jadi beda antar platform). Ini murni penyesuaian ukuran visual
  /// biar tata letaknya persis sama (satu baris, rata kanan) seperti
  /// Android. TIDAK PERNAH memengaruhi Android/web - di sana kondisinya
  /// selalu false, jadi style/ukurannya null/default seperti semula, tidak
  /// berubah sama sekali.
  bool _isIOS(BuildContext context) => Theme.of(context).platform == TargetPlatform.iOS;

  ButtonStyle? _iOSCompactButtonStyle(BuildContext context) {
    if (!_isIOS(context)) return null;
    return FilledButton.styleFrom(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      textStyle: const TextStyle(fontSize: 13),
    );
  }

  double _iOSCompactIconSize(BuildContext context) => _isIOS(context) ? 16 : 18;

  void _cancelTrackingSlider() {
    _historyTimer?.cancel();
    if (_playbackComplete) {
      setState(() {
        _historyPoints = [];
        _sliderIndex = 0;
        _autoPlaying = false;
        _playbackComplete = false;
      });
    } else {
      setState(() {
        _autoPlaying = false;
        _sliderIndex = _historyPoints.isEmpty ? 0 : _historyPoints.length - 1;
        _playbackComplete = true;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final auth = context.watch<AuthProvider>();
    final themeProvider = context.watch<ThemeProvider>();

    // One subscription drives the AppBar, the drawer (which now embeds the
    // selected device's info card, matching the reference's sidebar layout
    // instead of floating a card over the map), and the map body.
    return StreamBuilder<List<DeviceVessel>>(
      stream: _deviceService.watchAllDevices(),
      builder: (context, snapshot) {
        final devices = snapshot.data ?? [];
        final isLoading = snapshot.connectionState == ConnectionState.waiting;

        DeviceVessel? selected;
        for (final d in devices) {
          if (d.sn == _selectedSn) {
            selected = d;
            break;
          }
        }
        if (selected == null && devices.isNotEmpty && !_hasAutoSelected) {
          _hasAutoSelected = true;
          final toSelect = devices.first;
          WidgetsBinding.instance
              .addPostFrameCallback((_) { if (mounted) _selectDevice(toSelect); });
        }

        // Persistent desktop-style chrome on wide screens (web/tablet),
        // matching the reference app: a fixed header bar on top, and a
        // sidebar that's always visible beside the map. On narrow phone
        // screens that same fixed-width sidebar simply doesn't fit — it
        // pushed the map off-screen and overflowed the header — so there the
        // sidebar becomes a `Drawer` (opened via the header's menu button)
        // and the map takes the full width, keeping every feature reachable.
        final isWide = MediaQuery.sizeOf(context).width >= 900;
        final sidebar = DeviceSidebar(
          deviceService: _deviceService,
          selectedSn: _selectedSn,
          selectedDevice: selected,
          onSelect: (d) {
            if (!isWide) _scaffoldKey.currentState?.closeDrawer();
            _selectDevice(d);
          },
          onClearSelection: _clearSelection,
        );

        final mapArea = Expanded(
          child: ClipRRect(
            borderRadius: BorderRadius.circular(20),
            child: Stack(
                              children: [
                                FlutterMap(
                                  mapController: _mapController,
                                  options: const MapOptions(
                                    initialCenter: _kIndonesiaCenter,
                                    initialZoom: _kIndonesiaZoom,
                                  ),
                                  children: [
                                    TileLayer(
                                      urlTemplate:
                                          'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
                                      userAgentPackageName: 'com.skripsi.gpstracker',
                                    ),
                                    MarkerLayer(
                                      markers: devices
                                          .map((d) => Marker(
                                                point: LatLng(d.lat, d.lng),
                                                width: 48,
                                                height: 48,
                                                child: GestureDetector(
                                                  onTap: () => _selectDevice(d),
                                                  child: _DeviceMarker(
                                                      vessel: d,
                                                      isSelected: d.sn == selected?.sn),
                                                ),
                                              ))
                                          .toList(),
                                    ),
                                    if (selected != null)
                                      _SelectedDeviceOverlayLayers(
                                        vessel: selected,
                                        deviceService: _deviceService,
                                        historyTrack: _historyPoints.isEmpty
                                            ? const []
                                            : _historyPoints.sublist(0, _sliderIndex + 1),
                                        route: _route,
                                        onWaypointsChanged: (wps) =>
                                            _maybeFetchRoute(selected!, wps),
                                      ),
                                  ],
                                ),
                                if (!isLoading && devices.isEmpty) const _NoDeviceBanner(),
                                if (selected != null)
                                  Positioned(
                                    left: 12,
                                    right: 12,
                                    bottom: 12,
                                    // WAJIB ada `left` di sini (bukan cuma
                                    // `right`) - tanpanya Positioned membiarkan
                                    // Column/Wrap di bawah tumbuh selebar
                                    // konten tanpa batas kiri, jadi Wrap TIDAK
                                    // PERNAH pindah baris walau tombolnya lebih
                                    // lebar dari layar - malah meluber lewat
                                    // tepi kiri layar (kepotong, bukan wrap).
                                    // Dengan `left` diset, Column dapat lebar
                                    // maksimum yang benar (selebar layar - 24),
                                    // Wrap bisa pindah baris kalau perlu, dan
                                    // di layar lebar tampilannya tidak berubah
                                    // sama sekali (tetap rata kanan, karena
                                    // crossAxisAlignment.end + Wrap end).
                                    child: Column(
                                      crossAxisAlignment: CrossAxisAlignment.end,
                                      children: [
                                        if (_showEtaPanel)
                                          Padding(
                                            padding: const EdgeInsets.only(bottom: 8),
                                            child: StreamBuilder<List<Waypoint>>(
                                              stream:
                                                  _deviceService.watchWaypoints(selected.sn),
                                              builder: (context, snap) => EtaPanel(
                                                vessel: selected!,
                                                waypoints: snap.data ?? [],
                                                route: _route,
                                                routeLoading: _routeLoading,
                                                routeFailed: _routeFailed,
                                                onClose: () =>
                                                    setState(() => _showEtaPanel = false),
                                                onRemove: (w) =>
                                                    _deviceService.removeWaypoint(
                                                        sn: selected!.sn, waypointId: w.id),
                                              ),
                                            ),
                                          ),
                                        if (_historyPoints.isNotEmpty)
                                          Padding(
                                            padding: const EdgeInsets.only(bottom: 8),
                                            child: SizedBox(
                                              width: 300,
                                              child: TrackingToolbar(
                                                points: _historyPoints,
                                                sliderIndex: _sliderIndex,
                                                autoPlaying: _autoPlaying,
                                                playbackComplete: _playbackComplete,
                                                onSliderChanged: _onSliderChanged,
                                                onPlay: () => _startAutoPlay(
                                                    fromStart: _playbackComplete),
                                                onPause: _pauseAutoPlay,
                                                onRewatch: () =>
                                                    _startAutoPlay(fromStart: true),
                                                onCancel: _cancelTrackingSlider,
                                              ),
                                            ),
                                          ),
                                        Wrap(
                                          alignment: WrapAlignment.end,
                                          spacing: 8,
                                          runSpacing: 8,
                                          children: [
                                            if (_historyPoints.isEmpty) ...[
                                              FilledButton.icon(
                                                style: _iOSCompactButtonStyle(context),
                                                onPressed: () => _addWaypoint(selected!),
                                                icon: Icon(Icons.add_location_alt_outlined,
                                                    size: _iOSCompactIconSize(context)),
                                                label: const Text('Waypoint'),
                                              ),
                                              // Solid fill (not outlined) so the button stays
                                              // legible over whatever map tile colors sit
                                              // behind it; filled (not tonal) while active,
                                              // to double as a toggle state.
                                              _showEtaPanel
                                                  ? FilledButton.icon(
                                                      style: _iOSCompactButtonStyle(context),
                                                      onPressed: () => setState(
                                                          () => _showEtaPanel = false),
                                                      icon: Icon(Icons.flag,
                                                          size: _iOSCompactIconSize(context)),
                                                      label: const Text('ETA'),
                                                    )
                                                  : FilledButton.tonalIcon(
                                                      style: _iOSCompactButtonStyle(context),
                                                      onPressed: () => setState(
                                                          () => _showEtaPanel = true),
                                                      icon: Icon(Icons.flag_outlined,
                                                          size: _iOSCompactIconSize(context)),
                                                      label: const Text('ETA'),
                                                    ),
                                            ],
                                            FilledButton.tonalIcon(
                                              style: _iOSCompactButtonStyle(context),
                                              onPressed: () =>
                                                  _openTrackingHistoryDialog(selected!),
                                              icon: Icon(Icons.route_outlined,
                                                  size: _iOSCompactIconSize(context)),
                                              label: const Text('History'),
                                            ),
                                            if (_historyPoints.isNotEmpty)
                                              FilledButton.tonalIcon(
                                                onPressed: () =>
                                                    _exportService.exportTrackingHistory(
                                                  points: _historyPoints,
                                                  deviceName: selected!.name,
                                                  deviceSn: selected.sn,
                                                  days: 0,
                                                ),
                                                icon: const Icon(Icons.file_download_outlined,
                                                    size: 18),
                                                label: const Text('Ekspor'),
                                              ),
                                          ],
                                        ),
                                      ],
                                    ),
                                  ),
                              ],
                            ),
                          ),
                        );

        return Scaffold(
          key: _scaffoldKey,
          drawer: isWide ? null : Drawer(child: sidebar),
          body: SafeArea(
            child: Padding(
              padding: EdgeInsets.all(isWide ? 16 : 8),
              child: Column(
                children: [
                  AppHeader(
                    appName: 'SISTEM MONITORING',
                    userName: auth.displayName,
                    userRole: auth.appUser?.role ?? '',
                    isAdmin: auth.isAdmin,
                    themeMode: themeProvider.mode,
                    onThemeModeChanged: themeProvider.setMode,
                    isCompact: !isWide,
                    onMenuPressed:
                        isWide ? null : () => _scaffoldKey.currentState?.openDrawer(),
                    onOpenAdmin: () => Navigator.of(context).push(
                      MaterialPageRoute(builder: (_) => const ManageUsersScreen()),
                    ),
                    onLogout: auth.logout,
                  ),
                  SizedBox(height: isWide ? 16 : 8),
                  // mapArea itu sendiri sudah Expanded (lihat definisinya di
                  // atas) - Expanded WAJIB jadi child langsung dari Flex
                  // (Row/Column), tidak boleh dibungkus Expanded lagi
                  // (Expanded di dalam Expanded = "competing
                  // ParentDataWidgets", crash). Untuk layar lebar, mapArea
                  // jadi child Row (dibungkus SATU Expanded luar supaya Row
                  // ikut mengisi tinggi Column) - untuk layar sempit, mapArea
                  // langsung jadi child Column, TANPA Expanded pembungkus lagi.
                  if (isWide)
                    Expanded(
                      child: Row(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          SizedBox(width: 340, child: sidebar),
                          const SizedBox(width: 16),
                          mapArea,
                        ],
                      ),
                    )
                  else
                    mapArea,
                ],
              ),
            ),
          ),
        );
      },
    );
  }
}

/// Waypoint route + tracking-history polylines/markers for the currently
/// selected device — split out so it only rebuilds on its own waypoint
/// stream, not the whole device list.
class _SelectedDeviceOverlayLayers extends StatelessWidget {
  final DeviceVessel vessel;
  final DeviceService deviceService;
  final List<PositionPoint> historyTrack;
  final RouteResult? route;
  final void Function(List<Waypoint> waypoints) onWaypointsChanged;

  const _SelectedDeviceOverlayLayers({
    required this.vessel,
    required this.deviceService,
    required this.historyTrack,
    required this.route,
    required this.onWaypointsChanged,
  });

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<List<Waypoint>>(
      stream: deviceService.watchWaypoints(vessel.sn),
      builder: (context, wpSnapshot) {
        final waypoints = wpSnapshot.data ?? [];
        // Route fetching is a side effect keyed on the vessel/waypoints
        // state, not part of building the widget tree — deferred to after
        // this frame, same pattern as the auto-select-on-load logic above.
        WidgetsBinding.instance.addPostFrameCallback((_) => onWaypointsChanged(waypoints));

        final hasRoadRoute = route != null && route!.geometry.length > 1;
        return Stack(
          children: [
            PolylineLayer(
              polylines: [
                if (waypoints.isNotEmpty)
                  Polyline(
                    // Road-following geometry from OSRM once available;
                    // a straight line only as a momentary placeholder while
                    // the route is loading (or if routing failed entirely).
                    points: hasRoadRoute
                        ? route!.geometry
                        : [
                            LatLng(vessel.lat, vessel.lng),
                            ...waypoints.map((w) => LatLng(w.lat, w.lng)),
                          ],
                    color: Colors.blue.withValues(alpha: hasRoadRoute ? 0.85 : 0.5),
                    strokeWidth: hasRoadRoute ? 4 : 3,
                    pattern: const StrokePattern.dotted(),
                  ),
                if (historyTrack.length > 1)
                  Polyline(
                    points: historyTrack.map((p) => LatLng(p.lat, p.lng)).toList(),
                    color: Colors.deepOrange,
                    strokeWidth: 3,
                  ),
              ],
            ),
            MarkerLayer(
              markers: waypoints
                  .map((w) => Marker(
                        point: LatLng(w.lat, w.lng),
                        width: 32,
                        height: 32,
                        child: const Icon(Icons.flag, color: Colors.redAccent),
                      ))
                  .toList(),
            ),
            // Marker mobil MERAH di titik playback riwayat SAAT INI
            // (historyTrack.last - titik paling akhir dari yang sudah
            // "dilewati" slider/autoplay). Cuma SATU marker, tanpa animasi
            // fade eksplisit - tiap kali historyTrack berubah (slider/
            // autoplay maju satu titik), Flutter rebuild MarkerLayer ini dan
            // marker lama otomatis diganti (hilang) oleh marker baru di
            // posisi berikutnya (muncul), persis "icon sebelumnya hilang,
            // icon selanjutnya muncul" yang diminta - tanpa jejak marker
            // menumpuk di titik-titik lama (beda dari polyline oranye di
            // atas yang memang sengaja menumpuk untuk menunjukkan jalur).
            if (historyTrack.isNotEmpty)
              MarkerLayer(
                markers: [
                  Marker(
                    point: LatLng(historyTrack.last.lat, historyTrack.last.lng),
                    width: 40,
                    height: 40,
                    child: _carIcon(
                      heading: historyTrack.last.heading,
                      width: 30 * _kCarIconAspect,
                      height: 30,
                      assetPath: _kCarIconRed,
                    ),
                  ),
                ],
              ),
          ],
        );
      },
    );
  }
}

// Aspect ratio asli assets/icons/icon_map.png (1145x1374, tampak atas mobil
// menghadap ke atas gambar) - dipakai supaya Image.asset tidak gepeng/melar
// saat di-render di ukuran marker yang jauh lebih kecil. Dipakai bareng oleh
// _DeviceMarker (posisi live) dan _SelectedDeviceOverlayLayers (posisi
// playback riwayat) - makanya top-level, bukan di dalam satu class saja.
const double _kCarIconAspect = 1145 / 1374;

/// Path aset ikon mobil MERAH (posisi masa lalu / playback riwayat).
const String _kCarIconRed = 'assets/icons/icon_map.png';

/// Path aset ikon mobil BIRU (posisi live/terkini) - file terpisah dari
/// desainer, BUKAN hasil tint programatis dari versi merah, supaya warnanya
/// akurat sesuai aset asli.
const String _kCarIconBlue = 'assets/icons/icon_map_blue.png';

/// Ikon mobil dari [assetPath] (salah satu dari [_kCarIconRed]/
/// [_kCarIconBlue]), diputar sesuai [heading] (0 derajat = menghadap ke atas
/// gambar/utara, sama untuk kedua aset karena posenya identik, cuma beda
/// warna body).
Widget _carIcon({
  required double heading,
  required double width,
  required double height,
  required String assetPath,
}) {
  return Transform.rotate(
    angle: heading * math.pi / 180,
    child: Image.asset(
      assetPath,
      width: width,
      height: height,
      fit: BoxFit.contain,
    ),
  );
}

/// The device's on-map symbol: a colored badge (status color) with a
/// direction indicator rotated to [DeviceVessel.heading]. Selalu biru
/// (terlepas dari status) supaya jelas beda dari marker merah di riwayat
/// playback - biru = "ini posisi TERKINI", merah = "ini posisi masa lalu".
class _DeviceMarker extends StatelessWidget {
  final DeviceVessel vessel;
  final bool isSelected;

  const _DeviceMarker({required this.vessel, required this.isSelected});

  // Hijau = kirim data dalam 24 jam terakhir (aktif), merah = tidak
  // (lihat DeviceVessel.isActive - dihitung dari lastTransmittedAt, bukan
  // dari field status yang selalu null dari backend).
  Color _statusColor() =>
      vessel.isActive ? const Color(0xFF2E9E5B) : const Color(0xFFD64545);

  @override
  Widget build(BuildContext context) {
    final color = _statusColor();
    final double haloSize = isSelected ? 48 : 44;
    final double iconHeight = isSelected ? 34 : 28;
    final double iconWidth = iconHeight * _kCarIconAspect;
    return SizedBox(
      width: 48,
      height: 48,
      child: Stack(
        alignment: Alignment.center,
        children: [
          Container(
            width: haloSize,
            height: haloSize,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              color: color.withValues(alpha: isSelected ? 0.28 : 0.18),
            ),
          ),
          _carIcon(
            heading: vessel.heading,
            width: iconWidth,
            height: iconHeight,
            assetPath: _kCarIconBlue,
          ),
          // Titik status (hijau=aktif/merah=tidak aktif) - TIDAK ikut
          // diputar oleh heading, supaya tetap gampang dibaca dari sudut
          // manapun mobil sedang menghadap.
          Positioned(
            bottom: 4,
            right: 4,
            child: Container(
              width: 12,
              height: 12,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: color,
                border: Border.all(
                  color: isSelected ? Colors.amber : Colors.white,
                  width: 2,
                ),
                boxShadow: const [BoxShadow(color: Colors.black26, blurRadius: 2)],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// Small floating banner shown over the (still-visible) map when no device
/// has reported a position yet — replaces nothing, the map stays usable.
class _NoDeviceBanner extends StatelessWidget {
  const _NoDeviceBanner();

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Positioned(
      left: 12,
      right: 12,
      top: 12,
      child: Card(
        color: theme.colorScheme.surface.withValues(alpha: 0.95),
        child: Padding(
          padding: const EdgeInsets.all(14),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Icon(Icons.satellite_alt_outlined, color: theme.colorScheme.onSurfaceVariant),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text('Belum ada alat yang terdaftar.'),
                    const SizedBox(height: 2),
                    Text(
                      'Menunggu perangkat mengirim posisi pertama ke Firestore.',
                      style: theme.textTheme.bodySmall
                          ?.copyWith(color: theme.colorScheme.onSurfaceVariant),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
