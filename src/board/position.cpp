#include "position.hpp"
#include <sstream>

Position::Position() {
    clear();
}

void Position::clear() {
    board_.fill(NO_PIECE);
    pieceBB_.fill(EMPTY_BB);
    occupancyByColor_.fill(EMPTY_BB);
    occupancyAll_ = EMPTY_BB;

    sideToMove_ = WHITE;
    castlingRights_ = 0;
    enPassantSquare_ = NO_SQUARE;
    halfmoveClock_ = 0;
    fullmoveNumber_ = 1;
}

void Position::putPiece(Piece p, int sq) {
    // Defensive: if a piece already sits here, remove it first so the
    // bitboards/mailbox never get out of sync (e.g. accidental double-set).
    if (board_[sq] != NO_PIECE) {
        removePiece(sq);
    }
    board_[sq] = p;
    setBit(pieceBB_[p], sq);
    recomputeOccupancy();
}

void Position::removePiece(int sq) {
    Piece p = board_[sq];
    if (p == NO_PIECE) return;
    board_[sq] = NO_PIECE;
    clearBit(pieceBB_[p], sq);
    recomputeOccupancy();
}

void Position::recomputeOccupancy() {
    occupancyByColor_[WHITE] = EMPTY_BB;
    occupancyByColor_[BLACK] = EMPTY_BB;
    for (int p = WHITE_PAWN; p <= WHITE_KING; ++p)
        occupancyByColor_[WHITE] |= pieceBB_[p];
    for (int p = BLACK_PAWN; p <= BLACK_KING; ++p)
        occupancyByColor_[BLACK] |= pieceBB_[p];
    occupancyAll_ = occupancyByColor_[WHITE] | occupancyByColor_[BLACK];
}

void Position::setStartingPosition() {
    clear();

    // Back ranks
    const PieceType backRank[8] = {
        ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK
    };

    for (int file = 0; file < 8; ++file) {
        putPiece(makePiece(WHITE, backRank[file]), squareOf(file, 0)); // rank 1
        putPiece(makePiece(WHITE, PAWN),           squareOf(file, 1)); // rank 2
        putPiece(makePiece(BLACK, PAWN),           squareOf(file, 6)); // rank 7
        putPiece(makePiece(BLACK, backRank[file]), squareOf(file, 7)); // rank 8
    }

    sideToMove_ = WHITE;
    castlingRights_ = WHITE_KINGSIDE | WHITE_QUEENSIDE | BLACK_KINGSIDE | BLACK_QUEENSIDE;
    enPassantSquare_ = NO_SQUARE;
    halfmoveClock_ = 0;
    fullmoveNumber_ = 1;
}

std::string Position::toString() const {
    std::ostringstream out;
    for (int rank = 7; rank >= 0; --rank) {
        out << (rank + 1) << "  ";
        for (int file = 0; file < 8; ++file) {
            int sq = squareOf(file, rank);
            out << pieceToChar(board_[sq]) << ' ';
        }
        out << '\n';
    }
    out << "   a b c d e f g h\n\n";

    out << "Side to move: " << (sideToMove_ == WHITE ? "White" : "Black") << '\n';

    out << "Castling: ";
    if (castlingRights_ == 0) {
        out << "-";
    } else {
        if (castlingRights_ & WHITE_KINGSIDE)  out << 'K';
        if (castlingRights_ & WHITE_QUEENSIDE) out << 'Q';
        if (castlingRights_ & BLACK_KINGSIDE)  out << 'k';
        if (castlingRights_ & BLACK_QUEENSIDE) out << 'q';
    }
    out << '\n';

    out << "En passant: "
        << (enPassantSquare_ == NO_SQUARE ? "-" : squareToString(enPassantSquare_)) << '\n';
    out << "Halfmove clock: " << halfmoveClock_ << '\n';
    out << "Fullmove number: " << fullmoveNumber_ << '\n';

    return out.str();
}

bool Position::isConsistent() const {
    // Rebuild what the bitboards SHOULD be, purely from the mailbox,
    // and compare against what's actually stored.
    std::array<Bitboard, 12> expectedBB{};
    expectedBB.fill(EMPTY_BB);

    for (int sq = 0; sq < 64; ++sq) {
        Piece p = board_[sq];
        if (p != NO_PIECE) {
            setBit(expectedBB[p], sq);
        }
    }

    for (int p = 0; p < 12; ++p) {
        if (expectedBB[p] != pieceBB_[p]) return false;
    }

    Bitboard expectedWhite = EMPTY_BB, expectedBlack = EMPTY_BB;
    for (int p = WHITE_PAWN; p <= WHITE_KING; ++p) expectedWhite |= pieceBB_[p];
    for (int p = BLACK_PAWN; p <= BLACK_KING; ++p) expectedBlack |= pieceBB_[p];

    if (expectedWhite != occupancyByColor_[WHITE]) return false;
    if (expectedBlack != occupancyByColor_[BLACK]) return false;
    if ((expectedWhite | expectedBlack) != occupancyAll_) return false;

    return true;
}
