#!/usr/bin/env bash
# ===========================================================================
# Tigerfish NNUE Fine-tuning Pipeline
# ===========================================================================
#
# Prerequisites:
#   1. pip3 install --user --index-url https://download.pytorch.org/whl/cu128 torch
#      pip3 install --user lightning tyro python-chess tensorboard ranger21 schedulefree numba "numpy<2.0" psutil
#   2. Training data in training/data/*.binpack
#   3. nnue-pytorch cloned at ~/Workspace/nnue-pytorch
#   4. Existing Tigerfish .nnue at src/nn-9a0cc2a62c52.nnue
#
# Usage:
#   ./training/train_tiger.sh [step]
#   step: all | convert | train | export | embed
#         Default: all
# ===========================================================================

set -euo pipefail

TIGERFISH_DIR="$(cd "$(dirname "$0")/.." && pwd)"
NNUE_PYTORCH="$HOME/Workspace/nnue-pytorch"
TRAIN_DIR="$TIGERFISH_DIR/training"
DATA_DIR="$TRAIN_DIR/data"
RUNS_DIR="$TRAIN_DIR/runs"
NETS_DIR="$TRAIN_DIR/nets"

# The base net to fine-tune from
BASE_NET="$TIGERFISH_DIR/src/nn-9a0cc2a62c52.nnue"

# Training data (all .binpack files in data/)
TRAIN_DATA=$(ls "$DATA_DIR"/*.binpack 2>/dev/null | head -1)

STEP="${1:-all}"

mkdir -p "$RUNS_DIR" "$NETS_DIR"

# ---------------------------------------------------------------------------
# Step 1: Convert existing .nnue → .pt for fine-tuning base
# ---------------------------------------------------------------------------
convert_nnue_to_pt() {
    echo "=== Step 1: Converting base .nnue to .pt ==="
    BASE_PT="$NETS_DIR/base_net.pt"
    if [ -f "$BASE_PT" ]; then
        echo "  $BASE_PT already exists, skipping."
        return
    fi
    cd "$NNUE_PYTORCH"
    python3 serialize.py "$BASE_NET" "$BASE_PT"
    echo "  Done: $BASE_PT"
}

# ---------------------------------------------------------------------------
# Step 2: Install Python dependencies
# ---------------------------------------------------------------------------
install_deps() {
    echo "=== Installing Python dependencies ==="
    pip3 install --user lightning tyro python-chess tensorboard ranger21 \
                 schedulefree numba "numpy<2.0" psutil GPUtil 2>&1 | tail -3
}

# ---------------------------------------------------------------------------
# Step 3: Fine-tune training
# ---------------------------------------------------------------------------
run_training() {
    echo "=== Step 2: Starting NNUE fine-tuning ==="
    echo "  Data: $TRAIN_DATA"
    echo "  Base net: $NETS_DIR/base_net.pt"
    echo "  Output: $RUNS_DIR/tiger_finetune/"

    if [ ! -f "$TRAIN_DATA" ]; then
        echo "ERROR: No training data found in $DATA_DIR"
        echo "Wait for generate_training_data to complete first."
        exit 1
    fi

    cd "$NNUE_PYTORCH"
    python3 train.py \
        "$TRAIN_DATA" \
        --gpus 0 \
        --max-epochs 400 \
        --epoch-size 10000000 \
        --batch-size 16384 \
        --num-workers 4 \
        --resume-from-model "$NETS_DIR/base_net.pt" \
        --default-root-dir "$RUNS_DIR/tiger_finetune" \
        --network-save-period 20 \
        2>&1 | tee "$RUNS_DIR/training.log"
}

# ---------------------------------------------------------------------------
# Step 4: Export best checkpoint → .nnue
# ---------------------------------------------------------------------------
export_nnue() {
    echo "=== Step 3: Exporting best checkpoint to .nnue ==="
    # Find the latest checkpoint
    CKPT=$(ls -t "$RUNS_DIR"/tiger_finetune/lightning_logs/*/checkpoints/*.ckpt 2>/dev/null | head -1)
    if [ -z "$CKPT" ]; then
        echo "ERROR: No checkpoint found. Run training first."
        exit 1
    fi
    echo "  Using checkpoint: $CKPT"
    cd "$NNUE_PYTORCH"
    python3 serialize.py "$CKPT" "$NETS_DIR/tiger_net.nnue" --out-sha
    # Find the actual file (named by sha)
    TIGER_NNUE=$(ls -t "$NETS_DIR"/nn-*.nnue 2>/dev/null | head -1)
    echo "  Exported: $TIGER_NNUE"
    echo "$TIGER_NNUE" > "$NETS_DIR/latest_net.txt"
}

# ---------------------------------------------------------------------------
# Step 5: Embed net into Tigerfish and rebuild
# ---------------------------------------------------------------------------
embed_and_rebuild() {
    echo "=== Step 4: Embedding net into Tigerfish ==="
    TIGER_NNUE=$(cat "$NETS_DIR/latest_net.txt" 2>/dev/null || ls -t "$NETS_DIR"/nn-*.nnue | head -1)
    NET_NAME=$(basename "$TIGER_NNUE")
    NET_HASH="${NET_NAME%.nnue}"
    NET_HASH="${NET_HASH#nn-}"

    echo "  Net: $NET_NAME"
    echo "  Hash: $NET_HASH"

    # Copy net to src/
    cp "$TIGER_NNUE" "$TIGERFISH_DIR/src/"

    # Update evaluate.h to use new net
    sed -i "s|#define EvalFileDefaultNameBig \"nn-[a-f0-9]*.nnue\"|#define EvalFileDefaultNameBig \"$NET_NAME\"|" \
        "$TIGERFISH_DIR/src/evaluate.h"

    echo "  Updated src/evaluate.h to use $NET_NAME"
    echo "  Rebuilding Tigerfish..."

    cd "$TIGERFISH_DIR/src"
    make -j"$(nproc)" ARCH=native build 2>&1 | tail -5

    echo "=== Done! Tiger net embedded. ==="
    echo "  Engine: $TIGERFISH_DIR/src/tigerfish"
    echo "  Net:    $NET_NAME"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
case "$STEP" in
    all)
        install_deps
        convert_nnue_to_pt
        run_training
        export_nnue
        embed_and_rebuild
        ;;
    convert)   convert_nnue_to_pt ;;
    deps)      install_deps ;;
    train)     run_training ;;
    export)    export_nnue ;;
    embed)     embed_and_rebuild ;;
    *)
        echo "Usage: $0 [all|deps|convert|train|export|embed]"
        exit 1
        ;;
esac
