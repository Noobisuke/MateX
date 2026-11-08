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
    baseKey_ = 0;

    sideToMove_ = WHITE;
    castlingRights_ = 0;
    enPassantSquare_ = NO_SQUARE;
    halfmoveClock_ = 0;
    fullmoveNumber_ = 1;
}

void Position::putPiece(Piece p, int sq) {
    // NO_PIECE (12) is not a valid index into pieceBB_/occupancyByColor_; placing "no piece"
    // means emptying the square. (Audit fix: this used to write out of bounds.)
    if (p == NO_PIECE) {
        removePiece(sq);
        return;
    }
    // Defensive: if a piece already sits here, remove it first so the
    // bitboards/mailbox never get out of sync.
    if (board_[sq] != NO_PIECE) {
        removePiece(sq);
    }
    board_[sq] = p;
    setBit(pieceBB_[p], sq);
    // Incremental occupancy update (Stage 7): touch one bit instead of rebuilding.
    setBit(occupancyByColor_[colorOf(p)], sq);
    setBit(occupancyAll_, sq);
    baseKey_ ^= zobrist::KEYS.piece[p][sq];
}

void Position::removePiece(int sq) {
    Piece p = board_[sq];
    if (p == NO_PIECE) return;
    board_[sq] = NO_PIECE;
    clearBit(pieceBB_[p], sq);
    clearBit(occupancyByColor_[colorOf(p)], sq);
    clearBit(occupancyAll_, sq);
    baseKey_ ^= zobrist::KEYS.piece[p][sq];
}

void Position::setStartingPosition() {
    clear();

    const PieceType backRank[8] = {
        ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK
    };

    for (int file = 0; file < 8; ++file) {
        putPiece(makePiece(WHITE, backRank[file]), squareOf(file, 0));
        putPiece(makePiece(WHITE, PAWN),           squareOf(file, 1));
        putPiece(makePiece(BLACK, PAWN),           squareOf(file, 6));
        putPiece(makePiece(BLACK, backRank[file]), squareOf(file, 7));
    }

    setSideToMove(WHITE);
    setCastlingRights(WHITE_KINGSIDE | WHITE_QUEENSIDE | BLACK_KINGSIDE | BLACK_QUEENSIDE);  // must go through the setter: it updates the key
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

    if (baseKey_ != recomputeBaseKey(*this)) return false;  // incremental Zobrist key must match a full recompute

    return true;
}