#!/usr/bin/env python3
"""
Export a training checkpoint (.ckpt) to Tigerfish .nnue format.

Usage:
    python3 training/export_nnue.py training/runs/tiger_finetune
    python3 training/export_nnue.py training/runs/tiger_finetune --checkpoint latest
    python3 training/export_nnue.py --checkpoint path/to/specific.ckpt
"""

import argparse
import glob
import os
import subprocess
import sys

TIGERFISH_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
NNUE_PYTORCH  = os.path.join(os.path.expanduser("~"), "Workspace", "nnue-pytorch")
NETS_DIR      = os.path.join(TIGERFISH_DIR, "training", "nets")


def find_checkpoints(run_dir):
    """Find all .ckpt files in a run directory, sorted by modification time (newest first)."""
    pattern = os.path.join(run_dir, "**", "*.ckpt")
    ckpts = sorted(glob.glob(pattern, recursive=True), key=os.path.getmtime, reverse=True)
    return ckpts


def main():
    parser = argparse.ArgumentParser(description="Export NNUE checkpoint to .nnue")
    parser.add_argument("run_dir", nargs="?", default=None,
                        help="Training run directory (e.g. training/runs/tiger_finetune)")
    parser.add_argument("--checkpoint", type=str, default="latest",
                        help="'latest', 'best', 'all', or path to a specific .ckpt")
    parser.add_argument("--no-cupy", action="store_true",
                        help="Disable cupy (use numpy, slower but less GPU memory)")
    args = parser.parse_args()

    os.makedirs(NETS_DIR, exist_ok=True)

    # Determine which checkpoints to export
    if args.checkpoint not in ("latest", "best", "all") and os.path.isfile(args.checkpoint):
        ckpts = [args.checkpoint]
    elif args.run_dir:
        all_ckpts = find_checkpoints(args.run_dir)
        if not all_ckpts:
            print(f"ERROR: No .ckpt files found in {args.run_dir}")
            sys.exit(1)

        if args.checkpoint == "all":
            ckpts = all_ckpts
        elif args.checkpoint == "best":
            # Prefer checkpoint with "best" in name, otherwise newest
            best = [c for c in all_ckpts if "best" in os.path.basename(c).lower()]
            ckpts = best[:1] if best else all_ckpts[:1]
        else:  # latest
            ckpts = all_ckpts[:1]
    else:
        parser.print_help()
        sys.exit(1)

    print(f"Exporting {len(ckpts)} checkpoint(s) to {NETS_DIR}/\n")

    exported = []
    for ckpt in ckpts:
        print(f"  Converting: {ckpt}")
        cmd = [
            sys.executable, "serialize.py",
            ckpt, NETS_DIR,
            "--out-sha",
        ]
        if args.no_cupy:
            cmd += ["--no-cupy"]

        result = subprocess.run(cmd, cwd=NNUE_PYTORCH, capture_output=True, text=True)

        if result.returncode != 0:
            print(f"  FAILED: {result.stderr.strip()}")
            continue

        # Parse output to find the generated .nnue filename
        for line in result.stdout.strip().split("\n"):
            if line.startswith("Wrote "):
                nnue_path = line.replace("Wrote ", "").strip()
                exported.append(nnue_path)
                print(f"  -> {os.path.basename(nnue_path)}")

        if result.stdout:
            for line in result.stdout.strip().split("\n"):
                if not line.startswith("Wrote "):
                    print(f"     {line}")

    if not exported:
        print("\nERROR: No checkpoints were exported successfully.")
        sys.exit(1)

    # Save the latest export path
    latest_file = os.path.join(NETS_DIR, "latest_net.txt")
    with open(latest_file, "w") as f:
        f.write(exported[0] + "\n")

    print(f"\n{'=' * 60}")
    print(f"  Exported {len(exported)} net(s)")
    print(f"  Latest: {os.path.basename(exported[0])}")
    print(f"{'=' * 60}")
    print(f"\nNext step: python3 training/embed_net.py")


if __name__ == "__main__":
    main()
