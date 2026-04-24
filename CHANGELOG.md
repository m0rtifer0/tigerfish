# Tigerfish Changelog

## Unreleased

### Added
- `generate_training_data` UCI command baked into the engine.
  Self-play training data generation now runs with Tiger modifications
  active.
- Colab notebooks for NNUE training on Google Colab Pro
  (`training/colab/01_tiger_train.ipynb`,
   `training/colab/02_tiger_selfplay_colab.ipynb`,
   `training/colab/03_tiger_full_pipeline.ipynb`).
- Python pipeline scripts: `selfplay.py`, `train_tiger.py`,
  `export_nnue.py`, `embed_net.py`.
- `DESIGN.md` with a full internal design write-up.

### Fixed
- Infinite loop in `sfen_pack()` caused by `Rank` enum being `uint8_t`;
  the loop `for (Rank r = RANK_8; r >= RANK_1; --r)` wrapped around at
  zero. Replaced with an `int` loop counter.

## Phase 3A — Sharpness-Aware Root Tiebreaker

- Added `tiger_move_king_attacks()` for O(1) king-ring pressure scoring.
- Root-move tiebreaker swaps in the more aggressive move when candidates
  are within `antiDraw × sharpness / 512` cp.
- Safety-gated: only fires on MultiPV=1 with non-decisive scores.

## Phase 2 — Sharpness Signal

- Introduced `tiger_sharpness()` returning an integer 0–256.
- All four existing Tiger effects (optimism, check ordering, LMR
  reduction) are now scaled by sharpness.
- Components 2–4 gated behind "has major or minor piece" to suppress
  Tiger effects in pure pawn endgames.

## Phase 1 — Tiger Personality

- Added `TigerConfig` struct centralising all Tiger tuning parameters.
- Added four UCI options: `TigerMode`, `TigerAggression`, `TigerRisk`,
  `TigerAntiDraw`.
- Optimism boost in NNUE evaluation blend.
- Check-move ordering bonus in the move picker.
- LMR reduction scaling on forcing moves.
- Renamed binary to `tigerfish`; updated engine identity strings.
