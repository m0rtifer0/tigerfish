#!/usr/bin/env python3
"""
Embed an exported .nnue network into Tigerfish, update evaluate.h, and rebuild.

Usage:
    python3 training/embed_net.py                          # use latest from nets/
    python3 training/embed_net.py training/nets/nn-abc123.nnue  # specific file
    python3 training/embed_net.py --no-build               # just patch, don't compile
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

TIGERFISH_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC_DIR       = os.path.join(TIGERFISH_DIR, "src")
NETS_DIR      = os.path.join(TIGERFISH_DIR, "training", "nets")
EVALUATE_H    = os.path.join(SRC_DIR, "evaluate.h")


def find_latest_net():
    """Find the latest exported .nnue from nets/ directory."""
    latest_file = os.path.join(NETS_DIR, "latest_net.txt")
    if os.path.isfile(latest_file):
        with open(latest_file) as f:
            path = f.read().strip()
            if os.path.isfile(path):
                return path

    # Fallback: newest nn-*.nnue in nets/
    nets = sorted(glob.glob(os.path.join(NETS_DIR, "nn-*.nnue")),
                  key=os.path.getmtime, reverse=True)
    if nets:
        return nets[0]
    return None


def patch_evaluate_h(net_name):
    """Update EvalFileDefaultNameBig in evaluate.h."""
    with open(EVALUATE_H, "r") as f:
        content = f.read()

    old_pattern = r'#define EvalFileDefaultNameBig "nn-[a-f0-9]+\.nnue"'
    new_define = f'#define EvalFileDefaultNameBig "{net_name}"'

    if not re.search(old_pattern, content):
        print(f"  WARNING: Could not find EvalFileDefaultNameBig pattern in {EVALUATE_H}")
        print(f"  You may need to manually update the net name.")
        return False

    new_content = re.sub(old_pattern, new_define, content)

    if new_content == content:
        print(f"  evaluate.h already uses {net_name}")
        return True

    with open(EVALUATE_H, "w") as f:
        f.write(new_content)

    print(f"  Updated evaluate.h -> {net_name}")
    return True


def main():
    parser = argparse.ArgumentParser(description="Embed NNUE net into Tigerfish")
    parser.add_argument("nnue_file", nargs="?", default=None,
                        help="Path to .nnue file (default: latest from nets/)")
    parser.add_argument("--no-build", action="store_true",
                        help="Only copy and patch, don't rebuild")
    args = parser.parse_args()

    # Find the .nnue file
    if args.nnue_file:
        nnue_path = os.path.abspath(args.nnue_file)
    else:
        nnue_path = find_latest_net()

    if not nnue_path or not os.path.isfile(nnue_path):
        print("ERROR: No .nnue file found.")
        print("  Run export_nnue.py first, or provide a path.")
        sys.exit(1)

    net_name = os.path.basename(nnue_path)
    net_hash = net_name.replace("nn-", "").replace(".nnue", "")

    print("=" * 60)
    print("  Embedding NNUE into Tigerfish")
    print("=" * 60)
    print(f"  Net:  {net_name}")
    print(f"  Hash: {net_hash}")
    print()

    # 1. Copy .nnue to src/
    dest = os.path.join(SRC_DIR, net_name)
    if os.path.abspath(nnue_path) != os.path.abspath(dest):
        shutil.copy2(nnue_path, dest)
        print(f"  Copied to src/{net_name}")
    else:
        print(f"  Already in src/")

    # 2. Patch evaluate.h
    patch_evaluate_h(net_name)

    # 3. Build
    if args.no_build:
        print("\n  Skipping build (--no-build)")
    else:
        print("\n  Building Tigerfish...")
        result = subprocess.run(
            ["make", f"-j{os.cpu_count()}", "ARCH=native", "build"],
            cwd=SRC_DIR,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"  Build FAILED:\n{result.stderr[-500:]}")
            sys.exit(result.returncode)
        print("  Build OK")

    print(f"\n{'=' * 60}")
    print(f"  Tigerfish is ready with {net_name}")
    print(f"  Engine: src/tigerfish")
    print(f"{'=' * 60}")


if __name__ == "__main__":
    main()
