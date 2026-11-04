#include "types.hpp"
#include <cctype>
#include <stdexcept>

Square squareFromString(const std::string& s) {
    if (s.size() != 2) throw std::invalid_argument("Invalid square string: " + s);
    int file = s[0] - 'a';
    int rank = s[1] - '1';
    if (file < 0 || file > 7 || rank < 0 || rank > 7)
        throw std::invalid_argument("Invalid square string: " + s);
    return static_cast<Square>(squareOf(file, rank));
}

std::string squareToString(int sq) {
    if (sq < 0 || sq > 63) return "-";
    std::string s;
    s += static_cast<char>('a' + fileOf(sq));
    s += static_cast<char>('1' + rankOf(sq));
    return s;
}

char pieceToChar(Piece p) {
    static const char chars[12] = {
        'P','N','B','R','Q','K',   // white
        'p','n','b','r','q','k'    // black
    };
    if (p == NO_PIECE) return '.';
    return chars[p];
}

Piece charToPiece(char c) {
    switch (c) {
        case 'P': return WHITE_PAWN;
        case 'N': return WHITE_KNIGHT;
        case 'B': return WHITE_BISHOP;
        case 'R': return WHITE_ROOK;
        case 'Q': return WHITE_QUEEN;
        case 'K': return WHITE_KING;
        case 'p': return BLACK_PAWN;
        case 'n': return BLACK_KNIGHT;
        case 'b': return BLACK_BISHOP;
        case 'r': return BLACK_ROOK;
        case 'q': return BLACK_QUEEN;
        case 'k': return BLACK_KING;
        default:  return NO_PIECE;
    }
}
