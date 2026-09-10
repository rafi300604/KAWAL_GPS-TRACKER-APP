import 'package:intl/intl.dart';

final DateFormat dateTimeFormat = DateFormat('yyyy-MM-dd HH:mm:ss');
final DateFormat shortDateTimeFormat = DateFormat('yyyy-MM-dd | HH:mm');

String formatCoordinate(double value) => value.toStringAsFixed(6);
