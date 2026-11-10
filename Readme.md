# MateX

A chess engine written from scratch in modern C++20, built one stage at a time.
Correctness and testing come first, speed comes second: every stage ships with its own tests,
and the move generator is checked against independently computed reference counts.

**Current state:** a complete rule-correct move generator, a tapered static evaluation with nine optional features,
and an alpha-beta search with quiescence, move ordering, iterative deepening, a transposition table and the usual
modern techniques (PVS, null move, LMR, pruning, SEE). It plays legal, sensible chess from the command line.
A UCI interface (to plug it into a chess GUI) is planned but not built yet.

## Features

- **Board:** bitboards plus a mailbox, kept in sync and cross-checked.
- **Rules:** full legal move generation (castling, en passant, promotion), checkmate, stalemate, dead position,
  50/75-move and 3/5-fold repetition.
- **Verified:** a 37-position perft suite (189 node counts, about 73 million nodes) matches reference counts.
- **Evaluation:** material, piece-square tables, pawn structure, bishop pair, mobility, king safety, blended between
  middlegame and endgame by the material left (tapered). Nine extra features can each be switched on and measured
  separately (taper, mobility, passed pawns, pawn structure, outposts, rooks, king zone, threats, space).
- **Search:** negamax with alpha-beta, quiescence search, move ordering (hash move, captures by MVV-LVA, killers,
  history), iterative deepening with time/node limits, Zobrist hashing and a transposition table, plus principal
  variation search, aspiration windows, null move, late move reductions, futility and reverse futility pruning,
  check extensions, static exchange evaluation and repetition detection. Every technique is an option.
- **Tools:** an interactive `play` program, `perft_cli`, benchmarks, a tactical-suite runner (`tactics`), a self-play
  match tool (`selfplay`), and one test program per stage.

## Quick start

You need a C++20 compiler (GCC/MinGW or Clang) and CMake 3.16 or newer.

**Windows (MinGW):**

```powershell
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
mingw32-make
```

**Linux / macOS:**

```bash
mkdir build && cd build
cmake .. && make
```

Then play against the engine:

```bash
./play
```

Type a move such as `e2e4` (promotion: `e7e8q`). A short session:

```
> e2e4
Black to move, 20 legal moves.
> go 4
best b8c6  depth 4  score -37  nodes 7449  pv b8c6 b1c3 g8f6 g1f3
> engine time 1000        <- the engine thinks for one second and plays its move
```

| Command | What it does |
|---|---|
| `e2e4` | play a legal move |
| `moves` | list the legal moves |
| `go 5` / `go time 2000` | analyse to depth 5, or for 2000 ms |
| `engine 5` / `engine time 2000` | let the engine play its move |
| `eval` | show the evaluation terms |
| `undo`, `new`, `fen <FEN>` | take back, restart, load a position |
| `hash`, `hashfull`, `clearhash` | Zobrist key and transposition-table statistics |
| `options [list]` | show or set the search techniques (`options default`, `options pvs,null`, `options all,-sing`) |
| `evalopts [list]` | show or set the evaluation features (`evalopts none`, `evalopts default`, `evalopts all,-space`) |
| `help`, `quit` | |

## Other programs

```bash
./perft_cli 5                  # count nodes to depth 5 from the start position
./perft_cli --divide 4 "<FEN>" # per-move counts, for comparing with another engine
./perft_cli --suite            # the whole perft suite (about 7 seconds)
./bench_search 4               # how much each move-ordering technique saves
./bench_tt 5                   # what the transposition table buys
./chess_engine "<FEN>"         # print and validate any position
./tactics wac.epd --nodes 20000 --opts default --eval default   # solve an EPD test suite at a node budget
./selfplay --a default --b pvs --nodes 10000 --pairs 22         # node-limited match between two configurations
./selfplay --a default --b default --ea all --eb none --nodes 5000 --pairs 100   # compare two evaluations
```
(`tactics` and `selfplay` are the tools behind the Stage 17 and 18 measurements; `tactics` needs an EPD test-suite file such as Win At Chess, which is not included in the repository.)

## Testing

Each stage has its own test program (`test_stage1` to `test_stage18`), plus `test_regression` for bugs found
in audits. All commands below are run from the `build` folder after building. On Windows (MinGW) run the
programs as `test_stage1.exe`, or just `test_stage1` in PowerShell/cmd.

### Run everything

```bash
ctest --output-on-failure        # all 19 test programs, about 4 minutes
ctest -j4 --output-on-failure    # same, 4 at a time (faster)
ctest -N                         # just list the test names
```

### Run one stage

Either run the program directly (it prints a `FAIL:` line for every failed check and a summary at the end),
or ask CTest for it by name with `-R`:

```bash
./test_stage9                                  # run the program directly
ctest -R Stage9PerftTests --output-on-failure  # same test through CTest
```

| Stage | Command | CTest name | What it checks | Time* |
|---|---|---|---|---|
| 1 | `./test_stage1` | `Stage1Tests` | Board representation: bitboards, mailbox, piece placement, consistency | <1 s |
| 2 | `./test_stage2` | `Stage2Tests` | FEN parsing and generation, round trips, rejection of bad FENs | <1 s |
| 3 | `./test_stage3` | `Stage3Tests` | Attack detection: which squares each piece attacks, checks | <1 s |
| 4 | `./test_stage4` | `Stage4Tests` | Pseudo-legal move generation for every piece type | <1 s |
| 5 | `./test_stage5` | `Stage5Tests` | Legal move generation (moves that leave the king in check are removed) | <1 s |
| 6 | `./test_stage6` | `Stage6Tests` | Special rules: castling safety and rights, en passant, promotion | <1 s |
| 7 | `./test_stage7` | `Stage7Tests` | Make / unmake restores the exact position | <1 s |
| 8 | `./test_stage8` | `Stage8Tests` | Checkmate, stalemate, dead position, 50/75-move rules, 3/5-fold repetition | <1 s |
| 9 | `./test_stage9` | `Stage9PerftTests` | Perft against 37 positions with independently computed counts, plus `divide` and leaf breakdowns | ~10 s |
| 10 | `./test_stage10` | `Stage10EvalTests` | Static evaluation terms | ~1 s |
| 11 | `./test_stage11` | `Stage11SearchTests` | Negamax: best moves, mate scores, principal variation | ~5 s |
| 12 | `./test_stage12` | `Stage12AlphaBetaTests` | Alpha-beta gives exactly the same scores as plain minimax, with fewer nodes | ~16 s |
| 13 | `./test_stage13` | `Stage13QuiescenceTests` | Quiescence search, including in-check evasions | ~14 s |
| 14 | `./test_stage14` | `Stage14OrderingTests` | Move ordering never changes the score, only the node count | ~58 s |
| 15 | `./test_stage15` | `Stage15IterativeTests` | Iterative deepening, time and node limits, interruption, fallback move | ~29 s |
| 16 | `./test_stage16` | `Stage16HashTTTests` | Zobrist keys, transposition table, replacement policies, search with the table | ~38 s |
| 17 | `./test_stage17` | `Stage17AdvancedSearchTests` | Each search technique: options-off equals Stage 16, PVS / SEE ordering / aspiration keep the score, SEE against an exhaustive reference, null move, repetition, mates, tactics (`./test_stage17 see null` runs named sections) | ~100 s |
| 18 | `./test_stage18` | `Stage18EvaluationTests` | Tapered evaluation and each feature: no features equals a frozen Stage 10 copy, colour / mirror symmetry, features change only their own term, hand-computed values, search still sound (`./test_stage18 terms phase` runs named sections) | ~11 s |
| - | `./test_regression` | `RegressionTests` | Bugs found in earlier audits, cross-checked against python-chess | <1 s |

\*Approximate, on a Linux build; your machine may differ. A passing program ends with no `FAIL:` lines and exits
with code 0 (`echo $?` on Linux/macOS, `echo %errorlevel%` in cmd).

### Run groups of stages

```bash
ctest -R "Stage[1-8]Tests"                      # fast rule tests only (stages 1-8)
ctest -R "Stage(9|10)" --output-on-failure      # perft and evaluation
ctest -R "Stage1[1-8]" --output-on-failure      # stages 11-18 (search and evaluation)
ctest -E "Stage1[2-7]"                          # everything except the slow search tests
```

### Other checks (not part of CTest)

```bash
./perft_cli --suite                      # the 37-position perft suite, printed position by position
./perft_cli --divide 4 "<FEN>"           # per-move node counts, to compare with another engine
./bench_search 4                         # node counts for each move-ordering technique (stage 14)
./bench_tt 5                             # transposition-table benefit and replacement policies (stage 16)
echo "<FEN>" | ./eval_cli                # evaluation terms for any position (stages 10 and 18)
echo "4 <FEN>" | ./search_cli            # score, nodes and best move for "<depth> <FEN>" (stages 11-17)
echo "<FEN>" | ./hash_cli                # Zobrist key of any position (stage 16)
```

## Project layout

```
src/
  board/    types, bitboards, Position, FEN, attack detection, Zobrist keys
  move/     moves, pseudo-legal and legal generation, make/unmake
  rules/    castling and en passant helpers, validation, repetition, game state
  perft/    perft, divide, detailed statistics, the standard suite
  eval/     static evaluation (basic + tapered features)
  search/   alpha-beta, quiescence, ordering, iterative deepening, transposition table, SEE, advanced techniques
  main.cpp  command-line position inspector
tools/      play, perft_cli, eval_cli, search_cli, bench_search, bench_tt, hash_cli, tactics, selfplay
tests/      one test program per stage, plus regression tests
docs/       roadmap, rule checklist, detailed stage notes
```

## Roadmap

| Stage | Topic | Status |
|---|---|---|
| 1 | Board representation (bitboards + mailbox) | Done |
| 2 | FEN parsing and generation | Done |
| 3 | Attack detection | Done |
| 4 | Pseudo-legal move generation | Done |
| 5 | Legal move generation | Done |
| 6 | Special rules (castling, en passant, promotion) | Done |
| 7 | Make / unmake | Done |
| 8 | Game state and draw rules | Done |
| 9 | Perft and complete rule verification | Done |
| 10 | Evaluation | Done |
| 11 | Negamax search | Done |
| 12 | Alpha-beta pruning | Done |
| 13 | Quiescence search | Done |
| 14 | Move ordering | Done |
| 15 | Iterative deepening and time management | Done |
| 16 | Zobrist hashing and transposition table | Done |
| 17 | Advanced search (PVS, null move, LMR, pruning, SEE, ...) | Done |
| 18 | Advanced evaluation (tapering and nine features) | Done |
| 19 | UCI protocol | Planned |
| 20 | Final strength work | Planned |

## Design in brief

- Squares use little-endian rank-file numbering: a1 = 0, h1 = 7, a8 = 56, h8 = 63.
- Legal moves are pseudo-legal moves filtered by making each one and checking the mover's king.
- `makeMove` returns a small `UndoInfo` that the caller keeps; `unmakeMove` restores the exact position.
- Scores are in centipawns from the side to move's point of view; mate scores encode the distance to mate.
- Rule: no speed-up is claimed without a measurement.

More detail, stage by stage (including the Stage 17 and 18 measurements), is in [docs/STAGE_NOTES.md](docs/STAGE_NOTES.md). The rule-by-rule proof of
correctness is in [docs/RULE_CHECKLIST.md](docs/RULE_CHECKLIST.md).

## Known limits

- Search techniques and evaluation features were tuned by short self-play matches; single features are often within
  the measurement noise (about +-40 Elo per 200 games), so some choices are "not shown to hurt", not "proven to help".
- The halfmove clock is not part of the hash key (the 50-move rule depends on the path, as in other engines).
- No UCI interface yet, so it cannot be loaded into a chess GUI.

## Contributing

Bug reports, test positions and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MateX is released under the [MIT License](LICENSE).