import 'package:flutter/material.dart';

import '../../services/eta_calculator.dart';

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

class _AddWaypointDialogState extends State<AddWaypointDialog>
    with SingleTickerProviderStateMixin {
  late final TabController _tabController;
  final _formKeyDecimal = GlobalKey<FormState>();
  final _formKeyDms = GlobalKey<FormState>();

  final _nameController = TextEditingController();
  final _latController = TextEditingController();
  final _lngController = TextEditingController();

  final _dmsNameController = TextEditingController();
  final _latDegController = TextEditingController();
  final _latMinController = TextEditingController();
  final _latSecController = TextEditingController();
  String _latDir = 'N';
  final _lngDegController = TextEditingController();
  final _lngMinController = TextEditingController();
  final _lngSecController = TextEditingController();
  String _lngDir = 'E';

  String? _error;

  @override
  void initState() {
    super.initState();
    _tabController = TabController(length: 2, vsync: this);
  }

  @override
  void dispose() {
    _tabController.dispose();
    _nameController.dispose();
    _latController.dispose();
    _lngController.dispose();
    _dmsNameController.dispose();
    _latDegController.dispose();
    _latMinController.dispose();
    _latSecController.dispose();
    _lngDegController.dispose();
    _lngMinController.dispose();
    _lngSecController.dispose();
    super.dispose();
  }

  void _submitDecimal() {
    if (!_formKeyDecimal.currentState!.validate()) return;
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

  void _submitDms() {
    if (!_formKeyDms.currentState!.validate()) return;
    try {
      final lat = convertDmsToDecimal(
        degrees: int.parse(_latDegController.text),
        minutes: int.parse(_latMinController.text),
        seconds: double.parse(_latSecController.text.isEmpty ? '0' : _latSecController.text),
        direction: _latDir,
      );
      final lng = convertDmsToDecimal(
        degrees: int.parse(_lngDegController.text),
        minutes: int.parse(_lngMinController.text),
        seconds: double.parse(_lngSecController.text.isEmpty ? '0' : _lngSecController.text),
        direction: _lngDir,
      );
      Navigator.of(context).pop(WaypointDraft(
        name: _dmsNameController.text.trim(),
        lat: lat,
        lng: lng,
      ));
    } on FormatException catch (e) {
      setState(() => _error = e.message);
    }
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('Tambah Waypoint'),
      content: SizedBox(
        width: 380,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TabBar(
              controller: _tabController,
              tabs: const [
                Tab(text: 'Desimal'),
                Tab(text: 'DMS'),
              ],
              onTap: (_) => setState(() => _error = null),
            ),
            const SizedBox(height: 16),
            SizedBox(
              height: 260,
              child: TabBarView(
                controller: _tabController,
                children: [
                  _buildDecimalForm(),
                  _buildDmsForm(),
                ],
              ),
            ),
            if (_error != null) ...[
              const SizedBox(height: 8),
              Text(_error!, style: TextStyle(color: Theme.of(context).colorScheme.error)),
            ],
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('Batal'),
        ),
        FilledButton(
          onPressed: () =>
              _tabController.index == 0 ? _submitDecimal() : _submitDms(),
          child: const Text('Simpan'),
        ),
      ],
    );
  }

  Widget _buildDecimalForm() {
    return Form(
      key: _formKeyDecimal,
      child: Column(
        children: [
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
        ],
      ),
    );
  }

  Widget _buildDmsForm() {
    return Form(
      key: _formKeyDms,
      child: SingleChildScrollView(
        child: Column(
          children: [
            TextFormField(
              controller: _dmsNameController,
              decoration: const InputDecoration(labelText: 'Nama Waypoint'),
              validator: (v) => (v == null || v.trim().isEmpty) ? 'Wajib diisi' : null,
            ),
            const SizedBox(height: 12),
            _dmsRow('Lat', _latDegController, _latMinController, _latSecController,
                _latDir, ['N', 'S'], (v) => setState(() => _latDir = v!)),
            const SizedBox(height: 12),
            _dmsRow('Lng', _lngDegController, _lngMinController, _lngSecController,
                _lngDir, ['E', 'W'], (v) => setState(() => _lngDir = v!)),
          ],
        ),
      ),
    );
  }

  Widget _dmsRow(
    String label,
    TextEditingController deg,
    TextEditingController min,
    TextEditingController sec,
    String dir,
    List<String> dirOptions,
    ValueChanged<String?> onDirChanged,
  ) {
    return Row(
      children: [
        SizedBox(width: 32, child: Text(label)),
        Expanded(
          child: TextFormField(
            controller: deg,
            keyboardType: TextInputType.number,
            decoration: const InputDecoration(labelText: '°'),
            validator: (v) => int.tryParse(v ?? '') == null ? '?' : null,
          ),
        ),
        const SizedBox(width: 4),
        Expanded(
          child: TextFormField(
            controller: min,
            keyboardType: TextInputType.number,
            decoration: const InputDecoration(labelText: "'"),
            validator: (v) => int.tryParse(v ?? '') == null ? '?' : null,
          ),
        ),
        const SizedBox(width: 4),
        Expanded(
          child: TextFormField(
            controller: sec,
            keyboardType: const TextInputType.numberWithOptions(decimal: true),
            decoration: const InputDecoration(labelText: '"'),
          ),
        ),
        const SizedBox(width: 4),
        DropdownButton<String>(
          value: dir,
          items: dirOptions
              .map((d) => DropdownMenuItem(value: d, child: Text(d)))
              .toList(),
          onChanged: onDirChanged,
        ),
      ],
    );
  }
}
