#pragma once
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------
// Fundamental type aliases
// ---------------------------------------------------------------------

// A Bitboard is a 64-bit integer. Bit i (0-63) represents square i.
// Bit set  = a piece of that kind/color occupies that square.
// Bit clear = it does not.
using Bitboard = uint64_t;

// ---------------------------------------------------------------------
// Squares
// ---------------------------------------------------------------------
// We number squares 0-63, starting at a1 = 0, b1 = 1, ..., h1 = 7,
// a2 = 8, ..., h8 = 63. This is the standard "Little-Endian
// Rank-File" (LERF) mapping used by most bitboard engines,
// which makes bit i correspond directly to square i.
enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    NO_SQUARE = 64
};

inline int fileOf(int sq) { return sq & 7; }       // 0=a ... 7=h
inline int rankOf(int sq) { return sq >> 3; }       // 0=rank1 ... 7=rank8

inline int squareOf(int file, int rank) { return rank * 8 + file; }

// Convert e.g. "e4" -> Square, and Square -> "e4"
Square squareFromString(const std::string& s);
std::string squareToString(int sq);

// ---------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------
enum Color : int { WHITE = 0, BLACK = 1, NO_COLOR = 2 };

inline Color operator~(Color c) { return static_cast<Color>(c ^ 1); }

// ---------------------------------------------------------------------
// Piece types (color-independent) and full pieces (color-specific)
// ---------------------------------------------------------------------
enum PieceType : int {
    PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    NO_PIECE_TYPE = 6
};

// Piece encodes both color and type: 0-5 = white P..K, 6-11 = black P..K
enum Piece : int {
    WHITE_PAWN = 0, WHITE_KNIGHT, WHITE_BISHOP, WHITE_ROOK, WHITE_QUEEN, WHITE_KING,
    BLACK_PAWN, BLACK_KNIGHT, BLACK_BISHOP, BLACK_ROOK, BLACK_QUEEN, BLACK_KING,
    NO_PIECE = 12
};

inline Piece makePiece(Color c, PieceType pt) {
    return static_cast<Piece>(c * 6 + pt);
}
inline PieceType typeOf(Piece p) { return static_cast<PieceType>(p % 6); }
inline Color colorOf(Piece p) { return static_cast<Color>(p / 6); }

char pieceToChar(Piece p);      // e.g. WHITE_KNIGHT -> 'N', BLACK_KNIGHT -> 'n'
Piece charToPiece(char c);      // 'N' -> WHITE_KNIGHT, 'n' -> BLACK_KNIGHT

// ---------------------------------------------------------------------
// Castling rights
// ---------------------------------------------------------------------
// Stored as a 4-bit mask so we can cheaply combine/check/clear rights.
enum CastlingRight : int {
    WHITE_KINGSIDE  = 1,
    WHITE_QUEENSIDE = 2,
    BLACK_KINGSIDE  = 4,
    BLACK_QUEENSIDE = 8
};
using CastlingRights = uint8_t;
