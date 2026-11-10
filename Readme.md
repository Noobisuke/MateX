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
| 10-15 | Evaluation, negamax, alpha-beta, quiescence, move ordering, iterative deepening | Done |
| 16 | Zobrist hashing and transposition table | Done |
| 17 | Advanced search (PVS, aspiration, null move, LMR, pruning, extensions, SEE, repetition) | Done |
| 18-20 | Advanced evaluation, UCI, final strength work | Planned |

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
./bench_tt 5                         # what the transposition table buys, and which replacement policy is best
./hash_cli < fens.txt                # Zobrist key of every FEN on stdin (for cross-checks)
./tactics wac.epd --nodes 20000 --opts default    # solve an EPD test suite (bm moves) at a node budget
./selfplay --a default --b pvs --nodes 10000 --pairs 22   # node-limited match between two configurations
```

## Test

Each stage has its own test executable (`test_stage1` ... `test_stage17`) plus
`test_regression` for bugs found by audits. Run them all with CTest from the build folder:

```bash
ctest --output-on-failure
```

## Layout

```
src/
  board/   types, bitboards, Position, FEN, attack detection, Zobrist keys
  move/    Move, MoveList, pseudo-legal and legal generation, make/unmake
  rules/   castling and en passant helpers, position validation, repetition, game state
  perft/   perft, divide, detailed stats, the standard suite
  eval/    static evaluation
  search/  alpha-beta search, quiescence, ordering, iterative deepening, transposition table
  main.cpp CLI for manual inspection
tools/     play (interactive), perft_cli, bench_search, bench_tt, hash_cli, tactics, selfplay
tests/     one test executable per stage, plus regression tests
docs/      roadmap, rule checklist
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

## Stage 15: Iterative deepening and time management

`searchIterative` searches depth 1, 2, 3... under a time, node or stop-flag limit and always returns the best move of the last
completed depth (or a legal fallback). In `play`: `go time 1000` / `engine time 1000`. Tests: `test_stage15`.

## Stage 16: Zobrist hashing and transposition table

`src/board/zobrist.*`: a 64-bit key per position, the XOR of random keys for every (piece, square), the side to move,
the castling-rights mask and, only when a pawn can actually capture en passant, the ep file. The keys are generated at
compile time from a fixed seed, so the same position always hashes to the same value. `Position` keeps the
placement/side/castling part up to date inside `putPiece`, `removePiece` and the two setters, so make/unmake needed no
changes; `hashKey(pos)` adds the en passant part. The halfmove clock is not part of the key.

`src/search/tt.*`: a transposition table with 16-byte entries (full key, depth, score, EXACT/LOWER/UPPER bound, packed
move). Mate scores are stored relative to their node and rebased on probe. Four replacement policies exist
(`ALWAYS`, `DEPTH_AGE`, `TWO_TIER`, `BUCKET4`); `BUCKET4` is the default because it degrades least when the table is
small (see `bench_tt`). The table supplies the hash move first in ordering (validated against the legal moves, never
trusted) and cuts nodes off. The root is never cut off, and the PV is extended from the table when a cutoff shortened it.

API: `searchTT(pos, depth, tt, mode)` for fixed depth, or `SearchLimits::tt` for `searchIterative`. A null table is
exactly the Stage 15 behaviour. Modes: `TT_DEPTH_GE` (standard, entries of at least the requested depth),
`TT_EXACT_DEPTH` (only entries of exactly the requested depth: provably the same score as no table, used by the tests)
and `TT_ORDER_ONLY` (hash move only, no cutoffs).

In `play`: `hash` shows the key, `hashfull` the table statistics, `clearhash` empties it; `go` and `engine` share one
16 MB table for the whole game.

Measured (`bench_tt 5`, 8 positions): at a fixed depth the table needs 77% of the nodes (hash move alone: 93%); with
iterative deepening about 50% of the nodes. Tests: `test_stage16`.

Known limits: the halfmove clock is not in the key (the 50-move draw rule is path dependent, as in other engines);
a pinned en passant capture can give two keys for one position (a missed match, never a wrong one); no repetition
detection inside the search yet; the table is not used inside quiescence.

## Stage 17: Advanced search

Every technique is a flag in `SearchOptions` (`src/search/search.hpp`), all off by default, so the search with no options
is exactly the Stage 16 search (same scores and node counts, pinned by tests) and every technique can be measured on its own.
`SearchOptions::engineDefault()` is the set kept after the measurements; in `play`, `options` shows or changes it
(`options pvs,null`, `options default`, `options all,-sing`).

| Technique | Kind | Result |
|---|---|---|
| Principal variation search (`pvs`) | score-preserving | same scores, about 12% fewer nodes |
| Aspiration windows (`asp`) | score-preserving* | 8% fewer nodes to depth 8 |
| SEE move ordering (`seeord`) | score-preserving | about 29% fewer nodes at depth 5 |
| Reverse futility (`rfp`), null move (`null`), LMR (`lmr`), futility (`fut`), check extension (`ext`), SEE pruning in quiescence (`seeq`) | heuristic | kept |
| Razoring (`raz`) | heuristic | **reverted**: lost 23 of 148 tactical positions |
| Singular extensions (`sing`) | heuristic | **reverted**: 246 against 252 positions solved at 80k nodes |
| Repetition detection (`rep`) | correctness | kept (+57 +-63 Elo in self-play) |

\* identity holds in the score-preserving configuration; with the heuristic techniques on, pruning depends on the window.

With the default set a depth-5 search of the 8 benchmark positions needs 85,091 nodes instead of 1,451,373 (5.9%).
The pruning techniques only apply at non-PV nodes, so they need `pvs` to tell PV nodes from the rest.

New pieces: null-move make/unmake (`makeNullMove`), static exchange evaluation (`src/search/see.*`, 99.3-99.5% exact against an
exhaustive exchange search), repetition detection with game history (`SearchLimits::history`), `searchOpt()` for fixed-depth
searches with any combination, per-technique counters (`SearchStats`), and two evidence tools: `tools/tactics.cpp` (EPD
runner) and `tools/selfplay.cpp` (node-limited A/B matches with swapped colours). The tactical suite used for the
measurements is the public "Win At Chess" file; it is not part of the repository.

Tests: `test_stage17` (identity tests for the score-preserving techniques, SEE against an exhaustive reference, null-move
make/unmake, counters proving every technique fires, a tactical regression set, mate tests, repetition, extension cap, pinned
regression values). `test_stage17 see null` runs only the named sections.

Known limits: LMR can hide a quiet forced mate at a fixed depth (the engine still mated in 19 of 19 random KQ-vs-K and KR-vs-K
positions, taking about a third more plies); the self-play numbers have wide error bars (44 games is about +-80 Elo); the
mutation tests leave a few guards unobserved (see `PROJECT_STATE.md`).