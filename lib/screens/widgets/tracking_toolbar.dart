import 'package:flutter/material.dart';

import '../../models/position_point.dart';
import '../../utils/formatters.dart';

/// Inline playback toolbar for tracking history — mirrors the reference
/// app's slider + "Waktu"/"Titik ke-" meta + play/pause/replay/cancel
/// buttons living directly in the map's button bar (not inside any
/// dialog/sheet). [onCancel] stops playback and pins the full static track,
/// matching the reference's `cancelTrackingSlider` — it does not remove the
/// track from the map; that happens when the device is deselected or a new
/// range is fetched.
class TrackingToolbar extends StatelessWidget {
  final List<PositionPoint> points;
  final int sliderIndex;
  final bool autoPlaying;
  final bool playbackComplete;
  final ValueChanged<double> onSliderChanged;
  final VoidCallback onPlay;
  final VoidCallback onPause;
  final VoidCallback onRewatch;
  final VoidCallback onCancel;

  const TrackingToolbar({
    super.key,
    required this.points,
    required this.sliderIndex,
    required this.autoPlaying,
    required this.playbackComplete,
    required this.onSliderChanged,
    required this.onPlay,
    required this.onPause,
    required this.onRewatch,
    required this.onCancel,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final current = points.isEmpty ? null : points[sliderIndex.clamp(0, points.length - 1)];

    return Container(
      padding: const EdgeInsets.fromLTRB(12, 8, 12, 8),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface,
        borderRadius: BorderRadius.circular(12),
        boxShadow: const [BoxShadow(color: Colors.black26, blurRadius: 12)],
        border: Border.all(color: theme.colorScheme.outlineVariant),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Slider(
            value: sliderIndex.clamp(0, points.length - 1).toDouble(),
            min: 0,
            max: (points.length - 1).clamp(0, double.infinity).toDouble(),
            onChanged: points.length > 1 ? onSliderChanged : null,
          ),
          Row(
            children: [
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Waktu: ${current != null ? dateTimeFormat.format(current.timestamp) : '-'}',
                      style: theme.textTheme.bodySmall,
                    ),
                    Text(
                      'Titik ke-${sliderIndex + 1} / ${points.length}',
                      style: theme.textTheme.bodySmall,
                    ),
                  ],
                ),
              ),
              IconButton(
                tooltip: 'Batalkan slider',
                icon: const Icon(Icons.close),
                onPressed: onCancel,
              ),
              if (playbackComplete)
                IconButton(
                  tooltip: 'Putar ulang',
                  icon: const Icon(Icons.replay),
                  onPressed: onRewatch,
                )
              else
                IconButton(
                  tooltip: autoPlaying ? 'Jeda' : 'Putar',
                  icon: Icon(autoPlaying ? Icons.pause_circle : Icons.play_circle),
                  iconSize: 30,
                  onPressed: autoPlaying ? onPause : onPlay,
                ),
            ],
          ),
        ],
      ),
    );
  }
}
