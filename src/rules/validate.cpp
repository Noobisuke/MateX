#include "validate.hpp"
#include "../board/attacks.hpp"

namespace {

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

}  // namespace

bool validatePosition(const Position& pos, std::string* error) {
    const Bitboard whiteKing = pos.pieces(WHITE, KING);
    const Bitboard blackKing = pos.pieces(BLACK, KING);

    if (popCount(whiteKing) != 1) return fail(error, "White must have exactly one king");
    if (popCount(blackKing) != 1) return fail(error, "Black must have exactly one king");

    const int wk = lsbIndex(whiteKing);
    const int bk = lsbIndex(blackKing);
    if (testBit(kingAttacksFrom(wk), bk)) return fail(error, "The kings are adjacent");

    const Bitboard backRanks = 0xFF000000000000FFULL;  // ranks 1 and 8
    const Bitboard pawns = pos.pieces(WHITE, PAWN) | pos.pieces(BLACK, PAWN);
    if (pawns & backRanks) return fail(error, "A pawn stands on rank 1 or 8");

    for (Color c : {WHITE, BLACK}) {
        const char* name = (c == WHITE) ? "White" : "Black";
        if (popCount(pos.pieces(c, PAWN)) > 8) return fail(error, std::string(name) + " has more than 8 pawns");
        if (popCount(pos.occupancy(c)) > 16) return fail(error, std::string(name) + " has more than 16 pieces");
    }

    const Color stm = pos.sideToMove();
    if (isInCheck(pos, ~stm)) {
        return fail(error, "The side not to move is in check (the king could be captured)");
    }

    const int ep = pos.enPassantSquare();
    if (ep != NO_SQUARE) {
        // White to move: Black just double-pushed, so ep is on rank 6 (index 5), the pawn
        // stands one rank below it and came from one rank above it. Black to move: mirrored.
        const int expectedRank = (stm == WHITE) ? 5 : 2;
        const int victim = (stm == WHITE) ? ep - 8 : ep + 8;
        const int origin = (stm == WHITE) ? ep + 8 : ep - 8;
        if (rankOf(ep) != expectedRank) return fail(error, "En passant square is on the wrong rank for the side to move");
        if (pos.pieceOn(ep) != NO_PIECE) return fail(error, "En passant square is occupied");
        if (pos.pieceOn(victim) != makePiece(~stm, PAWN)) return fail(error, "No pawn stands in front of the en passant square");
        if (pos.pieceOn(origin) != NO_PIECE) return fail(error, "The en passant pawn's origin square is occupied");
    }

    return true;
}