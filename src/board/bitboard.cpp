#include "bitboard.hpp"
#include <sstream>

std::string bitboardToString(Bitboard bb) {
    std::ostringstream out;
    for (int rank = 7; rank >= 0; --rank) {
        out << (rank + 1) << "  ";
        for (int file = 0; file < 8; ++file) {
            int sq = squareOf(file, rank);
            out << (testBit(bb, sq) ? '1' : '.') << ' ';
        }
        out << '\n';
    }
    out << "   a b c d e f g h\n";
    return out.str();
}
