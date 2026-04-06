#include "sfen_packer.h"

#include "packed_sfen.h"

#include "misc.h"
#include "position.h"

#include <sstream>
#include <fstream>
#include <cstring> // std::memset()

using namespace std;

namespace Stockfish::Tools {

    // Class that handles bitstream
    struct BitStream
    {
        void set_data(std::uint8_t* data_) { data = data_; reset(); }
        uint8_t* get_data() const { return data; }
        int get_cursor() const { return bit_cursor; }
        void reset() { bit_cursor = 0; }

        void write_one_bit(int b)
        {
            if (b)
                data[bit_cursor / 8] |= 1 << (bit_cursor & 7);
            ++bit_cursor;
        }

        int read_one_bit()
        {
            int b = (data[bit_cursor / 8] >> (bit_cursor & 7)) & 1;
            ++bit_cursor;
            return b;
        }

        void write_n_bit(int d, int n)
        {
            for (int i = 0; i < n; ++i)
                write_one_bit(d & (1 << i));
        }

        int read_n_bit(int n)
        {
            int result = 0;
            for (int i = 0; i < n; ++i)
                result |= read_one_bit() ? (1 << i) : 0;
            return result;
        }

    private:
        int bit_cursor;
        std::uint8_t* data;
    };

    struct SfenPacker
    {
        void pack(const Position& pos, bool resetCastlingRights);

        uint8_t *data;
        BitStream stream;

        void write_board_piece_to_stream(Piece pc);
        Piece read_board_piece_from_stream();
    };

    // Huffman coding table
    struct HuffmanedPiece
    {
        int code;
        int bits;
    };

    constexpr HuffmanedPiece huffman_table[] =
    {
        {0b0000,1}, // NO_PIECE
        {0b0001,4}, // PAWN
        {0b0011,4}, // KNIGHT
        {0b0101,4}, // BISHOP
        {0b0111,4}, // ROOK
        {0b1001,4}, // QUEEN
    };

    // Pack sfen and store in data[32].
    void SfenPacker::pack(const Position& pos, bool resetCastlingRights)
    {
        memset(data, 0, 32);
        stream.set_data(data);

        // Side to move
        stream.write_one_bit((int)(pos.side_to_move()));

        // King positions (6 bits each)
        for (auto c : {WHITE, BLACK})
            stream.write_n_bit(pos.square<KING>(c), 6);

        // Board pieces (excluding kings)
        for (Rank r = RANK_8; r >= RANK_1; --r)
        {
            for (File f = FILE_A; f <= FILE_H; ++f)
            {
                Piece pc = pos.piece_on(make_square(f, r));
                if (type_of(pc) == KING)
                    continue;
                write_board_piece_to_stream(pc);
            }
        }

        // Castling rights
        if (resetCastlingRights)
        {
            stream.write_n_bit(0, 4);
        }
        else
        {
            stream.write_one_bit(pos.can_castle(WHITE_OO));
            stream.write_one_bit(pos.can_castle(WHITE_OOO));
            stream.write_one_bit(pos.can_castle(BLACK_OO));
            stream.write_one_bit(pos.can_castle(BLACK_OOO));
        }

        // En passant
        if (pos.ep_square() == SQ_NONE) {
            stream.write_one_bit(0);
        }
        else {
            stream.write_one_bit(1);
            stream.write_n_bit(static_cast<int>(pos.ep_square()), 6);
        }

        // Rule50
        stream.write_n_bit(pos.rule50_count(), 6);

        // Fullmove number
        const int fm = 1 + (pos.game_ply() - (pos.side_to_move() == BLACK)) / 2;
        stream.write_n_bit(fm, 8);

        // High bits of fullmove (backwards compatible extension)
        stream.write_n_bit(fm >> 8, 8);

        // Highest bit of rule50 (backwards compatible extension)
        stream.write_n_bit(pos.rule50_count() >> 6, 1);

        assert(stream.get_cursor() <= 256);
    }

    void SfenPacker::write_board_piece_to_stream(Piece pc)
    {
        PieceType pr = type_of(pc);
        auto c = huffman_table[pr];
        stream.write_n_bit(c.code, c.bits);

        if (pc == NO_PIECE)
            return;

        stream.write_one_bit(color_of(pc));
    }

    Piece SfenPacker::read_board_piece_from_stream()
    {
        PieceType pr = NO_PIECE_TYPE;
        int code = 0, bits = 0;
        while (true)
        {
            code |= stream.read_one_bit() << bits;
            ++bits;

            assert(bits <= 6);

            for (pr = NO_PIECE_TYPE; pr < KING; ++pr)
                if (huffman_table[pr].code == code
                    && huffman_table[pr].bits == bits)
                    goto Found;
        }
    Found:;
        if (pr == NO_PIECE_TYPE)
            return NO_PIECE;

        Color c = (Color)stream.read_one_bit();
        return make_piece(c, pr);
    }

    PackedSfen sfen_pack(Position& pos, bool resetCastlingRights)
    {
        PackedSfen sfen;

        SfenPacker sp;
        sp.data = (uint8_t*)&sfen;
        sp.pack(pos, resetCastlingRights);

        return sfen;
    }
}
