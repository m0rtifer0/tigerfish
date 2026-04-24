# Contributing to Tigerfish

Thanks for your interest in improving Tigerfish. This document covers the
contribution workflow and the design rules specific to Tiger-related
changes.

---

## Development Setup

```bash
git clone <your-fork-url> tigerfish
cd tigerfish/src
make -j$(nproc) ARCH=x86-64-avx2 COMP=gcc all
```

You should see `tigerfish` built in `src/`. Smoke test:

```bash
echo -e "uci\nposition startpos\ngo depth 10\nquit" | ./src/tigerfish
```

---

## Project Structure

See [README.md](README.md) for the high-level layout and
[DESIGN.md](DESIGN.md) for the internal design rationale.

---

## Tiger Design Rules

Tiger-related changes **must** respect these invariants:

1. **Bounded.** Every magic constant has a numeric cap. No unbounded
   bonuses, no reductions that can exceed the search depth.
2. **Gated by `TigerMode`.** All effects must vanish when the master
   switch is false.
3. **Scaled by sharpness.** Effects should fade to zero in quiet
   positions via the `tiger_sharpness()` multiplier.
4. **Decisive-score safe.** Root tiebreakers and evaluation nudges must
   not fire when `|score| >= VALUE_KNOWN_WIN`.
5. **Auditable.** Every new magic number goes into `TigerConfig` in
   [`src/tigerfish.h`](src/tigerfish.h).

A change that violates any of these is very unlikely to be accepted.

---

## Testing

Before opening a PR:

1. **Build clean.** `make clean && make -j ARCH=x86-64-avx2 COMP=gcc all`
   must finish with no warnings you introduced.
2. **Run a bench.** `./src/tigerfish bench` should succeed and produce a
   deterministic node count.
3. **Smoke-test Tiger options.** Verify that every Tiger option toggles
   correctly via UCI.
4. **For search / eval changes**: run at least one SPRT-style pair
   (old binary vs new binary) at short time control. Include the result
   in the PR description.

---

## Commit Style

- Small, focused commits. One logical change per commit.
- Imperative mood subject line, ≤ 70 characters.
- Body (optional) wraps at 72 characters and explains *why* the change
  is needed, not *what* the diff does.

Example:

```
Scale optimism boost by sharpness ratio

Previously the boost applied full magnitude in all positions, which
produced unhelpful eval inflation in pawn endgames. Scaling by
(sharpness / 256) makes the effect proportional to the position's
attacking potential.
```

---

## Pull Requests

1. Fork, create a topic branch off `master`.
2. Make your changes, keeping commits small and focused.
3. Open a PR with a short summary, the rationale, and test results.
4. Respond to review feedback by pushing new commits to the same
   branch — do not squash until the PR is approved.

---

## License

By contributing, you agree that your contributions will be licensed
under the GPLv3 license, the same license as the rest of Tigerfish.
