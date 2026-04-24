# Tigerfish — Design Notes

This document describes the internal design of Tigerfish and how its
aggressive personality is wired into search and evaluation.

---

## Philosophy

> "Controlled aggression, not random noise."

Tigerfish is **not** an engine that sacrifices material at random. It is an
engine with a tightly calibrated set of biases that nudge search and
evaluation toward initiative-holding lines **when the position genuinely
supports a king attack**. In quiet endgames Tigerfish is indistinguishable
from a conventional strong baseline.

The four operating principles:

1. **Prefer active, initiative-holding continuations** over sterile equality.
2. **Search check, forcing, and attack lines more deeply** before pruning them.
3. **Avoid early simplifications** in near-equal middlegame positions.
4. **Never discard sound defence.** When losing, Tigerfish plays the best
   defensive move — aggression only chooses *between equally good options*.

---

## Architecture

All Tiger behaviour flows through a single struct:

```
UCI options ──► TigerConfig ──► Worker::tigerCfg ──► search / eval / ordering
```

[`TigerConfig`](src/tigerfish.h) is a plain struct with five fields:

| Field        | Source            | Meaning                                   |
|--------------|-------------------|-------------------------------------------|
| `enabled`    | `TigerMode` UCI   | Master on/off switch.                     |
| `aggression` | `TigerAggression` | 0–100. Scales optimism + check ordering.  |
| `risk`       | `TigerRisk`       | 0–100. Scales LMR reduction on checks.    |
| `antiDraw`   | `TigerAntiDraw`   | 0–100. Extra optimism near equality.      |
| `sharpness`  | auto              | 0–256. Computed per search from position. |

`TigerConfig` is read **once at the start of every iterative deepening
call**, so option changes take effect at the next `go` command without any
engine restart.

---

## Phase 1 — Branding & Option Plumbing

### Engine identity (`misc.cpp`, `uci.cpp`)

Engine name is **Tigerfish** in both `engine_version_info()` and the UCI
`id name` / `id author` output. The binary is `tigerfish`.

### UCI options (`ucioption.cpp`)

`TigerMode`, `TigerAggression`, `TigerRisk`, `TigerAntiDraw` are added as
standard UCI options. Their values are read at the start of every search.

### TigerConfig struct (`tigerfish.h`)

Single source of truth for Tiger tuning parameters. No magic numbers
scattered across `.cpp` files.

---

## Phase 2 — Sharpness Signal

`tiger_sharpness(pos)` returns an integer 0–256 estimating how
tactically sharp the current position is. Computed in O(pieces) — cheap for
a once-per-search call.

### The four components

**1. King-ring attackers** (max 192).
Our non-pawn pieces that attack any square of the enemy king ring.
Each attacker contributes 48 points, capped at 4.

**2. Open / semi-open files** (max 60, gated).
For each of the 3 files around the enemy king, +2 if the file has no pawns
at all, +1 if only our pawns are there. Capped at 6 file-points × 10.

**3. Shelter weakness** (max 48, gated).
For each of the 3 squares directly in front of the enemy king, +16 if that
square holds anything other than an enemy pawn (e.g. Dragon's g7-bishop
replacing the g-pawn).

**4. Pawn storm** (max 32, gated).
Our pawns on the 3 files adjacent to the enemy king that have crossed the
centre line count 8 points each, capped at 4 pawns.

### Gating

Components 2–4 are **gated behind "we have at least one major or minor
piece"**. This suppresses Tiger effects in pure king+pawn endgames, where
advancing pawns represent promotion races rather than king attacks.

---

## Phase 3 — Search & Evaluation Integration

### Optimism boost (evaluation)

In the NNUE evaluation blend, the optimism term receives a small additive
nudge proportional to:

```
aggression × sharpness / 256
```

This nudge is **only applied when the root score is inside ±100 cp** — it
pushes a drawish-looking middlegame toward initiative without corrupting
evaluations in decisive positions.

An extra `antiDraw`-scaled term fires when the score is within ±50 cp,
specifically targeting peaceful simplification lines.

### Check-move ordering bonus (move picker)

Quiet moves that give check receive an extra history score:

```
bonus = aggression × sharpness × c1 / (100 × 256)
```

This lifts forcing moves earlier in the move list where they get more
search attention and benefit from tighter alpha-beta pruning.

### LMR reduction on forcing moves (search)

In Late Move Reduction, checking moves receive a smaller reduction:

```
reduction -= risk × sharpness × c2 / (100 × 256)
```

At default settings this amounts to roughly a quarter-ply of additional
search depth on forcing lines.

### Root-move tiebreaker (Phase 3A)

After iterative deepening, when multiple moves score within a
sharpness-scaled window:

```
window = antiDraw × sharpness / 512    // in centipawns
```

and neither score is decisive (`|score| < VALUE_KNOWN_WIN`), Tigerfish
swaps in the move that attacks **more squares in the enemy king ring**,
computed via `tiger_move_king_attacks(pos, m)` without any do/undo.

This tiebreaker makes Tiger's personality **visible in the engine's move
choice**, not only in search statistics.

---

## The Sharpness Multiplier

Every Tiger effect is scaled by `(sharpness / 256)`. This single design
choice is what makes Tigerfish safe to use as a general-purpose engine:

| Position type          | Sharpness | Tiger effect           |
|------------------------|-----------|------------------------|
| Dead pawn endgame      | ~0        | Zero.                  |
| Quiet middlegame       | 20–60     | Barely noticeable.     |
| Developing attack      | 80–150    | Moderate.              |
| Open king / sacrifices | 180–256   | Fully active.          |

The engine never "plays badly" in endgames because there are no effects to
apply. The engine never holds back in king-attack positions because
sharpness is saturated.

---

## Safety Invariants

Every Tiger change must respect these rules:

1. **Bounded.** Every magic constant has a numeric cap. No unbounded
   bonuses.
2. **Gated by `TigerMode`.** All effects vanish when the master switch is
   false.
3. **Scaled by sharpness.** Effects fade to zero in quiet positions.
4. **Decisive-score safe.** Root tiebreaker and optimism nudges never fire
   when `|score| >= VALUE_KNOWN_WIN`.
5. **No unsafe search pruning.** Forcing-move LMR reduction is always
   bounded by `reduction >= 1` to avoid null-move-like exposures.
6. **Auditable.** Every new magic number goes into `TigerConfig`, never
   scattered across `.cpp` files.

---

## Training Pipeline (Phase 4)

The `generate_training_data` UCI command generates self-play `.binpack`
data directly inside the engine. All Tiger modifications are active during
self-play, so the data reflects Tigerfish's actual style and the fine-tuned
net learns a consistent evaluation space.

Implementation: [`src/tools/training_data_generator.cpp`](src/tools/training_data_generator.cpp)
and [`src/tools/sfen_packer.cpp`](src/tools/sfen_packer.cpp).

Key design choices:

- **Single-game-at-a-time**, multi-thread search. All CPU threads contribute
  to every position search, giving stronger labels than parallel games at
  single-thread depth.
- **Fixed-depth search**, with depth typically 8–12 for the data generator.
- **Adjudication rules**: resignation at `|score| >= eval_limit`, draw at
  8 consecutive `|score| <= 0` scores after ply 80, max 400 plies.
- **Random opening moves** (default 8) sampled from uniform-legal to
  broaden the position distribution.
- **16-ply `write_minply`** — the first 16 plies are not recorded, since
  they contain forced random openings with low-quality labels.

---

## File Map

| Area                       | Files                                                                                                    |
|----------------------------|----------------------------------------------------------------------------------------------------------|
| Tiger core                 | [`tigerfish.h`](src/tigerfish.h)                                                                         |
| Search integration         | [`search.cpp`](src/search.cpp), [`search.h`](src/search.h)                                               |
| Evaluation integration     | [`evaluate.cpp`](src/evaluate.cpp), [`evaluate.h`](src/evaluate.h)                                       |
| Move ordering              | [`movepick.cpp`](src/movepick.cpp)                                                                       |
| UCI options                | [`ucioption.cpp`](src/ucioption.cpp), [`engine.cpp`](src/engine.cpp), [`uci.cpp`](src/uci.cpp)           |
| Training data generator    | [`tools/training_data_generator.cpp`](src/tools/training_data_generator.cpp), [`tools/sfen_packer.cpp`](src/tools/sfen_packer.cpp) |
| Python tooling             | [`training/selfplay.py`](training/selfplay.py), [`training/train_tiger.py`](training/train_tiger.py), [`training/export_nnue.py`](training/export_nnue.py), [`training/embed_net.py`](training/embed_net.py) |

---

## Future Work

- **Phase 5**: NNUE architecture search (feature transformer changes that
  encode king-ring activity directly).
- **Phase 6**: Opening-book integration tuned for sharp systems.
- **Phase 7**: Tunable piece-specific aggression (e.g. `TigerQueenAggression`).
