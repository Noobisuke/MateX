#include "movegen.hpp"
#include "../board/attacks.hpp"

namespace {

void addMove(MoveList& moves, int from, int to, Piece piece, Piece captured) {
    Move m;
    m.from = static_cast<int8_t>(from);
    m.to = static_cast<int8_t>(to);
    m.piece = piece;
    m.captured = captured;
    m.flag = (captured == NO_PIECE) ? QUIET : CAPTURE;
    moves.push(m);
}

void addPromotions(MoveList& moves, int from, int to, Color side, Piece captured) {
    static const PieceType promoPieces[4] = { QUEEN, ROOK, BISHOP, KNIGHT };
    for (PieceType pt : promoPieces) {
        Move m;
        m.from = static_cast<int8_t>(from);
        m.to = static_cast<int8_t>(to);
        m.piece = makePiece(side, PAWN);
        m.captured = captured;
        m.promotion = pt;
        m.flag = (captured == NO_PIECE) ? PROMOTION : PROMOTION_CAPTURE;
        moves.push(m);
    }
}

// Shared body for knight/bishop/rook/queen: for each piece of the given
// type, every attacked square not occupied by a friendly piece is a move.
template <typename AttackFn>
void generateForPiece(const Position& pos, MoveList& moves, PieceType pt, AttackFn attacksFrom) {
    Color side = pos.sideToMove();
    Bitboard ownOcc = pos.occupancy(side);
    Piece piece = makePiece(side, pt);

    Bitboard bb = pos.pieces(side, pt);
    while (bb) {
        int from = popLsb(bb);
        Bitboard targets = attacksFrom(from) & ~ownOcc;
        while (targets) {
            int to = popLsb(targets);
            addMove(moves, from, to, piece, pos.pieceOn(to));
        }
    }
}

void addCastle(MoveList& moves, int from, int to, Piece king, MoveFlag flag) {
    Move m;
    m.from = static_cast<int8_t>(from);
    m.to = static_cast<int8_t>(to);
    m.piece = king;
    m.flag = flag;
    moves.push(m);
}

} // namespace

void generatePawnMoves(const Position& pos, MoveList& moves) {
    Color side = pos.sideToMove();
    Color enemy = ~side;
    Bitboard occAll = pos.occupancyAll();
    Bitboard enemyOcc = pos.occupancy(enemy);
    Piece ownPawn = makePiece(side, PAWN);

    int forward = (side == WHITE) ? 8 : -8;
    int startRank = (side == WHITE) ? 1 : 6;   // 0-indexed rank 2 / rank 7
    int promoRank = (side == WHITE) ? 7 : 0;   // 0-indexed rank 8 / rank 1
    int epRank    = (side == WHITE) ? 5 : 2;   // rank on which an ep target must sit for this side

    Bitboard bb = pos.pieces(side, PAWN);
    while (bb) {
        int from = popLsb(bb);

        // --- Single push (and double push, which requires the single) ---
        int to = from + forward;
        if (to >= 0 && to < 64 && !testBit(occAll, to)) {
            if (rankOf(to) == promoRank) {
                addPromotions(moves, from, to, side, NO_PIECE);
            } else {
                Move m;
                m.from = static_cast<int8_t>(from);
                m.to = static_cast<int8_t>(to);
                m.piece = ownPawn;
                m.flag = QUIET;
                moves.push(m);

                if (rankOf(from) == startRank) {
                    int to2 = to + forward;
                    if (!testBit(occAll, to2)) {
                        Move dm;
                        dm.from = static_cast<int8_t>(from);
                        dm.to = static_cast<int8_t>(to2);
                        dm.piece = ownPawn;
                        dm.flag = DOUBLE_PAWN_PUSH;
                        moves.push(dm);
                    }
                }
            }
        }

        // --- Captures (including capture-promotions) ---
        Bitboard attacks = pawnAttacksFrom(from, side);
        Bitboard caps = attacks & enemyOcc;
        while (caps) {
            int capTo = popLsb(caps);
            Piece captured = pos.pieceOn(capTo);
            if (rankOf(capTo) == promoRank) {
                addPromotions(moves, from, capTo, side, captured);
            } else {
                addMove(moves, from, capTo, ownPawn, captured);
            }
        }

        // --- En passant ---
        // Guarded so a malformed FEN (ep square on the wrong rank, or no
        // enemy pawn actually behind it) cannot yield a bogus capture.
        int ep = pos.enPassantSquare();
        if (ep != NO_SQUARE && testBit(attacks, ep) && rankOf(ep) == epRank &&
            pos.pieceOn(ep - forward) == makePiece(enemy, PAWN) &&
            pos.pieceOn(ep) == NO_PIECE) {
            // The captured pawn sits one rank behind `to`; Stage 7's
            // make-move derives it as (to - forward) from the EN_PASSANT flag.
            Move m;
            m.from = static_cast<int8_t>(from);
            m.to = static_cast<int8_t>(ep);
            m.piece = ownPawn;
            m.captured = makePiece(enemy, PAWN);
            m.flag = EN_PASSANT;
            moves.push(m);
        }
    }
}

void generateKnightMoves(const Position& pos, MoveList& moves) {
    generateForPiece(pos, moves, KNIGHT, [](int sq) { return knightAttacksFrom(sq); });
}

void generateBishopMoves(const Position& pos, MoveList& moves) {
    Bitboard occ = pos.occupancyAll();
    generateForPiece(pos, moves, BISHOP, [occ](int sq) { return bishopAttacksFrom(sq, occ); });
}

void generateRookMoves(const Position& pos, MoveList& moves) {
    Bitboard occ = pos.occupancyAll();
    generateForPiece(pos, moves, ROOK, [occ](int sq) { return rookAttacksFrom(sq, occ); });
}

void generateQueenMoves(const Position& pos, MoveList& moves) {
    Bitboard occ = pos.occupancyAll();
    generateForPiece(pos, moves, QUEEN, [occ](int sq) { return queenAttacksFrom(sq, occ); });
}

void generateKingMoves(const Position& pos, MoveList& moves) {
    Color side = pos.sideToMove();
    Bitboard kingBB = pos.pieces(side, KING);
    if (kingBB == EMPTY_BB) return;

    int from = lsbIndex(kingBB);
    Piece king = makePiece(side, KING);

    // --- Normal king moves ---
    Bitboard targets = kingAttacksFrom(from) & ~pos.occupancy(side);
    while (targets) {
        int to = popLsb(targets);
        addMove(moves, from, to, king, pos.pieceOn(to));
    }

    // --- Castling: structural conditions only (see header note) ---
    Bitboard occ = pos.occupancyAll();
    CastlingRights cr = pos.castlingRights();

    if (side == WHITE) {
        if ((cr & WHITE_KINGSIDE) && from == E1 && pos.pieceOn(H1) == WHITE_ROOK &&
            !testBit(occ, F1) && !testBit(occ, G1)) {
            addCastle(moves, E1, G1, king, CASTLE_KINGSIDE);
        }
        if ((cr & WHITE_QUEENSIDE) && from == E1 && pos.pieceOn(A1) == WHITE_ROOK &&
            !testBit(occ, D1) && !testBit(occ, C1) && !testBit(occ, B1)) {
            addCastle(moves, E1, C1, king, CASTLE_QUEENSIDE);
        }
    } else {
        if ((cr & BLACK_KINGSIDE) && from == E8 && pos.pieceOn(H8) == BLACK_ROOK &&
            !testBit(occ, F8) && !testBit(occ, G8)) {
            addCastle(moves, E8, G8, king, CASTLE_KINGSIDE);
        }
        if ((cr & BLACK_QUEENSIDE) && from == E8 && pos.pieceOn(A8) == BLACK_ROOK &&
            !testBit(occ, D8) && !testBit(occ, C8) && !testBit(occ, B8)) {
            addCastle(moves, E8, C8, king, CASTLE_QUEENSIDE);
        }
    }
}

void generatePseudoLegalMoves(const Position& pos, MoveList& moves) {
    generatePawnMoves(pos, moves);
    generateKnightMoves(pos, moves);
    generateBishopMoves(pos, moves);
    generateRookMoves(pos, moves);
    generateQueenMoves(pos, moves);
    generateKingMoves(pos, moves);
}