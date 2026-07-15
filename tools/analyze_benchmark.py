#!/usr/bin/env python3
"""
GPU Physics Benchmark Analysis Tool

Reads the CSV output from PhysicsBenchmark and generates:
  1. Human-readable summary tables
  2. ASCII bar charts of speedup
  3. JSON output for external plotting tools

Usage:
  python tools/analyze_benchmark.py logs/physics_benchmark.csv
  python tools/analyze_benchmark.py logs/physics_benchmark.csv --json
  python tools/analyze_benchmark.py logs/physics_benchmark.csv --chart
"""

import csv
import sys
import json
import math
from pathlib import Path


def load_csv(path: str) -> list[dict]:
    """Load benchmark CSV and return list of row dicts."""
    rows = []
    with open(path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            row['ParticleCount'] = int(row['ParticleCount'])
            row['CPUSingle_ms'] = float(row['CPUSingle_ms'])
            row['CPUMulti_ms'] = float(row['CPUMulti_ms'])
            row['GPU_ms'] = float(row['GPU_ms'])
            row['CPUSingle_FPS'] = float(row['CPUSingle_FPS'])
            row['CPUMulti_FPS'] = float(row['CPUMulti_FPS'])
            row['GPU_FPS'] = float(row['GPU_FPS'])
            row['Speedup_Multi'] = float(row['Speedup_Multi'])
            row['Speedup_GPU'] = float(row['Speedup_GPU'])
            rows.append(row)
    return rows


def print_summary(rows: list[dict]):
    """Print human-readable summary."""
    collision = rows[0]['Collision'] if rows else 'N/A'

    print("=" * 70)
    print("  CPU vs GPU Physics Engine Benchmark Analysis")
    print("=" * 70)
    print(f"  Collision: {collision}")
    print()
    print(f"  {'Particles':>10} | {'CPU 1T ms':>10} {'CPU MT ms':>10} {'GPU ms':>10} | {'CPU 1T FPS':>10} {'CPU MT FPS':>10} {'GPU FPS':>10} | {'MTx':>6} {'GPUx':>6}")
    print(f"  {'-'*10}-+-{'-'*10}-{'-'*10}-{'-'*10}-+-{'-'*10}-{'-'*10}-{'-'*10}-+-{'-'*6}-{'-'*6}")

    for r in rows:
        pc = r['ParticleCount']
        c1 = r['CPUSingle_ms']
        cm = r['CPUMulti_ms']
        gp = r['GPU_ms']
        c1f = r['CPUSingle_FPS']
        cmf = r['CPUMulti_FPS']
        gpf = r['GPU_FPS']
        sm = r['Speedup_Multi']
        sg = r['Speedup_GPU']

        print(f"  {pc:>10,} | {c1:>10.3f} {cm:>10.3f} {gp:>10.3f} | {c1f:>10.1f} {cmf:>10.1f} {gpf:>10.1f} | {sm:>5.1f}x {sg:>5.1f}x")

    print()
    print("  Interpretation:")
    print("    - Speedup MT  = CPU_Single / CPU_Multi (CPU parallelism efficiency)")
    print("    - Speedup GPU = CPU_Single / GPU       (GPU acceleration factor)")
    print("    - Values > 1 indicate faster execution")
    print()

    # Key insights
    print("  Key Insights:")
    gpu_available = any(r['GPU_ms'] > 0.001 for r in rows)

    if gpu_available:
        # Find crossover point where GPU becomes faster than CPU multi-thread
        for r in rows:
            if r['GPU_ms'] < r['CPUMulti_ms']:
                print(f"    ✅ GPU outperforms CPU multi-thread at {r['ParticleCount']:,} particles")
                break
        else:
            print("    ❌ GPU did not outperform CPU multi-thread in tested range")

        # Best GPU speedup
        best = max(rows, key=lambda r: r['Speedup_GPU'])
        print(f"    🏆 Best GPU speedup: {best['Speedup_GPU']:.1f}x at {best['ParticleCount']:,} particles")
    else:
        print("    ❌ No GPU data available (GPU physics engine not initialized)")

    # CPU multi-thread scaling efficiency
    for r in rows:
        if r['Speedup_Multi'] > 1.5:
            print(f"    🟢 Good multi-thread scaling at {r['ParticleCount']:,} particles ({r['Speedup_Multi']:.1f}x)")
            break
    else:
        print("    🟡 Poor multi-thread scaling (workload too small for threading overhead)")


def print_chart(rows: list[dict]):
    """Print ASCII bar chart of speedup."""
    print()
    print("  GPU Speedup Over CPU Single-Thread:")
    print()

    max_speedup = max(max(r['Speedup_GPU'], r['Speedup_Multi']) for r in rows)
    max_speedup = max(max_speedup, 1.0)
    bar_max = 50

    for r in rows:
        pc = r['ParticleCount']
        sg = min(r['Speedup_GPU'], max_speedup)
        sm = r['Speedup_Multi']

        # GPU bar
        gpu_bar = int((sg / max_speedup) * bar_max) if sg > 0 else 0
        gpu_str = '█' * gpu_bar + f' {sg:.1f}x' if sg > 0 else '  N/A'

        # MT bar
        mt_bar = int((sm / max_speedup) * bar_max)
        mt_str = '▓' * mt_bar + f' {sm:.1f}x'

        print(f"  {pc:>6,} | GPU: {gpu_str}")
        print(f"        | MT : {mt_str}")
        print()


def export_json(rows: list[dict], path: str):
    """Export results to JSON file."""
    data = {
        'benchmark': 'CPU vs GPU Physics Engine',
        'collision_enabled': rows[0]['Collision'] == 'Yes' if rows else False,
        'results': rows
    }
    with open(path, 'w') as f:
        json.dump(data, f, indent=2)
    print(f"  JSON exported: {path}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python analyze_benchmark.py <benchmark.csv> [options]")
        print("Options:")
        print("  --json       Export results as JSON")
        print("  --chart      Show ASCII bar chart")
        sys.exit(1)

    csv_path = sys.argv[1]
    if not Path(csv_path).exists():
        print(f"Error: File not found: {csv_path}")
        sys.exit(1)

    rows = load_csv(csv_path)

    if not rows:
        print("Error: No data in CSV")
        sys.exit(1)

    if '--json' in sys.argv:
        json_path = Path(csv_path).with_suffix('.json')
        export_json(rows, str(json_path))

    if '--chart' in sys.argv:
        print_chart(rows)

    print_summary(rows)


if __name__ == '__main__':
    main()