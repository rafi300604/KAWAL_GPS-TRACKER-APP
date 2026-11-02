import 'dart:async';

import 'package:flutter/material.dart';

import '../../models/place_result.dart';
import '../../services/geocoding_service.dart';

class WaypointDraft {
  final String name;
  final double lat;
  final double lng;

  const WaypointDraft({required this.name, required this.lat, required this.lng});
}

class AddWaypointDialog extends StatefulWidget {
  const AddWaypointDialog({super.key});

  @override
  State<AddWaypointDialog> createState() => _AddWaypointDialogState();
}

class _AddWaypointDialogState extends State<AddWaypointDialog> {
  final _formKey = GlobalKey<FormState>();
  final _geocodingService = GeocodingService();

  final _nameController = TextEditingController();
  final _latController = TextEditingController();
  final _lngController = TextEditingController();
  final _searchController = TextEditingController();

  // Debounce pencarian nama tempat - Nominatim (geocoder OpenStreetMap
  // gratis yang dipakai di sini, lihat GeocodingService) mewajibkan maksimal
  // ~1 request/detik; nunggu user berhenti ngetik dulu (400ms) sebelum
  // benar-benar query juga sekalian menghindari flood request per-huruf
  // yang sama sekali tidak perlu.
  Timer? _debounce;
  List<PlaceResult> _results = [];
  bool _searching = false;
  String? _searchError;

  String? _error;

  @override
  void dispose() {
    _debounce?.cancel();
    _nameController.dispose();
    _latController.dispose();
    _lngController.dispose();
    _searchController.dispose();
    super.dispose();
  }

  void _onSearchChanged(String query) {
    _debounce?.cancel();
    if (query.trim().isEmpty) {
      setState(() {
        _results = [];
        _searching = false;
        _searchError = null;
      });
      return;
    }
    setState(() => _searching = true);
    _debounce = Timer(const Duration(milliseconds: 400), () async {
      final results = await _geocodingService.search(query);
      if (!mounted) return;
      setState(() {
        _results = results;
        _searching = false;
        _searchError = results.isEmpty ? 'Tempat tidak ditemukan' : null;
      });
    });
  }

  void _pickResult(PlaceResult result) {
    setState(() {
      _latController.text = result.lat.toStringAsFixed(6);
      _lngController.text = result.lng.toStringAsFixed(6);
      // Nama waypoint diisi dari potongan pertama display_name (mis.
      // "Monas, RT.5/RW.2, Gambir, ..." -> "Monas") cuma kalau user BELUM
      // mengetik nama sendiri - supaya tidak menimpa nama yang sudah
      // sengaja diisi manual.
      if (_nameController.text.trim().isEmpty) {
        _nameController.text = result.displayName.split(',').first.trim();
      }
      _searchController.text = result.displayName;
      _results = [];
      _searchError = null;
    });
  }

  void _submit() {
    if (!_formKey.currentState!.validate()) return;
    final lat = double.parse(_latController.text);
    final lng = double.parse(_lngController.text);
    if (lat < -90 || lat > 90 || lng < -180 || lng > 180) {
      setState(() => _error = 'Lat harus -90..90, Lng harus -180..180');
      return;
    }
    Navigator.of(context).pop(WaypointDraft(
      name: _nameController.text.trim(),
      lat: (lat * 1e8).round() / 1e8,
      lng: (lng * 1e8).round() / 1e8,
    ));
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return AlertDialog(
      title: const Text('Tambah Waypoint'),
      content: SizedBox(
        width: 380,
        child: Form(
          key: _formKey,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              TextField(
                controller: _searchController,
                decoration: InputDecoration(
                  labelText: 'Cari nama tempat',
                  hintText: 'mis. Monas, Stasiun Gambir, ...',
                  prefixIcon: const Icon(Icons.search),
                  suffixIcon: _searching
                      ? const Padding(
                          padding: EdgeInsets.all(12),
                          child: SizedBox(
                            width: 16,
                            height: 16,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          ),
                        )
                      : (_searchController.text.isNotEmpty
                          ? IconButton(
                              icon: const Icon(Icons.clear),
                              onPressed: () {
                                _searchController.clear();
                                setState(() {
                                  _results = [];
                                  _searchError = null;
                                });
                              },
                            )
                          : null),
                ),
                onChanged: _onSearchChanged,
              ),
              // Hasil pencarian - list pendek (maks 5 dari GeocodingService)
              // langsung di bawah field, bukan overlay/dropdown terpisah,
              // supaya tetap sederhana di dalam dialog yang space-nya
              // terbatas. Tap satu hasil langsung isi lat/lng (+ nama kalau
              // masih kosong) - koordinat TETAP bisa diedit manual
              // sesudahnya lewat field Latitude/Longitude di bawah, hasil
              // pencarian cuma starting point, bukan satu-satunya cara isi.
              if (_results.isNotEmpty)
                Container(
                  margin: const EdgeInsets.only(top: 4),
                  constraints: const BoxConstraints(maxHeight: 180),
                  decoration: BoxDecoration(
                    border: Border.all(color: theme.colorScheme.outlineVariant),
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: ListView.separated(
                    shrinkWrap: true,
                    padding: EdgeInsets.zero,
                    itemCount: _results.length,
                    separatorBuilder: (_, _) => const Divider(height: 1),
                    itemBuilder: (context, i) {
                      final r = _results[i];
                      return ListTile(
                        dense: true,
                        leading: const Icon(Icons.place_outlined),
                        title: Text(r.displayName, maxLines: 2, overflow: TextOverflow.ellipsis),
                        onTap: () => _pickResult(r),
                      );
                    },
                  ),
                ),
              if (_searchError != null) ...[
                const SizedBox(height: 4),
                Align(
                  alignment: Alignment.centerLeft,
                  child: Text(_searchError!,
                      style: theme.textTheme.bodySmall
                          ?.copyWith(color: theme.colorScheme.error)),
                ),
              ],
              const SizedBox(height: 16),
              const Align(alignment: Alignment.centerLeft, child: Text('Atau isi manual:')),
              const SizedBox(height: 8),
              TextFormField(
                controller: _nameController,
                decoration: const InputDecoration(labelText: 'Nama Waypoint'),
                validator: (v) => (v == null || v.trim().isEmpty) ? 'Wajib diisi' : null,
              ),
              const SizedBox(height: 12),
              TextFormField(
                controller: _latController,
                keyboardType: const TextInputType.numberWithOptions(signed: true, decimal: true),
                decoration: const InputDecoration(labelText: 'Latitude'),
                validator: (v) =>
                    double.tryParse(v ?? '') == null ? 'Angka tidak valid' : null,
              ),
              const SizedBox(height: 12),
              TextFormField(
                controller: _lngController,
                keyboardType: const TextInputType.numberWithOptions(signed: true, decimal: true),
                decoration: const InputDecoration(labelText: 'Longitude'),
                validator: (v) =>
                    double.tryParse(v ?? '') == null ? 'Angka tidak valid' : null,
              ),
              if (_error != null) ...[
                const SizedBox(height: 8),
                Text(_error!, style: TextStyle(color: Theme.of(context).colorScheme.error)),
              ],
            ],
          ),
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('Batal'),
        ),
        FilledButton(
          onPressed: _submit,
          child: const Text('Simpan'),
        ),
      ],
    );
  }
}
