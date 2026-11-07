# Rule correctness checklist (Stage 9 gate)

The roadmap requires every rule below to be handled correctly before search work begins.
Each row names where it is implemented and what proves it. "Perft" means the 37-position suite in
`src/perft/suite.cpp` (189 counts, about 73M nodes), whose reference counts were computed
independently with python-chess 1.11.2 and, for the standard positions, also match the published tables.
`test_stage9` additionally checks leaf breakdowns (captures, en passant, castles, promotions, checks,
mates) and `divide` output against python-chess.

| Rule | Implemented in | Verified by |
|---|---|---|
| Every normal piece movement | `move/movegen.cpp` | Stage 4 tests; perft (all positions) |
| Pawn double moves | `move/movegen.cpp`, `rules/special.cpp` (`enPassantSquareAfter`) | Stage 4, 6, 7 tests; perft |
| Pawn captures | `move/movegen.cpp` | Stage 4 tests; perft capture counts (start d4: 1576) |
| En passant | `move/movegen.cpp`, `move/makemove.cpp` | Stage 5, 6, 7 tests; perft en passant counts (Kiwipete d3: 45; position 3 d4: 123) |
| Castling, both sides | `move/movegen.cpp`, `rules/special.cpp` | Stage 6 tests; perft castle counts (Kiwipete d3: 3162) |
| Promotion to all four pieces | `move/movegen.cpp`, `move/makemove.cpp` | Stage 4, 6, 7 tests; perft promotion counts (position 5 d3: 5068) |
| Promotion captures | same | Stage 6, 7 tests; perft positions 4, 5 and the promotion positions |
| Check | `board/attacks.cpp` | Stage 3, 5 tests; perft check counts (start d4: 469) |
| Checkmate | `rules/gamestate.cpp` | Stage 8 tests; perft mate counts (start d4: 8); python-chess comparison |
| Stalemate | `rules/gamestate.cpp` | Stage 8 tests; python-chess comparison |
| Pins | `move/legal.cpp` | Stage 5, 6 tests; perft pin positions |
| Double check | `move/legal.cpp` | Stage 5 tests; perft "double check position" |
| Discovered check | `move/legal.cpp` | Stage 5, 6 tests; perft check counts include discovered and double checks |
| Illegal king moves | `move/legal.cpp` | Stage 5 tests; perft |
| Illegal king captures | `rules/validate.cpp`, `move/legal.cpp` | Regression tests; Stage 9 walk asserts no legal move ever captures a king |
| Castling restrictions (in check, through or onto attack, blocked, rook attacked or not) | `rules/special.cpp` (`castlingPathSafe`) | Stage 6 tests; perft castling positions, including rook-trade positions that need a lost right to matter |
| En passant legality (horizontal and diagonal discovered checks, pins) | `move/legal.cpp` | Stage 5, 6 tests; perft en passant positions |
| Castling-right changes | `rules/special.cpp` (`castlingRightsAfter`) | Stage 6, 7 tests; perft rook-trade positions |
| Position state | `board/position.*`, `move/makemove.*` | Stage 1, 2, 7 tests (make/unmake round trip at every node of a depth-3 tree); python-chess FEN comparison |
| Repetition state | `rules/repetition.*` | Stage 8 tests; python-chess comparison |
| Halfmove clock | `move/makemove.cpp` | Stage 7, 8 tests; python-chess FEN comparison |
| Draw conditions (dead position, 50/75-move, 3/5-fold) | `rules/gamestate.cpp` | Stage 8 tests; python-chess comparison on about 635,000 positions |

## How the gate was checked

Rule bugs were injected on purpose (en passant victim not removed, every promotion becoming a queen,
the legality filter disabled, castling checking only the king's start square, castling rights not
cleared by rook moves or captures) and the suite fails for each of them.
Findings from that exercise: the castling-right mutations only show up at depth 4 or more and only in
positions where a rook returns to its corner, which is why the "rook trade" positions were added.

## Not covered by perft (covered elsewhere)

Perft ignores the halfmove clock, repetition and the draw rules; those are checked by the Stage 7 and 8 tests.