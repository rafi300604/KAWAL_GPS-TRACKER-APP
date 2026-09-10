/// Live Indonesian "time ago" string, e.g. "2 hari 3 jam 5 menit 10 detik lalu".
String formatDeltaFromNow(DateTime time, {DateTime? now}) {
  final current = now ?? DateTime.now();
  var diff = current.difference(time).inSeconds;
  if (diff < 0) diff = 0;
  if (diff < 1) return 'Baru saja';

  final days = diff ~/ 86400;
  diff -= days * 86400;
  final hours = diff ~/ 3600;
  diff -= hours * 3600;
  final minutes = diff ~/ 60;
  final seconds = diff - minutes * 60;

  final parts = <String>[];
  if (days > 0) parts.add('$days hari');
  if (hours > 0) parts.add('$hours jam');
  if (minutes > 0) parts.add('$minutes menit');
  if (seconds > 0 || parts.isEmpty) parts.add('$seconds detik');

  return '${parts.join(' ')} lalu';
}
