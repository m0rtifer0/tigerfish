#!/usr/bin/env python3
"""
Tigerfish Self-Play Training Data Generator

Uses Tigerfish's built-in generate_training_data command to generate .binpack
training data via self-play. All Tiger search modifications (aggression,
sharpness, anti-draw) are active during self-play.

Usage:
    python3 training/selfplay.py                        # defaults: 10M positions
    python3 training/selfplay.py --count 50000000       # 50M positions
    python3 training/selfplay.py --depth 12             # deeper search
    python3 training/selfplay.py --threads 8            # limit threads

Output: training/data/tiger_train_NNN.binpack
"""

import argparse
import glob
import os
import subprocess
import sys
import time

TIGERFISH_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATA_DIR      = os.path.join(TIGERFISH_DIR, "training", "data")
TIGERFISH_BIN = os.path.join(TIGERFISH_DIR, "src", "tigerfish")


def next_binpack_name():
    """Generate the next sequential tiger_train_NNN.binpack filename."""
    existing = glob.glob(os.path.join(DATA_DIR, "tiger_train_*.binpack"))
    nums = []
    for f in existing:
        base = os.path.basename(f)
        try:
            n = int(base.replace("tiger_train_", "").replace(".binpack", ""))
            nums.append(n)
        except ValueError:
            pass
    next_num = max(nums, default=0) + 1
    return os.path.join(DATA_DIR, f"tiger_train_{next_num:03d}.binpack")


def main():
    parser = argparse.ArgumentParser(description="Tigerfish Self-Play Data Generator")
    parser.add_argument("--engine", type=str, default=TIGERFISH_BIN,
                        help=f"Engine binary (default: {TIGERFISH_BIN})")
    parser.add_argument("--count", type=int, default=10_000_000,
                        help="Number of positions to generate (default: 10M)")
    parser.add_argument("--depth", type=int, default=9,
                        help="Search depth per position (default: 9)")
    parser.add_argument("--threads", type=int, default=os.cpu_count(),
                        help="Number of threads (default: all cores)")
    parser.add_argument("--hash", type=int, default=256,
                        help="Hash table size in MB (default: 256)")
    parser.add_argument("--output", type=str, default=None,
                        help="Output .binpack path (default: auto-numbered)")
    parser.add_argument("--eval-limit", type=int, default=3000,
                        help="Max eval to keep (default: 3000 cp)")
    parser.add_argument("--random-move-count", type=int, default=8,
                        help="Random opening moves (default: 8)")
    parser.add_argument("--random-multi-pv", type=int, default=4,
                        help="MultiPV for random moves (default: 4)")
    args = parser.parse_args()

    if not os.path.isfile(args.engine):
        print(f"ERROR: Engine not found: {args.engine}")
        print(f"  Build Tigerfish: cd ~/Workspace/tigerfish/src && make -j$(nproc) ARCH=x86-64-avx2 COMP=gcc all")
        sys.exit(1)

    output = args.output or next_binpack_name()
    os.makedirs(os.path.dirname(output), exist_ok=True)

    print("=" * 60)
    print("  Tigerfish Self-Play Data Generation")
    print("=" * 60)
    print(f"  Engine:    {args.engine}")
    print(f"  Output:    {output}")
    print(f"  Positions: {args.count:,}")
    print(f"  Depth:     {args.depth}")
    print(f"  Threads:   {args.threads}")
    print(f"  Hash:      {args.hash} MB")
    print("=" * 60)

    # Build UCI command sequence
    uci_commands = "\n".join([
        "uci",
        f"setoption name Threads value {args.threads}",
        f"setoption name Hash value {args.hash}",
        "isready",
        (
            f"generate_training_data "
            f"depth {args.depth} "
            f"count {args.count} "
            f"random_move_count {args.random_move_count} "
            f"random_multi_pv {args.random_multi_pv} "
            f"random_multi_pv_diff 50 "
            f"eval_limit {args.eval_limit} "
            f"output_file_name {output}"
        ),
        "quit",
    ])

    log_file = os.path.join(DATA_DIR, "gensfen.log")
    start = time.time()

    print(f"\n  Generating... (log: {log_file})\n")

    with open(log_file, "w") as log:
        proc = subprocess.Popen(
            [args.engine],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        proc.stdin.write(uci_commands + "\n")
        proc.stdin.flush()
        proc.stdin.close()

        for line in proc.stdout:
            sys.stdout.write(line)
            log.write(line)
            log.flush()

        proc.wait()

    elapsed = time.time() - start
    hours = elapsed / 3600

    if proc.returncode != 0:
        print(f"\nERROR: Engine exited with code {proc.returncode}")
        sys.exit(proc.returncode)

    if os.path.isfile(output):
        size_mb = os.path.getsize(output) / (1024 * 1024)
        print(f"\n{'=' * 60}")
        print(f"  Done in {hours:.1f} hours")
        print(f"  Output: {output} ({size_mb:.1f} MB)")
        print(f"{'=' * 60}")
        print(f"\nNext step: python3 training/train_tiger.py")
    else:
        print(f"\nWARNING: Output file not found: {output}")


if __name__ == "__main__":
    main()
