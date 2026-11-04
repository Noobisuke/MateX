// Stage 4 tests: pseudo-legal move generation

#include "../src/board/position.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/attacks.hpp"
#include "../src/move/movegen.hpp"
#include <iostream>
#include <string>

static int testsRun = 0;
static int testsFailed = 0;

#define CHECK(cond, msg) do { \
    ++testsRun; \
    if (!(cond)) { \
        ++testsFailed; \
        std::cout << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
    } \
} while (0)

static Position fromFEN(const std::string& fen) {
    Position pos;
    std::string err;
    if (!loadFEN(pos, fen, &err)) {
        std::cout << "TEST SETUP FAILED to load FEN '" << fen << "': " << err << "\n";
    }
    return pos;
}

static const Move* findMove(const MoveList& moves, const std::string& uci) {
    for (size_t i = 0; i < moves.size(); ++i) {
        if (moves[i].toString() == uci) return &moves[i];
    }
    return nullptr;
}

static bool has(const MoveList& moves, const std::string& uci) {
    return findMove(moves, uci) != nullptr;
}

static int countIf(const MoveList& moves, bool (*pred)(const Move&)) {
    int n = 0;
    for (size_t i = 0; i < moves.size(); ++i) if (pred(moves[i])) ++n;
    return n;
}

static bool isCap(const Move& m) { return m.isCapture(); }
static bool isPromo(const Move& m) { return m.isPromotion(); }
static bool isCastleMove(const Move& m) { return m.isCastle(); }

static size_t totalMoves(const std::string& fen) {
    Position pos = fromFEN(fen);
    MoveList moves;
    generatePseudoLegalMoves(pos, moves);
    return moves.size();
}

// --- Starting position ---------------------------------------------------------

void testStartingPosition() {
    Position pos = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    MoveList moves;
    generatePseudoLegalMoves(pos, moves);
    CHECK(moves.size() == 20, "Starting position: 20 moves expected, got " + std::to_string(moves.size()));
    CHECK(countIf(moves, isCap) == 0, "Starting position: no captures");
    CHECK(has(moves, "e2e4"), "e2e4 generated");
    CHECK(has(moves, "e2e3"), "e2e3 generated");
    CHECK(has(moves, "g1f3"), "g1f3 generated");
    CHECK(has(moves, "b1a3"), "b1a3 generated");
    CHECK(!has(moves, "e2e5"), "e2e5 must not exist");
    CHECK(!has(moves, "d1d3"), "queen blocked by own pawn");
    CHECK(!has(moves, "e1g1"), "no castling through own pieces");

    // Same count for Black (symmetry).
    CHECK(totalMoves("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1") == 20,
          "Black also has 20 moves from the starting setup");
}

void testDoublePushFlag() {
    Position pos = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    MoveList moves;
    generatePseudoLegalMoves(pos, moves);
    const Move* m = findMove(moves, "e2e4");
    CHECK(m && m->flag == DOUBLE_PAWN_PUSH, "e2e4 flagged DOUBLE_PAWN_PUSH");
    const Move* s = findMove(moves, "e2e3");
    CHECK(s && s->flag == QUIET, "e2e3 flagged QUIET");
}

// --- Pawn pushes ----------------------------------------------------------------

void testPawnPushBlocking() {
    MoveList moves;
    // Blocker directly in front: no pushes at all.
    generatePawnMoves(fromFEN("4k3/8/8/8/8/4n3/4P3/4K3 w - - 0 1"), moves);
    CHECK(!has(moves, "e2e3") && !has(moves, "e2e4"), "Blocked on e3: neither push available");

    // Blocker two ahead: single push only.
    MoveList m2;
    generatePawnMoves(fromFEN("4k3/8/8/8/4n3/8/4P3/4K3 w - - 0 1"), m2);
    CHECK(has(m2, "e2e3"), "e3 free: single push available");
    CHECK(!has(m2, "e2e4"), "e4 occupied: double push unavailable");

    // Not on start rank: no double push.
    MoveList m3;
    generatePawnMoves(fromFEN("4k3/8/8/8/8/4P3/8/4K3 w - - 0 1"), m3);
    CHECK(has(m3, "e3e4"), "e3e4 available");
    CHECK(!has(m3, "e3e5"), "e3e5 unavailable (not on start rank)");
}

void testBlackPawnDirection() {
    MoveList moves;
    generatePawnMoves(fromFEN("4k3/4p3/8/8/8/8/8/4K3 b - - 0 1"), moves);
    CHECK(has(moves, "e7e6"), "Black single push goes down the board");
    CHECK(has(moves, "e7e5"), "Black double push from rank 7");
    CHECK(moves.size() == 2, "Black pawn on e7 has exactly 2 moves");
}

// --- Pawn captures --------------------------------------------------------------

void testPawnCaptures() {
    MoveList moves;
    generatePawnMoves(fromFEN("4k3/8/8/3p1p2/4P3/8/8/4K3 w - - 0 1"), moves);
    CHECK(has(moves, "e4d5") && has(moves, "e4f5"), "Both diagonal captures generated");
    CHECK(has(moves, "e4e5"), "Quiet push still generated");
    const Move* m = findMove(moves, "e4d5");
    CHECK(m && m->flag == CAPTURE && m->captured == BLACK_PAWN, "e4d5 is a CAPTURE of a black pawn");

    MoveList head;
    generatePawnMoves(fromFEN("4k3/8/8/8/4p3/4P3/8/4K3 w - - 0 1"), head);
    CHECK(!has(head, "e3e4"), "No straight-ahead capture");
}

void testPawnEdgeCaptures() {
    // a-file pawn must not wrap around to the h-file.
    MoveList moves;
    generatePawnMoves(fromFEN("4k3/8/8/6p1/P7/8/8/4K3 w - - 0 1"), moves);
    CHECK(moves.size() == 1 && has(moves, "a4a5"), "a4 pawn: only a4a5, no wraparound capture");

    MoveList hfile;
    generatePawnMoves(fromFEN("4k3/8/8/1p6/7P/8/8/4K3 w - - 0 1"), hfile);
    CHECK(hfile.size() == 1 && has(hfile, "h4h5"), "h4 pawn: only h4h5, no wraparound capture");
}

// --- En passant -----------------------------------------------------------------

void testEnPassant() {
    MoveList moves;
    generatePawnMoves(fromFEN("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"), moves);
    const Move* m = findMove(moves, "e5d6");
    CHECK(m != nullptr, "e5d6 en passant generated");
    CHECK(m && m->flag == EN_PASSANT, "e5d6 flagged EN_PASSANT");
    CHECK(m && m->captured == BLACK_PAWN, "e5d6 records captured black pawn");
    CHECK(m && m->isCapture(), "En passant counts as a capture");

    MoveList none;
    generatePawnMoves(fromFEN("4k3/8/8/3pP3/8/8/8/4K3 w - - 0 1"), none);
    CHECK(!has(none, "e5d6"), "No en passant without a target square");
}

void testBlackEnPassant() {
    MoveList moves;
    generatePawnMoves(fromFEN("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1"), moves);
    const Move* m = findMove(moves, "d4e3");
    CHECK(m && m->flag == EN_PASSANT && m->captured == WHITE_PAWN, "Black d4xe3 en passant generated");
}

void testBogusEnPassantRejected() {
    // Regression guard: ep square on the wrong rank for the side to move
    // (white to move, target d3) must not produce e2xd3 "en passant".
    MoveList wrongRank;
    generatePawnMoves(fromFEN("4k3/8/8/8/8/8/4P3/4K3 w - d3 0 1"), wrongRank);
    CHECK(!has(wrongRank, "e2d3"), "Ep target on wrong rank must not generate a capture");

    // Correct rank, but no enemy pawn behind the target square.
    MoveList noPawn;
    generatePawnMoves(fromFEN("4k3/8/8/4P3/8/8/8/4K3 w - d6 0 1"), noPawn);
    CHECK(!has(noPawn, "e5d6"), "Ep target with no pawn behind it must not generate a capture");
}

// --- Promotion ------------------------------------------------------------------

void testQuietPromotion() {
    MoveList moves;
    generatePawnMoves(fromFEN("4k3/P7/8/8/8/8/8/4K3 w - - 0 1"), moves);
    CHECK(has(moves, "a7a8q") && has(moves, "a7a8r") && has(moves, "a7a8b") && has(moves, "a7a8n"),
          "All four promotion pieces generated");
    CHECK(countIf(moves, isPromo) == 4, "Exactly 4 promotion moves");
    const Move* m = findMove(moves, "a7a8n");
    CHECK(m && m->flag == PROMOTION && m->promotion == KNIGHT, "a7a8n is a PROMOTION to knight");
}

void testCapturePromotion() {
    MoveList moves;
    generatePawnMoves(fromFEN("r1r1k3/1P6/8/8/8/8/8/4K3 w - - 0 1"), moves);
    CHECK(countIf(moves, isPromo) == 12, "b7 promotes 4 ways on b8 + 4 on a8 + 4 on c8 = 12");
    const Move* m = findMove(moves, "b7a8q");
    CHECK(m && m->flag == PROMOTION_CAPTURE && m->captured == BLACK_ROOK && m->promotion == QUEEN,
          "b7xa8=Q is PROMOTION_CAPTURE of a rook");
    CHECK(has(moves, "b7b8q"), "Quiet promotion b7b8 also generated (b8 is empty)");
}

void testBlackPromotion() {
    CHECK(totalMoves("4k3/8/8/8/8/8/p7/4K3 b - - 0 1") == 9,
          "Black a2 pawn: 4 promotions + 5 king moves = 9");
    MoveList moves;
    generatePawnMoves(fromFEN("4k3/8/8/8/8/8/p7/4K3 b - - 0 1"), moves);
    CHECK(has(moves, "a2a1q") && has(moves, "a2a1n"), "Black promotes on rank 1");
}

// --- Pieces ---------------------------------------------------------------------

void testKnightMoves() {
    MoveList moves;
    generateKnightMoves(fromFEN("4k3/8/8/8/8/4P3/8/4KN2 w - - 0 1"), moves);
    CHECK(has(moves, "f1d2") && has(moves, "f1g3") && has(moves, "f1h2"), "Free knight jumps generated");
    CHECK(!has(moves, "f1e3"), "Knight must not land on own pawn on e3");
    CHECK(moves.size() == 3, "Exactly 3 knight moves");
}

void testSlidingBlocking() {
    MoveList own;
    generateRookMoves(fromFEN("4k3/8/8/8/P7/8/8/R3K3 w - - 0 1"), own);
    CHECK(has(own, "a1a2") && has(own, "a1a3"), "Rook reaches squares before own pawn");
    CHECK(!has(own, "a1a4") && !has(own, "a1a5"), "Rook can't land on or pass own pawn");

    MoveList enemy;
    generateRookMoves(fromFEN("4k3/8/8/8/p7/8/8/R3K3 w - - 0 1"), enemy);
    const Move* m = findMove(enemy, "a1a4");
    CHECK(m && m->flag == CAPTURE && m->captured == BLACK_PAWN, "Rook captures enemy pawn on a4");
    CHECK(!has(enemy, "a1a5"), "Rook stops after capturing");
}

void testBishopAndQueenCounts() {
    MoveList b;
    generateBishopMoves(fromFEN("4k3/8/8/8/3B4/8/8/4K3 w - - 0 1"), b);
    CHECK(b.size() == 13, "Bishop on d4 of an open board has 13 moves, got " + std::to_string(b.size()));

    MoveList q;
    generateQueenMoves(fromFEN("4k3/8/8/8/3Q4/8/8/4K3 w - - 0 1"), q);
    CHECK(q.size() == 27, "Queen on d4 of an open board has 27 moves, got " + std::to_string(q.size()));
}

void testKingMoves() {
    MoveList moves;
    generateKingMoves(fromFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1"), moves);
    CHECK(moves.size() == 5, "King on e1 (no castling rights) has 5 moves");

    MoveList corner;
    generateKingMoves(fromFEN("4k3/8/8/8/8/8/8/K7 w - - 0 1"), corner);
    CHECK(corner.size() == 3, "King on a1 has 3 moves");
}

// --- Castling (structural only) ------------------------------------------------

void testCastlingGenerated() {
    MoveList moves;
    generateKingMoves(fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"), moves);
    const Move* ks = findMove(moves, "e1g1");
    const Move* qs = findMove(moves, "e1c1");
    CHECK(ks && ks->flag == CASTLE_KINGSIDE, "White O-O generated with CASTLE_KINGSIDE flag");
    CHECK(qs && qs->flag == CASTLE_QUEENSIDE, "White O-O-O generated with CASTLE_QUEENSIDE flag");

    MoveList black;
    generateKingMoves(fromFEN("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1"), black);
    CHECK(has(black, "e8g8") && has(black, "e8c8"), "Black castling both sides generated");
}

void testCastlingRequiresRights() {
    MoveList moves;
    generateKingMoves(fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1"), moves);
    CHECK(countIf(moves, isCastleMove) == 0, "No castling without rights");

    MoveList partial;
    generateKingMoves(fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w Kq - 0 1"), partial);
    CHECK(has(partial, "e1g1") && !has(partial, "e1c1"), "Only the held right (K) generates castling");
}

void testCastlingBlocked() {
    MoveList moves;
    generateKingMoves(fromFEN("r3k2r/8/8/8/8/8/8/R3KB1R w KQkq - 0 1"), moves);
    CHECK(!has(moves, "e1g1"), "Bishop on f1 blocks kingside castling");
    CHECK(has(moves, "e1c1"), "Queenside still open");

    MoveList queenside;
    generateKingMoves(fromFEN("r3k2r/8/8/8/8/8/8/RN2K2R w KQkq - 0 1"), queenside);
    CHECK(!has(queenside, "e1c1"), "Knight on b1 blocks queenside castling (b1 must be empty)");
}

void testCastlingRightWithoutRook() {
    // Rights claimed, but no rook on h1: must not castle (guards bad FEN input).
    MoveList moves;
    generateKingMoves(fromFEN("4k3/8/8/8/8/8/8/R3K3 w KQ - 0 1"), moves);
    CHECK(!has(moves, "e1g1"), "No kingside castle with no rook on h1");
    CHECK(has(moves, "e1c1"), "Queenside castle fine with rook on a1");
}

// --- Aggregate counts where pseudo-legal == legal ----------------------------

void testKnownCounts() {
    CHECK(totalMoves("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1") == 26,
          "Castling test position: 26 moves (matches the known perft(1) value)");
    CHECK(totalMoves("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1") == 26,
          "Castling test position, Black to move: 26 moves");
    CHECK(totalMoves("4k3/P7/8/8/8/8/8/4K3 w - - 0 1") == 9,
          "Lone promoting pawn + king: 4 promotions + 5 king moves");
}

void testKiwipete() {
    Position pos = fromFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    MoveList moves;
    generatePseudoLegalMoves(pos, moves);
    // Known LEGAL count is 48. Pseudo-legal is a superset, so >= 48.
    CHECK(moves.size() >= 48, "Kiwipete pseudo-legal count >= 48 legal moves, got " + std::to_string(moves.size()));
    CHECK(countIf(moves, isCap) == 8, "Kiwipete has 8 captures, got " + std::to_string(countIf(moves, isCap)));
    CHECK(countIf(moves, isCastleMove) == 2, "Kiwipete: both castling moves generated");
}

void testNoMoveLandsOnOwnPiece() {
    // Invariant over several positions: no generated move targets a friendly piece.
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1",
        "rnbqkb1r/pp1p1ppp/5n2/2pPp3/8/8/PPP1PPPP/RNBQKBNR w KQkq e6 0 4"
    };
    for (const char* fen : fens) {
        Position pos = fromFEN(fen);
        MoveList moves;
        generatePseudoLegalMoves(pos, moves);
        bool ok = true;
        for (size_t i = 0; i < moves.size(); ++i) {
            const Move& m = moves[i];
            if (pos.pieceOn(m.from) != m.piece) ok = false;                                  // mover matches board
            if (m.flag != EN_PASSANT && pos.pieceOn(m.to) != m.captured) ok = false;          // captured matches board
            if (m.captured != NO_PIECE && colorOf(m.captured) == pos.sideToMove()) ok = false; // never capture own
            if (colorOf(m.piece) != pos.sideToMove()) ok = false;                            // only side to move
        }
        CHECK(ok, std::string("Move records consistent with board for: ") + fen);
    }
}

int main() {
    initAttackTables();

    testStartingPosition();
    testDoublePushFlag();
    testPawnPushBlocking();
    testBlackPawnDirection();
    testPawnCaptures();
    testPawnEdgeCaptures();
    testEnPassant();
    testBlackEnPassant();
    testBogusEnPassantRejected();
    testQuietPromotion();
    testCapturePromotion();
    testBlackPromotion();
    testKnightMoves();
    testSlidingBlocking();
    testBishopAndQueenCounts();
    testKingMoves();
    testCastlingGenerated();
    testCastlingRequiresRights();
    testCastlingBlocked();
    testCastlingRightWithoutRook();
    testKnownCounts();
    testKiwipete();
    testNoMoveLandsOnOwnPiece();

    std::cout << "\n" << (testsRun - testsFailed) << "/" << testsRun << " checks passed.\n";
    if (testsFailed == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << testsFailed << " CHECK(S) FAILED\n";
    return 1;
}