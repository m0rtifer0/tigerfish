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
//   0   = dead quiet (pure pawn endgame, fully closed structure)
//   256 = full-throttle king attack (rook on open file + knight in king ring)
//
// Four independent signals are combined:
//   1. Attacker count  – our non-pawn pieces that hit the enemy king ring.
//      Max 4 attackers × 48 = 192 pts.
//   2. Open-file bonus – open/semi-open files next to the enemy king.
//      Max 6 file-pts × 10 = 60 pts.   [GATED: requires us to have pieces]
//   3. Shelter weakness – the enemy pawn directly in front of the king is
//      missing (e.g. Dragon's g7-bishop replacing the g-pawn).
//      Max 3 files × 16 = 48 pts.       [GATED: requires us to have pieces]
//   4. Pawn storm – our pawns on the 3 files near the enemy king that have
//      crossed the centre line and can drive the attack forward.
//      Max 4 pawns × 8 = 32 pts.        [GATED: requires us to have pieces]
//
// Components 2–4 are gated behind hasMajorOrMinor so that in pure king +
// pawn endgames Tiger effects are suppressed completely (the advancing pawns
// are promotion races, not king attacks).
//
// Total is capped at 256.  O(pieces) — cheap for a once-per-search call.
// ---------------------------------------------------------------------------
inline int tiger_sharpness(const Position& pos) {

    Color    us        = pos.side_to_move();
    Color    them      = ~us;
    Square   theirKing = pos.square<KING>(them);
    Bitboard kingRing  = attacks_bb<KING>(theirKing);
    Bitboard occ       = pos.pieces();
    int      kf        = static_cast<int>(file_of(theirKing));

    // --- Component 1: pieces attacking enemy king ring ----------------------
    // Always computed — it already requires non-pawn pieces by definition.
    Bitboard attackers = pos.pieces(us) & ~pos.pieces(us, PAWN) & ~pos.pieces(us, KING);
    int kingAttackers  = 0;
    {
        Bitboard tmp = attackers;
        while (tmp)
        {
            Square s = pop_lsb(tmp);
            if (attacks_bb(pos.piece_on(s), s, occ) & kingRing)
                ++kingAttackers;
        }
    }
    int score = std::min(kingAttackers, 4) * 48;   // max 192

    // Gate: components 2–4 only fire when we have at least one major or minor
    // piece.  This suppresses Tiger in pure pawn endgames where "advancing
    // toward the enemy king" means promotion play, not a king attack.
    const bool hasMajorOrMinor = (pos.count<KNIGHT>(us) + pos.count<BISHOP>(us)
                                  + pos.count<ROOK>(us)   + pos.count<QUEEN>(us)) > 0;
    if (hasMajorOrMinor)
    {
        // --- Component 2: open / semi-open files near enemy king ------------
        // Open (no pawns at all) = 2 pts; semi-open (no enemy pawn) = 1 pt.
        int fileScore = 0;
        for (int df = -1; df <= 1; ++df)
        {
            int fi = kf + df;
            if (fi < FILE_A || fi > FILE_H)
                continue;
            Bitboard fileMask = file_bb(static_cast<File>(fi));
            if (!(pos.pieces(PAWN) & fileMask))             fileScore += 2;
            else if (!(pos.pieces(them, PAWN) & fileMask))  fileScore += 1;
        }
        score += std::min(fileScore, 6) * 10;   // max 60

        // --- Component 3: pawn shelter weakness (immediate rank) ------------
        // Check the single rank directly in front of the enemy king.
        // If that square holds anything other than an enemy pawn (e.g. the
        // Dragon's bishop on g7 replacing the g-pawn), the king is structurally
        // exposed even without active attackers yet.
        //
        // dr: direction from the king toward their centre.
        //   WHITE king → shelter is toward rank 8 (+1).
        //   BLACK king → shelter is toward rank 1 (-1).
        const int  dr            = (them == WHITE) ? 1 : -1;
        const Rank shelterRank   = static_cast<Rank>(static_cast<int>(rank_of(theirKing)) + dr);
        int        shelterMissed = 0;
        if (shelterRank >= RANK_1 && shelterRank <= RANK_8)
        {
            for (int df = -1; df <= 1; ++df)
            {
                int fi = kf + df;
                if (fi < FILE_A || fi > FILE_H)
                    continue;
                Square shelter = make_square(static_cast<File>(fi), shelterRank);
                if (pos.piece_on(shelter) != make_piece(them, PAWN))
                    ++shelterMissed;
            }
        }
        score += shelterMissed * 16;   // max 3 × 16 = 48

        // --- Component 4: pawn storm (advanced pawns near enemy king) -------
        // Our pawns on the 3 files adjacent to the enemy king that have crossed
        // the centre line contribute to a developing pawn storm.
        // Threshold: rank >= 4 for white, rank <= 5 for black.
        int stormPawns  = 0;
        Bitboard ourPwns = pos.pieces(us, PAWN);
        while (ourPwns)
        {
            Square ps = pop_lsb(ourPwns);
            if (std::abs(static_cast<int>(file_of(ps)) - kf) > 1)
                continue;
            const bool advanced = (us == WHITE) ? (rank_of(ps) >= RANK_4)
                                                : (rank_of(ps) <= RANK_5);
            if (advanced)
                ++stormPawns;
        }
        score += std::min(stormPawns, 4) * 8;   // max 4 × 8 = 32
    }

    return std::min(score, 256);
}

// ---------------------------------------------------------------------------
// tiger_move_king_attacks
//
// Estimates how many squares in the enemy king ring the moved piece will
// attack from its destination square.  Used by the root-move tiebreaker
// (Phase 3A) to prefer more aggressive moves when two candidates score
// within the tiebreaker window.
//
// Design:
//   * No do_move/undo_move — uses bitboard arithmetic only, so it is O(1)
//     and has zero side-effects on the position.
//   * Occupied bitboard is adjusted to reflect the move (piece removed from
//     source, placed at destination; captured piece already there is ignored
//     because the piece still blocks/unblocks lines in the same way).
//   * Promotions use the promoted piece type; en passant and castling return
//     0 (rare and less relevant to direct king pressure).
// ---------------------------------------------------------------------------
inline int tiger_move_king_attacks(const Position& pos, Move m) {
    if (!m.is_ok())
        return 0;

    const MoveType mt = m.type_of();
    if (mt == EN_PASSANT || mt == CASTLING)
        return 0;

    Color    us        = pos.side_to_move();
    Color    them      = ~us;
    Square   theirKing = pos.square<KING>(them);
    Bitboard kingRing  = attacks_bb<KING>(theirKing);
    Square   from      = m.from_sq();
    Square   to        = m.to_sq();

    // Occupied squares after the move: remove 'from', add 'to'.
    // (Captures: the captured piece on 'to' is replaced — same result.)
    Bitboard occ = (pos.pieces() ^ square_bb(from)) | square_bb(to);

    // Piece type after the move (promotion changes the type).
    PieceType pt = (mt == PROMOTION) ? m.promotion_type()
                                     : type_of(pos.piece_on(from));

    // Pawns and kings rarely contribute direct king-ring pressure via this
    // metric, and pawn pseudo-attacks need a colour argument — skip them.
    if (pt == PAWN || pt == KING)
        return 0;

    int score = 0;

    // Bonus for the piece landing directly inside the king ring (e.g. Nxg7).
    // A piece that infiltrates the king's immediate vicinity is strongly
    // aggressive and deserves the highest weight.
    if (square_bb(to) & kingRing)
        score += 4;

    // Count king-ring squares attacked from the new square.
    score += popcount(attacks_bb(pt, to, occ) & kingRing);

    return score;
}

}  // namespace Stockfish

#endif  // #ifndef TIGERFISH_H_INCLUDED
