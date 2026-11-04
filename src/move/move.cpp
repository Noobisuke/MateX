#include "move.hpp"

std::string Move::toString() const {
    if (from < 0 || to < 0) return "0000"; // null move convention

    std::string s = squareToString(from) + squareToString(to);
    if (isPromotion()) {
        switch (promotion) {
            case KNIGHT: s += 'n'; break;
            case BISHOP: s += 'b'; break;
            case ROOK:   s += 'r'; break;
            case QUEEN:  s += 'q'; break;
            default: break;
        }
    }
    return s;
}