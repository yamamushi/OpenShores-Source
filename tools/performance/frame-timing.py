#!/usr/bin/env python3
"""Summarize an OPENSHORES_FRAME_TIMING CSV without third-party dependencies."""
import argparse
import csv
import json
import math
from pathlib import Path


def summarize(path, limit=10):
    with Path(path).open() as stream:
        records = list(csv.DictReader(line for line in stream if not line.startswith('#')))
    frames = {int(r['frame']): r for r in records if r['record'] == 'frame'}
    swaps = [r for r in records if r['record'] == 'swap' and float(r['interval_ms']) >= 0]
    intervals = sorted(float(r['interval_ms']) for r in swaps)
    def percentile(p):
        if not intervals:
            return None
        at = (len(intervals) - 1) * p
        lo, hi = math.floor(at), math.ceil(at)
        return round(intervals[lo] + (intervals[hi] - intervals[lo]) * (at - lo), 3)
    slowest = []
    for swap in sorted(swaps, key=lambda r: float(r['interval_ms']), reverse=True)[:limit]:
        frame = frames.get(int(swap['frame']), {})
        categories = {key[:-3]: float(value) for key, value in frame.items()
                      if key.endswith('_ms') and key not in
                      ('elapsed_ms', 'interval_ms', 'paint_ms', 'paint_to_swap_ms', 'logger_io_ms')}
        slowest.append({'frame': int(swap['frame']), 'elapsed_ms': float(swap['elapsed_ms']),
                        'swap_interval_ms': float(swap['interval_ms']),
                        'paint_ms': float(frame['paint_ms']) if frame else None,
                        'paint_to_swap_ms': float(swap['paint_to_swap_ms']),
                        'draws': int(frame['draws']) if frame else None,
                        'triangles': int(frame['triangles']) if frame else None,
                        'logger_io_ms': float(frame['logger_io_ms']) if frame else None,
                        'categories_ms': dict(sorted(categories.items(), key=lambda item: -item[1])[:5]),
                        'resource_calls': {key: int(value) for key, value in frame.items()
                                           if key in ('mesh.create_calls', 'flora.mesh_calls', 'texture.upload_calls', 'buffer.update_calls')}})
    return {'file': str(path), 'paint_frames': len(frames), 'swap_intervals': len(intervals),
            'interval_ms': {'p50': percentile(.5), 'p95': percentile(.95), 'p99': percentile(.99),
                            'max': max(intervals) if intervals else None},
            'intervals_over_50ms': sum(v > 50 for v in intervals),
            'intervals_over_100ms': sum(v > 100 for v in intervals),
            'dropped_records': max((int(r['dropped_records']) for r in records), default=0),
            'slowest': slowest,
            'note': 'CPU wall times; nested categories overlap. Qt swap intervals include scheduling/composition/driver waits, not physical scanout. No GPU pass duration is inferred.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--worst', type=int, default=10)
    args = parser.parse_args()
    print(json.dumps(summarize(args.csv, max(0, args.worst)), indent=2))


if __name__ == '__main__':
    main()
