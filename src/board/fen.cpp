#include "fen.hpp"
#include <sstream>
#include <vector>
#include <cctype>
#include <algorithm>

namespace {

// Splits a string on whitespace into tokens. FEN fields are always
// separated by single spaces, but we're lenient about repeated spaces.
std::vector<std::string> splitFields(const std::string& s) {
    std::vector<std::string> fields;
    std::istringstream iss(s);
    std::string token;
    while (iss >> token) fields.push_back(token);
    return fields;
}

bool parsePiecePlacement(Position& pos, const std::string& field, std::string* errorOut) {
    // Walk ranks 8 down to 1 (as written left-to-right in the field),
    // placing pieces into our internal rank-0-at-bottom addressing.
    int rank = 7;
    int file = 0;

    for (size_t i = 0; i < field.size(); ++i) {
        char c = field[i];

        if (c == '/') {
            if (file != 8) {
                if (errorOut) *errorOut = "Rank " + std::to_string(rank + 1) +
                                           " does not contain exactly 8 files";
                return false;
            }
            --rank;
            file = 0;
            if (rank < 0) {
                if (errorOut) *errorOut = "Too many ranks in piece placement field";
                return false;
            }
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            int emptyCount = c - '0';
            if (emptyCount < 1 || emptyCount > 8) {
                if (errorOut) *errorOut = "Invalid empty-square count in piece placement";
                return false;
            }
            file += emptyCount;
            if (file > 8) {
                if (errorOut) *errorOut = "Rank " + std::to_string(rank + 1) + " overflows 8 files";
                return false;
            }
            continue;
        }

        Piece p = charToPiece(c);
        if (p == NO_PIECE) {
            if (errorOut) *errorOut = std::string("Invalid piece character '") + c + "'";
            return false;
        }
        if (file >= 8) {
            if (errorOut) *errorOut = "Rank " + std::to_string(rank + 1) + " overflows 8 files";
            return false;
        }
        pos.putPiece(p, squareOf(file, rank));
        ++file;
    }

    if (rank != 0 || file != 8) {
        if (errorOut) *errorOut = "Piece placement field does not describe exactly 8 ranks";
        return false;
    }

    return true;
}

bool parseSideToMove(Position& pos, const std::string& field, std::string* errorOut) {
    if (field == "w") { pos.setSideToMove(WHITE); return true; }
    if (field == "b") { pos.setSideToMove(BLACK); return true; }
    if (errorOut) *errorOut = "Side to move must be 'w' or 'b'";
    return false;
}

bool parseCastlingRights(Position& pos, const std::string& field, std::string* errorOut) {
    if (field == "-") { pos.setCastlingRights(0); return true; }

    CastlingRights cr = 0;
    for (char c : field) {
        switch (c) {
            case 'K': cr |= WHITE_KINGSIDE;  break;
            case 'Q': cr |= WHITE_QUEENSIDE; break;
            case 'k': cr |= BLACK_KINGSIDE;  break;
            case 'q': cr |= BLACK_QUEENSIDE; break;
            default:
                if (errorOut) *errorOut = std::string("Invalid castling rights character '") + c + "'";
                return false;
        }
    }
    pos.setCastlingRights(cr);
    return true;
}

bool parseEnPassant(Position& pos, const std::string& field, std::string* errorOut) {
    if (field == "-") { pos.setEnPassantSquare(NO_SQUARE); return true; }

    if (field.size() != 2) {
        if (errorOut) *errorOut = "Invalid en passant square '" + field + "'";
        return false;
    }

    try {
        Square sq = squareFromString(field);
        int r = rankOf(sq);
        // A double-push target square must sit on rank 3 (index 2, after
        // White's push) or rank 6 (index 5, after Black's push).
        if (r != 2 && r != 5) {
            if (errorOut) *errorOut = "En passant square '" + field + "' is not on rank 3 or 6";
            return false;
        }
        pos.setEnPassantSquare(sq);
        return true;
    } catch (const std::invalid_argument&) {
        if (errorOut) *errorOut = "Invalid en passant square '" + field + "'";
        return false;
    }
}

bool parseNonNegativeInt(const std::string& field, int& out, std::string* errorOut, const char* fieldName) {
    if (field.empty() || !std::all_of(field.begin(), field.end(),
                                       [](unsigned char c) { return std::isdigit(c); })) {
        if (errorOut) *errorOut = std::string(fieldName) + " must be a non-negative integer";
        return false;
    }
    try {
        out = std::stoi(field);
    } catch (...) {
        if (errorOut) *errorOut = std::string(fieldName) + " is out of range";
        return false;
    }
    return true;
}

} // namespace

bool loadFEN(Position& pos, const std::string& fen, std::string* errorOut) {
    pos.clear();

    std::vector<std::string> fields = splitFields(fen);

    // Strict FEN requires all 6 fields. Some real-world FENs omit the
    // halfmove/fullmove counters; we accept that leniently by defaulting
    // them to 0 and 1, but everything else is mandatory.
    if (fields.size() < 4) {
        if (errorOut) *errorOut = "FEN must have at least 4 fields "
                                   "(piece placement, side to move, castling, en passant)";
        pos.clear();
        return false;
    }

    std::string err;

    if (!parsePiecePlacement(pos, fields[0], &err)) {
        if (errorOut) *errorOut = err;
        pos.clear();
        return false;
    }
    if (!parseSideToMove(pos, fields[1], &err)) {
        if (errorOut) *errorOut = err;
        pos.clear();
        return false;
    }
    if (!parseCastlingRights(pos, fields[2], &err)) {
        if (errorOut) *errorOut = err;
        pos.clear();
        return false;
    }
    if (!parseEnPassant(pos, fields[3], &err)) {
        if (errorOut) *errorOut = err;
        pos.clear();
        return false;
    }

    int halfmove = 0;
    int fullmove = 1;

    if (fields.size() >= 5) {
        if (!parseNonNegativeInt(fields[4], halfmove, &err, "Halfmove clock")) {
            if (errorOut) *errorOut = err;
            pos.clear();
            return false;
        }
    }
    if (fields.size() >= 6) {
        if (!parseNonNegativeInt(fields[5], fullmove, &err, "Fullmove number") || fullmove < 1) {
            if (errorOut) *errorOut = err.empty() ? "Fullmove number must be at least 1" : err;
            pos.clear();
            return false;
        }
    }

    pos.setHalfmoveClock(halfmove);
    pos.setFullmoveNumber(fullmove);

    if (!pos.isConsistent()) {
        if (errorOut) *errorOut = "Internal error: position inconsistent after FEN load";
        pos.clear();
        return false;
    }

    return true;
}

std::string toFEN(const Position& pos) {
    std::ostringstream out;

    // Field 1: piece placement, rank 8 down to rank 1.
    for (int rank = 7; rank >= 0; --rank) {
        int emptyRun = 0;
        for (int file = 0; file < 8; ++file) {
            Piece p = pos.pieceOn(squareOf(file, rank));
            if (p == NO_PIECE) {
                ++emptyRun;
            } else {
                if (emptyRun > 0) {
                    out << emptyRun;
                    emptyRun = 0;
                }
                out << pieceToChar(p);
            }
        }
        if (emptyRun > 0) out << emptyRun;
        if (rank > 0) out << '/';
    }

    // Field 2: side to move.
    out << ' ' << (pos.sideToMove() == WHITE ? 'w' : 'b');

    // Field 3: castling rights.
    out << ' ';
    CastlingRights cr = pos.castlingRights();
    if (cr == 0) {
        out << '-';
    } else {
        if (cr & WHITE_KINGSIDE)  out << 'K';
        if (cr & WHITE_QUEENSIDE) out << 'Q';
        if (cr & BLACK_KINGSIDE)  out << 'k';
        if (cr & BLACK_QUEENSIDE) out << 'q';
    }

    // Field 4: en passant target square.
    out << ' ' << (pos.enPassantSquare() == NO_SQUARE ? "-" : squareToString(pos.enPassantSquare()));

    // Fields 5-6: halfmove clock, fullmove number.
    out << ' ' << pos.halfmoveClock();
    out << ' ' << pos.fullmoveNumber();

    return out.str();
}
