#!/usr/bin/env python3
"""
Tigerfish NNUE Training Launcher

Usage:
    python3 training/train_tiger.py                    # scratch training
    python3 training/train_tiger.py --resume-from-model nets/base_net.pt  # fine-tune
    python3 training/train_tiger.py --max-epochs 100   # override defaults

Wraps nnue-pytorch/train.py with Tiger-specific defaults.
"""

import argparse
import os
import subprocess
import sys

TIGERFISH_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
NNUE_PYTORCH  = os.path.join(os.path.expanduser("~"), "Workspace", "nnue-pytorch")
DATA_DIR      = os.path.join(TIGERFISH_DIR, "training", "data")
RUNS_DIR      = os.path.join(TIGERFISH_DIR, "training", "runs")


def find_training_data():
    """Find all .binpack files in data/."""
    files = sorted(
        f for f in os.listdir(DATA_DIR) if f.endswith(".binpack")
    )
    if not files:
        print(f"ERROR: No .binpack files found in {DATA_DIR}")
        sys.exit(1)
    return [os.path.join(DATA_DIR, f) for f in files]


def main():
    parser = argparse.ArgumentParser(description="Tigerfish NNUE Training")
    parser.add_argument("--max-epochs", type=int, default=400)
    parser.add_argument("--epoch-size", type=int, default=10_000_000)
    parser.add_argument("--batch-size", type=int, default=16384)
    parser.add_argument("--num-workers", type=int, default=4)
    parser.add_argument("--gpus", type=str, default="0")
    parser.add_argument("--network-save-period", type=int, default=20)
    parser.add_argument("--resume-from-model", type=str, default=None,
                        help="Path to .pt file for fine-tuning")
    parser.add_argument("--resume-from-checkpoint", type=str, default=None,
                        help="Path to .ckpt to resume training")
    parser.add_argument("--run-name", type=str, default="tiger_finetune",
                        help="Name for the training run directory")
    parser.add_argument("--data", type=str, nargs="*", default=None,
                        help=".binpack files (default: all in data/)")
    args = parser.parse_args()

    data_files = args.data if args.data else find_training_data()
    run_dir = os.path.join(RUNS_DIR, args.run_name)
    os.makedirs(run_dir, exist_ok=True)

    print("=" * 60)
    print("  Tigerfish NNUE Training")
    print("=" * 60)
    print(f"  Data:       {data_files}")
    print(f"  Output:     {run_dir}")
    print(f"  Epochs:     {args.max_epochs}")
    print(f"  Epoch size: {args.epoch_size:,}")
    print(f"  Batch size: {args.batch_size:,}")
    print(f"  GPUs:       {args.gpus}")
    if args.resume_from_model:
        print(f"  Fine-tune:  {args.resume_from_model}")
    if args.resume_from_checkpoint:
        print(f"  Resume:     {args.resume_from_checkpoint}")
    print("=" * 60)

    cmd = [
        sys.executable, "train.py",
        *data_files,
        "--gpus", args.gpus,
        "--max-epochs", str(args.max_epochs),
        "--epoch-size", str(args.epoch_size),
        "--batch-size", str(args.batch_size),
        "--num-workers", str(args.num_workers),
        "--default-root-dir", run_dir,
        "--network-save-period", str(args.network_save_period),
    ]

    if args.resume_from_model:
        cmd += ["--resume-from-model", args.resume_from_model]
    if args.resume_from_checkpoint:
        cmd += ["--resume-from-checkpoint", args.resume_from_checkpoint]

    log_file = os.path.join(run_dir, "training.log")
    print(f"\n  Log: {log_file}\n")

    with open(log_file, "w") as log:
        proc = subprocess.Popen(
            cmd,
            cwd=NNUE_PYTORCH,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        for line in proc.stdout:
            sys.stdout.write(line)
            log.write(line)
        proc.wait()

    if proc.returncode != 0:
        print(f"\nERROR: Training failed with exit code {proc.returncode}")
        sys.exit(proc.returncode)

    print(f"\nTraining complete. Checkpoints in: {run_dir}")
    print(f"Next step: python3 training/export_nnue.py {run_dir}")


if __name__ == "__main__":
    main()
