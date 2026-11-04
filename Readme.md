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
| 5 | Legal move generation | Next |
| 6-20 | Special rules, make/unmake, draws, perft, evaluation, search, UCI, optimization | Planned |

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

Without CMake:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -o chess_engine src/main.cpp src/board/*.cpp src/move/*.cpp
```

## Run

```bash
./chess_engine                       # starting position
./chess_engine "<FEN string>"        # any position
```

Prints the board, FEN, check status and all pseudo-legal moves.

## Test

Each stage has its own test executable:

```bash
./test_stage1
./test_stage2
./test_stage3
./test_stage4
```

Or run them all with CTest from the build folder: `ctest --output-on-failure`.

## Layout

```
src/
  board/   types, bitboards, Position, FEN, attack detection
  move/    Move, MoveList, pseudo-legal move generation
  main.cpp CLI for manual inspection
tests/     one test executable per stage
```

## Design notes

- Squares use little-endian rank-file mapping: a1 = 0, h1 = 7, a8 = 56, h8 = 63.
- Position stores 12 piece bitboards plus a mailbox array, kept in sync and
  cross-checked by `Position::isConsistent()`.
- Move generation is pseudo-legal for now. Moves that leave the mover's own
  king in check are filtered in Stage 5; full castling legality is Stage 6.