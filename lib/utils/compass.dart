const List<String> _compassLabels = [
  'Utara',
  'Timur Laut',
  'Timur',
  'Tenggara',
  'Selatan',
  'Barat Daya',
  'Barat',
  'Barat Laut',
];

/// Converts a heading in degrees (0-360) into an 8-point Indonesian compass label.
String formatHeadingCompass(double heading) {
  final normalized = ((heading % 360) + 360) % 360;
  final index = (normalized / 45).round() % 8;
  return _compassLabels[index];
}
