# Contributing to MateX

Thanks for your interest! MateX is built one stage at a time (see the roadmap in the README), so the most
useful contributions are the ones that keep the engine correct and well tested.

## Ways to help

- **Report a bug.** Include the FEN of the position, the move or command that went wrong, and what you
  expected. A wrong move count from `perft_cli --divide` against another engine is the best possible report.
- **Suggest test positions.** Tricky positions (pins, en passant, promotions, repetition, mates) are welcome.
- **Improve docs or comments.** Anything that makes the code easier to follow.
- **Send a fix or a feature** as a pull request (below).

## Building and testing

```bash
mkdir build && cd build
cmake .. && make          # Windows MinGW: cmake .. -G "MinGW Makefiles" && mingw32-make
ctest --output-on-failure
```

Every pull request should leave `ctest` fully green. Please also build once with warnings enabled
(`cmake .. -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic"`) and keep the build warning-free.

## Ground rules

1. **Correctness first.** Priority order: rule correctness, testing, a working engine, search strength,
   performance, advanced techniques. A fast but illegal move is worthless.
2. **Every change comes with a test.** For a bug fix, add a regression test (see `tests/test_regression.cpp`)
   that fails before the fix and passes after it.
3. **Measure, don't guess.** Do not claim a speed-up or a strength gain without numbers (`bench_search`,
   `bench_tt`, or a match result with its error margin).
4. **Keep changes small and focused.** One idea per pull request. Make the smallest fix that solves the problem.
5. **Don't skip ahead.** The project grows stage by stage; a new technique should come with its explanation,
   tests and a benchmark showing that it helps.

## Code style

- C++20, no external dependencies, standard library only.
- Follow the style of the surrounding code: clear names, short functions, comments that explain *why*.
- Squares are little-endian (a1 = 0, h8 = 63); scores are centipawns from the side to move's view.
- Keep source files free of tabs and trailing whitespace.

## Submitting a pull request

1. Fork the repository and create a branch (`fix-castling-bug`, `stage-17-null-move`, ...).
2. Make your change, add tests, and run `ctest`.
3. Commit with a short, descriptive message, for example `Fix en passant after a pinned pawn`.
4. Open a pull request that says what changed, why, and how you tested it.

By contributing you agree that your work is released under the project's [MIT License](LICENSE).
