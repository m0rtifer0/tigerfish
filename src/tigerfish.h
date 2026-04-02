/*
  Tigerfish – a UCI chess engine derived from Stockfish.
  Tigerfish is free software licensed under the GNU GPLv3 License.
  Upstream Stockfish copyright: Copyright (C) 2004-2026 The Stockfish developers
  (see AUTHORS file). Tigerfish modifications are layered on top without altering
  the upstream licensing or authorship of the original code.
*/

#ifndef TIGERFISH_H_INCLUDED
#define TIGERFISH_H_INCLUDED

namespace Stockfish {

// ---------------------------------------------------------------------------
// TigerConfig
//
// Holds all Tigerfish-specific tuning parameters, parsed once per search from
// UCI options and propagated through iterative deepening, search, and eval.
//
// Design goals:
//   - No magic numbers scattered across search.cpp / evaluate.cpp.
//   - All Tiger effects are bounded (safe to set to 0 for pure Stockfish behaviour).
//   - A single struct makes it easy to forward-declare and pass as needed.
// ---------------------------------------------------------------------------
struct TigerConfig {

    // Master on/off switch.  When false every other field is ignored and the
    // engine behaves identically to vanilla Stockfish.
    bool enabled = false;

    // 0-100.  Controls how eagerly Tiger pursues initiative and attacking play:
    //   * Scales the optimism boost that feeds into NNUE evaluation blending.
    //   * Scales the extra check-bonus added to quiet-move ordering scores.
    // Default 50 gives a moderate, balanced aggression profile.
    int aggression = 50;

    // 0-100.  Controls how conservatively Tiger prunes tactical/forcing lines:
    //   * Reduces LMR for moves that give check, keeping attack lines deeper.
    // Default 50 means checking moves get ~0.25 plies more search depth.
    int risk = 50;

    // 0-100.  Controls preference for complications near equality:
    //   * Adds a flat optimism bonus when the root score is inside ±100 cp,
    //     nudging the engine toward initiative rather than peaceful simplification.
    // Default 50 gives a noticeable but not overwhelming anti-draw nudge.
    int antiDraw = 50;
};

}  // namespace Stockfish

#endif  // #ifndef TIGERFISH_H_INCLUDED
