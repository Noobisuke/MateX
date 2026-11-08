# Chess Engine

A chess engine written from scratch in modern C++20, built one stage at a time
with correctness and testing first, performance second.

## Status

| Stage | Description | Status |
|---|---|---|
| 1 | Board representation (bitboards + mailbox) | Done |
| 2 | FEN parsing and generation | Done |
| 3 | Attack detection | Done |
| 4 | Pseudo-legal move generation | Done |
| 5 | Legal move generation | Done |
| 6 | Special rules (castling safety, castling rights, en passant, promotion) | Done |
| 7 | Make / unmake | Done |
| 8 | Game state and draw rules (mate, stalemate, dead position, 50/75-move, 3/5-fold) | Done |
| 9 | Perft and complete rule verification | Done: **move generator verified** |
| 10-20 | Evaluation, search, UCI, optimization | Planned |

The move generator passes a 37-position perft suite (189 node counts, about 73 million
nodes) against independently computed reference counts. See `docs/RULE_CHECKLIST.md`.

## Build

Requires a C++20 compiler (GCC/MinGW or Clang) and, optionally, CMake 3.16+.

With CMake (MinGW on Windows):

```powershell
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
mingw32-make
```

With CMake on Linux/macOS:

```bash
mkdir build && cd build
cmake .. && make
```

Without CMake (from the project root):

```bash
g++ -std=c++20 -O2 -Wall -Wextra -o chess_engine src/main.cpp src/board/*.cpp src/move/*.cpp src/rules/*.cpp src/perft/*.cpp
```

## Run

```bash
./chess_engine                       # starting position
./chess_engine "<FEN string>"        # any position (validated)
./play                               # play moves by hand: e2e4, moves, undo, fen <FEN>, new, quit
./perft_cli 5                        # count nodes to depth 5 from the start position
./perft_cli --divide 4 "<FEN>"       # per-move counts, for debugging against another engine
./perft_cli --detailed 4             # captures, en passant, castles, promotions, checks, mates
./perft_cli --bulk 5                 # same count using bulk counting
./perft_cli --suite                  # the whole perft suite (about 7 seconds)
```

## Test

Each stage has its own test executable (`test_stage1` ... `test_stage9`) plus
`test_regression` for bugs found by audits. Run them all with CTest from the build folder:

```bash
ctest --output-on-failure
```

## Layout

```
src/
  board/   types, bitboards, Position, FEN, attack detection
  move/    Move, MoveList, pseudo-legal and legal generation, make/unmake
  rules/   castling and en passant helpers, position validation, repetition, game state
  perft/   perft, divide, detailed stats, the standard suite
  main.cpp CLI for manual inspection
tools/     play (interactive), perft_cli
tests/     one test executable per stage, plus regression tests
docs/      roadmap prompt, rule checklist
```

## Design notes

- Squares use little-endian rank-file mapping: a1 = 0, h1 = 7, a8 = 56, h8 = 63.
- Position stores 12 piece bitboards plus a mailbox array, kept in sync and
  cross-checked by `Position::isConsistent()`.
- Legal moves are pseudo-legal moves filtered by making each one and testing the mover's king.
- `makeMove` returns a small `UndoInfo` that the caller keeps; `unmakeMove` restores the exact position.
- Game history (for repetition) is separate from the per-move undo information.
- `loadFEN` checks syntax only; call `validatePosition` on any position that comes from outside.

## Stage 10: Basic evaluation

`src/eval/evaluate.*` scores a position in centipawns from the side to move's view
(material, piece-square tables, pawn structure, bishop pair, mobility, king shield).
In `play`, type `eval` to see the term breakdown. Tests: `test_stage10`.

## Stage 11: Negamax search

`src/search/search.*`: fixed-depth negamax with mate-distance scores and a principal variation.
In `play`: `go 3` analyses, `engine 3` plays the engine's move, so you can now play against it.
Tests: `test_stage11`.

## Stage 12: Alpha-beta

`search()` now uses alpha-beta pruning (`searchMinimax()` keeps the plain version as a reference). Same results, far fewer nodes.
Tests: `test_stage12` (agreement with minimax + node savings table).

## Stage 13: Quiescence search

`search()` now resolves captures/promotions at depth 0 (stand pat, evasions when in check), fixing the horizon effect.
Older variants stay available (`searchMinimax`, `searchAlphaBeta`) as references. Tests: `test_stage13`.

## Stage 14: Move ordering

Alpha-beta tries captures (MVV-LVA), killer moves and history-ranked quiet moves first. Scores are unchanged; node counts
fall to about 3% of the unordered search at depth 4. `bench_search [depth]` prints the comparison. Tests: `test_stage14`.