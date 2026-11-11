cmake_minimum_required(VERSION 3.16)
project(ChessEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# Stage 19: the UCI engine searches on a worker thread (std::thread needs -pthread on Linux).
find_package(Threads)
if(Threads_FOUND)
    link_libraries(Threads::Threads)
endif()

# --- Core engine sources (grows stage by stage) -----------------------------
set(ENGINE_SOURCES
    src/board/types.cpp
    src/board/bitboard.cpp
    src/board/position.cpp
    src/board/fen.cpp
    src/board/attacks.cpp
    src/board/zobrist.cpp
    src/move/move.cpp
    src/move/movegen.cpp
    src/move/makemove.cpp
    src/move/legal.cpp
    src/rules/special.cpp
    src/rules/validate.cpp
    src/rules/repetition.cpp
    src/rules/gamestate.cpp
    src/perft/perft.cpp
    src/perft/suite.cpp
    src/eval/evaluate.cpp
    src/search/tt.cpp
    src/search/see.cpp
    src/search/search.cpp
    src/uci/timeman.cpp
    src/uci/uci.cpp
)

# --- Main executable (manual/visual inspection) -----------------------------
add_executable(chess_engine src/main.cpp ${ENGINE_SOURCES})

# --- The UCI engine (Stage 19): run this from a chess GUI ---------------------
add_executable(matex src/uci_main.cpp ${ENGINE_SOURCES})

# --- Interactive move-playing tool (manual checking)
add_executable(play tools/play.cpp ${ENGINE_SOURCES})

# --- Perft tool (perft / divide / detailed / suite)
add_executable(perft_cli tools/perft_cli.cpp ${ENGINE_SOURCES})

# --- Evaluation dump tool (batch cross-check)
add_executable(eval_cli tools/eval_cli.cpp ${ENGINE_SOURCES})

# --- Search dump tool (batch cross-check)
add_executable(search_cli tools/search_cli.cpp ${ENGINE_SOURCES})

# --- Move-ordering benchmark
add_executable(bench_search tools/bench_search.cpp ${ENGINE_SOURCES})

# --- Hash dump tool (batch cross-check against python-chess)
add_executable(hash_cli tools/hash_cli.cpp ${ENGINE_SOURCES})

# --- Tactical test-suite runner (EPD files with best moves) and node-limited self-play matches (Stage 17)
add_executable(tactics tools/tactics.cpp ${ENGINE_SOURCES})
add_executable(selfplay tools/selfplay.cpp ${ENGINE_SOURCES})

# --- Transposition-table benchmark (nodes with/without TT, replacement policies)
add_executable(bench_tt tools/bench_tt.cpp ${ENGINE_SOURCES})

# --- Test executables --------------------------------------------------------
add_executable(test_stage1 tests/test_stage1.cpp ${ENGINE_SOURCES})
add_executable(test_stage2 tests/test_stage2.cpp ${ENGINE_SOURCES})
add_executable(test_stage3 tests/test_stage3.cpp ${ENGINE_SOURCES})
add_executable(test_stage4 tests/test_stage4.cpp ${ENGINE_SOURCES})
add_executable(test_stage5 tests/test_stage5.cpp ${ENGINE_SOURCES})
add_executable(test_stage6 tests/test_stage6.cpp ${ENGINE_SOURCES})
add_executable(test_stage7 tests/test_stage7.cpp ${ENGINE_SOURCES})
add_executable(test_stage8 tests/test_stage8.cpp ${ENGINE_SOURCES})
add_executable(test_stage9 tests/test_stage9.cpp ${ENGINE_SOURCES})
add_executable(test_stage10 tests/test_stage10.cpp ${ENGINE_SOURCES})
add_executable(test_stage11 tests/test_stage11.cpp ${ENGINE_SOURCES})
add_executable(test_stage12 tests/test_stage12.cpp ${ENGINE_SOURCES})
add_executable(test_stage13 tests/test_stage13.cpp ${ENGINE_SOURCES})
add_executable(test_stage14 tests/test_stage14.cpp ${ENGINE_SOURCES})
add_executable(test_stage15 tests/test_stage15.cpp ${ENGINE_SOURCES})
add_executable(test_stage16 tests/test_stage16.cpp ${ENGINE_SOURCES})
add_executable(test_stage17 tests/test_stage17.cpp ${ENGINE_SOURCES})
add_executable(test_stage18 tests/test_stage18.cpp ${ENGINE_SOURCES})
add_executable(test_stage19 tests/test_stage19.cpp ${ENGINE_SOURCES})
add_executable(test_regression tests/test_regression.cpp ${ENGINE_SOURCES})

enable_testing()
add_test(NAME Stage1Tests COMMAND test_stage1)
add_test(NAME Stage2Tests COMMAND test_stage2)
add_test(NAME Stage3Tests COMMAND test_stage3)
add_test(NAME Stage4Tests COMMAND test_stage4)
add_test(NAME Stage5Tests COMMAND test_stage5)
add_test(NAME Stage6Tests COMMAND test_stage6)
add_test(NAME Stage7Tests COMMAND test_stage7)
add_test(NAME Stage8Tests COMMAND test_stage8)
add_test(NAME Stage9PerftTests COMMAND test_stage9)
add_test(NAME Stage10EvalTests COMMAND test_stage10)
add_test(NAME Stage11SearchTests COMMAND test_stage11)
add_test(NAME Stage12AlphaBetaTests COMMAND test_stage12)
add_test(NAME Stage13QuiescenceTests COMMAND test_stage13)
add_test(NAME Stage14OrderingTests COMMAND test_stage14)
add_test(NAME Stage15IterativeTests COMMAND test_stage15)
add_test(NAME Stage16HashTTTests COMMAND test_stage16)
add_test(NAME Stage17AdvancedSearchTests COMMAND test_stage17)
add_test(NAME Stage18EvaluationTests COMMAND test_stage18)
add_test(NAME Stage19UciTests COMMAND test_stage19)
add_test(NAME RegressionTests COMMAND test_regression)