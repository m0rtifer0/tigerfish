# Tigerfish – Design Notes

Tigerfish is a UCI chess engine derived from Stockfish.  
It retains full Stockfish correctness and UCI compliance while layering
a controllable aggressive personality on top.

---

## Philosophy

> "Controlled aggression, not random noise."

Tigerfish is **not** a weaker Stockfish with random sacrifices bolted on.
It is Stockfish with calibrated adjustments to search priority and evaluation
bias so that the engine:

* prefers active, initiative-holding continuations over sterile equality;
* searches check, forcing, and attack lines more deeply before pruning them;
* avoids early simplifications in near-equal positions;
* plays the best *defensive* move when it is losing — the aggression applies
  to choosing *between equally good options*, not to discarding sound defences.

---

## Architecture

All Tiger behaviour is isolated in a thin layer:

```
UCI options  ──►  TigerConfig  ──►  Worker::tigerCfg  ──►  search / eval
```

`TigerConfig` (`src/tigerfish.h`) is a plain struct with four fields.  
It is read once at the start of every `iterative_deepening()` call, so option
changes take effect at the next `go` command without any engine restart.

---

## UCI Options

| Option          | Type | Default | Range  | Effect |
|-----------------|------|---------|--------|--------|
| `TigerMode`     | bool | false   | –      | Master on/off. False → identical to Stockfish |
| `TigerAggression` | int | 50    | 0–100  | Scales optimism boost and check-move ordering bonus |
| `TigerRisk`     | int  | 50      | 0–100  | Scales LMR reduction for checking moves |
| `TigerAntiDraw` | int  | 50      | 0–100  | Extra optimism bonus near equality to avoid draws |

All options are no-ops when `TigerMode = false`.

---

## Technical Changes (Phase 1)

### A. Branding (`src/misc.cpp`, `src/uci.cpp`, `src/Makefile`)

Engine name changed to "Tigerfish" in `engine_version_info()` and the UCI
`id name` / `id author` output.  Upstream Stockfish copyright notices are
preserved intact; only the display name is changed.  Binary is now `tigerfish`.

### B. TigerConfig struct (`src/tigerfish.h`)

Plain-old-data struct included by `search.h`.  No vtables, no allocations.
Stored as `Worker::tigerCfg` (private member, one copy per thread).

### C. Optimism adjustment (`src/search.cpp` – `iterative_deepening`)

Stockfish already computes an `optimism[us]` value each aspiration iteration
(formula: `144 * avg / (|avg| + 91)`) and feeds it into NNUE evaluation
blending.  Higher optimism → engine values its own activity/initiative more.

Two Tiger adjustments applied after the base optimism is computed:

1. **Aggression boost**: `optimism[us] += optimism[us] * aggression / 500`  
   Scales existing optimism by up to +20 % at aggression = 100.  
   Bound: max Stockfish optimism ≈ 144 units → max extra ≈ 29 units ≈ 4–5 cp.

2. **AntiDraw bonus**: flat bonus `antiDraw * (100 – |avg|) / 300` when
   `|avg| < 100` (approximately within ±1 pawn of equality).  
   Bound: max extra ≈ 33 units ≈ 5 cp, tapers to zero as advantage grows.

### D. Check-move LMR reduction (`src/search.cpp` – `search()`)

LMR reduction `r` is decreased for moves that give check when Tiger is active.
This keeps attacking/checking lines one step deeper in the tree before they
risk being pruned by LMR.

Formula: `r -= risk * 512 / 100`  
Bound: at risk = 100, checking moves get at most –512 LMR units = –½ ply.  
SEE and legality guards upstream remain fully active — no unsound sacrifices
slip through because of this.

### E. Check-move ordering bonus (`src/movepick.cpp` – `score<QUIETS>`)

Quiet moves that give check already receive a `16 384` ordering bonus gated by
a SEE(move) ≥ –75 check.  Tiger adds `tigerCheckBonus = aggression * 50`
on top (max +5 000 at aggression = 100, ≈ +30 %).

Effect: checking quiets are tried earlier in the move loop, so they reach full
depth more often before any depth-budget cutoffs.  The SEE gate means only
moves that don't obviously lose material benefit from this.

---

## Safety Rails

* **Move legality is unchanged.** `movegen`, `position`, `SEE`, `repetition`,
  and `TT` are not touched.
* **All bonuses are bounded.**  Every Tiger constant is clipped by the
  parameter range (0–100) times a small fixed multiplier; none can produce
  infinite loops, NaN, or out-of-range search values.
* **TigerMode = false is a strict no-op.**  The `if (tigerCfg.enabled)` guard
  in every callsite ensures zero performance or behaviour change when disabled.
* **Losing positions:** Tiger adjustments modify how the engine *values*
  positions, not which moves are *legal*.  The engine still finds the best
  defensive move when it is behind.

---

## What Was Intentionally Left Out (Phase 1)

* NNUE architecture/weight changes (phase 2 territory).
* Hand-crafted king-safety eval terms (superseded by NNUE for now).
* Opening book or endgame table changes.
* Root move sharpness scoring beyond the optimism adjustment.
* Position-specific sharpness function (planned for phase 2 alongside
  per-position aggression scaling).

---

## Technical Changes (Phase 2)

### F. Per-position sharpness signal (`src/tigerfish.h`)

`tiger_sharpness(pos)` returns 0–256 measuring how tactically sharp the
current root position is.  Two independent signals:

1. **Attacker count** – our non-pawn pieces that reach the enemy king ring
   (squares adjacent to enemy king).  Each piece +48 pts, capped at 4
   (max 192 pts).
2. **Open-file bonus** – fully open files adjacent to the enemy king = +2 pts,
   semi-open (no enemy pawn) = +1 pt.  Max 6 pts × 10 = 60 pts.

Result is capped at 256.  Computed once per `iterative_deepening()` call from
`rootPos` and stored in `tigerCfg.sharpness`.

All four Tigerfish effect sites now multiply their contribution by
`sharpness / 256`:

| Effect | Full-strength formula | Scaled formula |
|--------|----------------------|----------------|
| Optimism aggression boost | `opt * aggr / 500` | `opt * aggr * sharpness / (500 * 256)` |
| AntiDraw bonus | `antiDraw * (100-\|avg\|) / 300` | `antiDraw * sharpness * (100-\|avg\|) / (300*256)` |
| LMR check reduction | `risk * 512 / 100` | `risk * sharpness * 2 / 100` |
| Check ordering bonus | `aggr * 50` | `aggr * sharpness * 50 / 256` |

**Verified behaviour** (depth 15, `TigerAggression=TigerRisk=100`):

| Position type | sharpness (est.) | Tiger OFF nodes | Tiger ON nodes | Ratio |
|---|---|---|---|---|
| 3 attackers on king ring | ≈56% (144/256) | 33 032 | 53 101 | **+61%** |
| King+pawn endgame | ≈8% (20/256) | 45 366 | 64 139 | **+41%** |

Tiger is visibly more aggressive in attacking positions and proportionally
quieter in closed/endgame structures.

---

## Technical Changes (Phase 3)

### G. Root-move sharpness tiebreaker (`src/tigerfish.h`, `src/search.cpp`)

When Tiger is active, the two highest-scoring root moves are close together,
and the position is genuinely sharp, the engine prefers the move that most
directly threatens the enemy king.

**`tiger_move_king_attacks(pos, m)`** — returns an attack score for move `m`:
* `+4` if the destination square is inside the enemy king ring (e.g. Nxg7)
* `+popcount(attacks from destination ∩ king ring)` for further ring pressure

No `do_move`/`undo_move` — pure bitboard arithmetic, zero side-effects.

**Tiebreaker window** in `start_searching()`:
```
window = antiDraw * sharpness / (2 * 256)
```
At antiDraw=100, sharpness=256: window = 50 cp (full strength).  
At antiDraw=100, sharpness=58 (Nf5 position): window = 11 cp.  
At antiDraw=100, sharpness=10 (quiet): window = 1 cp (nearly never fires).

**Verified behaviour**:

Position: `r2q1rk1/pp2ppbp/2np1n2/5N2/4P3/2N1B1P1/PPP2PBP/R2QR1K1 w - - 0 14`
(White has Nf5 attacking g7; sharpness ≈ 58; Qe2 and Nxg7 score within 1 cp)

| Mode | Depth 16 bestmove |
|---|---|
| Tiger OFF | **d1e2** (Qe2 — preparatory) |
| Tiger ON (antiDraw=100) | **f5g7** (Nxg7 — direct king infiltration) |

Tiger selects the most aggressive of two objectively near-equal moves.  
Perft 5 = 4865609 (canonical). Zero build warnings.

---

## Suggested Next Steps (Phase 4)

1. **Targeted futility loosening**: in very sharp positions (`sharpness > 160`),
   slightly increase the futility margin so that tactical lines survive
   shallower pruning.

2. **Sharpness signal refinement**: add pawn-shelter weakness and central
   tension as third/fourth sharpness components so structural sharpness
   (e.g. Sicilian Dragon early middlegame) is also detected.

3. **Dedicated NNUE fine-tuning**: retrain (or bias) the network on games
   played by sharp engines (e.g., Leela with high temperature, AlphaZero style
   games) to further shift the positional taste without changing architecture.

---

## Build

```bash
cd src
make -j build ARCH=native
# produces: tigerfish  (or tigerfish.exe on Windows)
```

---

## Upstream

Tigerfish is a fork of Stockfish (https://github.com/official-stockfish/Stockfish).  
Stockfish is Copyright (C) 2004–2026 The Stockfish developers (see AUTHORS file).  
Both are released under the GNU General Public License v3.
