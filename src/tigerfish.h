/*
  Tigerfish – a UCI chess engine derived from Stockfish.
  Tigerfish is free software licensed under the GNU GPLv3 License.
  Upstream Stockfish copyright: Copyright (C) 2004-2026 The Stockfish developers
  (see AUTHORS file). Tigerfish modifications are layered on top without altering
  the upstream licensing or authorship of the original code.
*/

#ifndef TIGERFISH_H_INCLUDED
#define TIGERFISH_H_INCLUDED

#include <algorithm>

#include "bitboard.h"
#include "position.h"

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

    // 0-256.  Per-search positional sharpness of the root position.
    //   Computed once per iterative_deepening() call via tiger_sharpness(rootPos).
    //   All Tiger effects are multiplied by (sharpness / 256) so they scale up in
    //   genuinely attacking positions and fade out in quiet/endgame structures.
    //   Never set by the user — derived automatically.
    int sharpness = 0;
};

// ---------------------------------------------------------------------------
// tiger_sharpness
//
// Estimates how tactically sharp/attacking the current position is.
// Returns 0-256:
//   0   = dead quiet (closed endgame, sterile structure)
//   256 = full-throttle king attack
//
// Two independent signals are combined:
//   1. Attacker count  – our non-pawn pieces that hit the enemy king ring.
//      Each piece contributes up to 4 * 48 = 192 pts.
//   2. Open-file bonus – open/semi-open files next to the enemy king
//      that provide highways for rooks and queens.
//      Up to 6 file-points * 10 = 60 pts.
//
// Total is capped at 256.  The computation is O(pieces) — cheap enough to
// call once per iterative_deepening() call.
// ---------------------------------------------------------------------------
inline int tiger_sharpness(const Position& pos) {

    Color    us        = pos.side_to_move();
    Color    them      = ~us;
    Square   theirKing = pos.square<KING>(them);
    Bitboard kingRing  = attacks_bb<KING>(theirKing);
    Bitboard occ       = pos.pieces();

    // --- Component 1: pieces attacking enemy king ring ----------------------
    // Only non-pawn, non-king pieces of the side to move are considered.
    Bitboard attackers = pos.pieces(us) & ~pos.pieces(us, PAWN) & ~pos.pieces(us, KING);
    int kingAttackers  = 0;
    while (attackers)
    {
        Square s = pop_lsb(attackers);
        if (attacks_bb(pos.piece_on(s), s, occ) & kingRing)
            ++kingAttackers;
    }
    int score = std::min(kingAttackers, 4) * 48;   // max 192

    // --- Component 2: open / semi-open files near enemy king ----------------
    // Fully open (no pawns at all) = 2 pts; semi-open (no enemy pawn) = 1 pt.
    // We check the king file plus one file on each side.
    int kf        = static_cast<int>(file_of(theirKing));
    int fileScore = 0;
    for (int df = -1; df <= 1; ++df)
    {
        int fi = kf + df;
        if (fi < FILE_A || fi > FILE_H)
            continue;
        Bitboard fileMask = file_bb(static_cast<File>(fi));
        if (!(pos.pieces(PAWN) & fileMask))             fileScore += 2;  // open
        else if (!(pos.pieces(them, PAWN) & fileMask))  fileScore += 1;  // semi-open
    }
    score += std::min(fileScore, 6) * 10;   // max 60

    return std::min(score, 256);
}

}  // namespace Stockfish

#endif  // #ifndef TIGERFISH_H_INCLUDED
