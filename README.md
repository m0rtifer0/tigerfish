# Tigerfish

> An aggressive-style UCI chess engine.

Tigerfish is a chess engine that values **initiative** over equality.
Every feature in the engine — the evaluation blend, move ordering, late-move
reductions, root-move tiebreakers — is built around a single goal: when two
moves are roughly equal, pick the one that keeps the game alive.

Tigerfish speaks the Universal Chess Interface (UCI) protocol and works with
any standard chess GUI (CuteChess, Arena, BanksiaGUI, Nibbler, lichess-bot,
etc.).

---

## Highlights

- **Four dials, one personality.** `TigerMode`, `TigerAggression`, `TigerRisk`
  and `TigerAntiDraw` give you direct control over the engine's style from the
  GUI without rebuilding.
- **Sharpness-aware effects.** Every Tiger bias is scaled by a positional
  *sharpness* signal (0–256). In quiet endgames the engine plays with a cool
  head; in king-attack positions it leans in hard.
- **Built-in self-play data generator.** The `generate_training_data` UCI
  command produces `.binpack` training data directly inside the engine with
  all Tiger modifications active — ready to feed into any NNUE trainer.
- **Strong by default.** With `TigerMode = false` the engine plays a
  conventional, strong baseline. Tiger features are additive and bounded.

---

## Quick Start

### Build

```bash
cd src
make -j$(nproc) ARCH=x86-64-avx2 COMP=gcc all
```

Other common architectures:

```bash
make -j ARCH=x86-64-avx512 COMP=gcc all   # AVX-512 CPUs
make -j ARCH=x86-64-bmi2  COMP=gcc all    # Haswell-class CPUs
make -j ARCH=apple-silicon COMP=clang all # M-series Macs
```

Run `make help` from `src/` for the full list of supported architectures.

### Play

```bash
./src/tigerfish
```

Basic session:

```
uci
setoption name Hash value 1024
setoption name Threads value 8
setoption name TigerMode value true
setoption name TigerAggression value 70
setoption name TigerAntiDraw value 60
isready
position startpos
go depth 20
```

---

## UCI Options

### Standard

| Option          | Type  | Default  | Range            |
|-----------------|-------|----------|------------------|
| `Threads`       | spin  | 1        | 1–1024           |
| `Hash`          | spin  | 16 MB    | 1–33554432       |
| `MultiPV`       | spin  | 1        | 1–256            |
| `Skill Level`   | spin  | 20       | 0–20             |
| `Move Overhead` | spin  | 10 ms    | 0–5000           |
| `UCI_Chess960`  | check | false    | –                |
| `SyzygyPath`    | string| –        | endgame tablebases |
| `EvalFile`      | string| embedded | path to .nnue    |

### Tiger-specific

| Option            | Type  | Default | Range   | Effect                                                         |
|-------------------|-------|---------|---------|----------------------------------------------------------------|
| `TigerMode`       | check | false   | –       | Master switch. False = no Tiger effects.                       |
| `TigerAggression` | spin  | 50      | 0–100   | Optimism boost + check-move ordering bonus.                    |
| `TigerRisk`       | spin  | 50      | 0–100   | Reduces late-move reduction on forcing moves.                  |
| `TigerAntiDraw`   | spin  | 50      | 0–100   | Extra optimism near equal scores; scales root-move tiebreaker. |

All Tiger effects are **gated by the `TigerMode` switch** and further **scaled
by positional sharpness**, so they stay dormant in dead-drawn endgames and
fully active in attacking middlegames.

---

## Tiger Personality, in Detail

Tiger effects combine five independent mechanisms:

1. **Sharpness signal** (`tiger_sharpness()` in [`src/tigerfish.h`](src/tigerfish.h))
   Integer 0–256 derived once per search from:
   - Pieces attacking the enemy king ring (max 192 pts).
   - Open/semi-open files near the enemy king (max 60 pts, gated).
   - Missing shelter pawns in front of the enemy king (max 48 pts, gated).
   - Advanced pawns on files adjacent to the enemy king (max 32 pts, gated).
   The non-attacker components are gated behind *"we have at least one major
   or minor"* so pawn endgames don't accidentally trigger king-attack logic.

2. **Optimism boost** (evaluation). When the score is close to 0,
   `TigerAggression` and `TigerAntiDraw` nudge the side-to-move's evaluation
   up by a small, sharpness-scaled amount — just enough to prefer keeping the
   game alive over taking a peaceful draw.

3. **Check-move ordering bonus** (move ordering). Quiet moves that give check
   get a history bonus proportional to `TigerAggression × sharpness`. They
   surface earlier in the move list and get more search attention.

4. **Forcing-move LMR reduction** (search). Checking moves receive a smaller
   late-move reduction proportional to `TigerRisk × sharpness`. At default
   settings this is roughly a quarter-ply of extra search on forcing lines.

5. **Root-move tiebreaker** (root). When two root moves score within
   `(TigerAntiDraw × sharpness / 512)` cp of each other and neither is
   decisive, Tigerfish prefers the move that attacks more squares in the
   enemy king ring. This makes the aggressive personality visible in the
   actual move choice, not just in search statistics.

All five components degrade to zero when `TigerMode = false`, when the
position is quiet, or when the score is decisive. Nothing is added to dead
positions.

---

## NNUE Network

Tigerfish uses two NNUE networks (big + small) embedded in the binary at
compile time via the `incbin` mechanism. The default filenames are declared
in [`src/evaluate.h`](src/evaluate.h) and must be present in `src/` at build
time.

To replace the embedded net:

1. Place the new `nn-XXXX.nnue` alongside the sources in `src/`.
2. Update `EvalFileDefaultNameBig` in [`src/evaluate.h`](src/evaluate.h).
3. Rebuild.

---

## Self-Play Data Generation

Tigerfish exposes a `generate_training_data` UCI command that runs self-play
games at a fixed depth and writes the positions to a `.binpack` file. All
Tiger modifications are active during self-play, so the generated data
reflects Tigerfish's actual style.

```bash
echo "generate_training_data depth 9 count 10000000 output_file_name train.binpack" \
  | ./src/tigerfish
```

Parameters:

| Parameter            | Default  | Meaning                                          |
|----------------------|----------|--------------------------------------------------|
| `depth`              | 9        | Fixed search depth per position                  |
| `count`              | 10000000 | Target number of positions                       |
| `eval_limit`         | 3000     | Absolute cp value above which the game is resigned |
| `random_move_count`  | 8        | Opening random moves for position diversity     |
| `write_min_ply`      | 16       | First ply recorded (skips forced random opening) |
| `write_max_ply`      | 400      | Maximum game length before draw adjudication    |
| `keep_draws`         | 1        | Include drawn games in the output               |
| `output_file_name`   | –        | Output path (`.binpack` suffix added automatically) |

The output `.binpack` file can be consumed by any standard NNUE trainer.

---

## Project Layout

```
tigerfish/
├── src/                          # Engine source code
│   ├── tigerfish.h               # TigerConfig, tiger_sharpness, tiger_move_king_attacks
│   ├── search.cpp / search.h     # Tiger-aware iterative deepening & LMR
│   ├── evaluate.cpp / evaluate.h # Tiger optimism blending
│   ├── uci.cpp / ucioption.cpp   # UCI protocol + Tiger options
│   ├── nnue/                     # NNUE architecture + network loader
│   ├── tools/                    # generate_training_data + sfen packer
│   └── Makefile                  # Build system
├── README.md
├── DESIGN.md                     # Deep-dive design notes
├── CHANGELOG.md                  # Release history
├── CONTRIBUTING.md               # Contribution guide
├── AUTHORS                       # List of contributors
└── Copying.txt                   # GPLv3 license text
```

---

## Testing Tiger Behaviour

Verify that Tigerfish changes its move choice in sharp positions:

```bash
# Quiet endgame — Tiger should be dormant (no effect).
echo -e "setoption name TigerMode value true\nposition fen 8/8/8/8/4k3/8/4K3/8 w - - 0 1\ngo depth 15\nquit" \
  | ./src/tigerfish | grep "bestmove"

# King attack position — Tiger should prefer attacking moves.
echo -e "setoption name TigerMode value true\nsetoption name TigerAggression value 90\nposition fen r2qkb1r/ppp2ppp/2n5/3pp3/2B1P3/2N5/PPPP1PPP/R1BQK2R w KQkq - 0 1\ngo depth 15\nquit" \
  | ./src/tigerfish | grep "bestmove"
```

---

## Contributing

Pull requests are welcome. Tiger-related changes should respect the design
rules:

- **Bounded.** Every effect must have a numeric cap. No unbounded bonuses.
- **Gated.** Tiger effects must be disabled when `TigerMode = false`.
- **Scaled.** Effects should scale with `tiger_sharpness()` so they stay out
  of the way in quiet positions.
- **Auditable.** Every new magic number goes into `TigerConfig` in
  [`src/tigerfish.h`](src/tigerfish.h), never scattered across `.cpp` files.

---

## License

Tigerfish is free software distributed under the
[GNU General Public License v3](Copying.txt) (or any later version).

See [AUTHORS](AUTHORS) for the list of contributors.
