#ifndef _SFEN_PACKER_H_
#define _SFEN_PACKER_H_

#include "packed_sfen.h"

namespace Tigerfish {
class Position;
}

namespace Tigerfish::Tools {

    // Pack a position into a 32-byte PackedSfen for training data output.
    PackedSfen sfen_pack(Position& pos, bool resetCastlingRights);
}

#endif
