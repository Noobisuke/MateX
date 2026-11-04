cmake_minimum_required(VERSION 3.16)
project(ChessEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# --- Core engine sources (grows stage by stage) -----------------------------
set(ENGINE_SOURCES
    src/board/types.cpp
    src/board/bitboard.cpp
    src/board/position.cpp
    src/board/fen.cpp
    src/board/attacks.cpp
    src/move/move.cpp
    src/move/movegen.cpp
    src/move/legal.cpp
    src/rules/special.cpp
)

# --- Main executable (manual/visual inspection) -----------------------------
add_executable(chess_engine src/main.cpp ${ENGINE_SOURCES})

# --- Test executables --------------------------------------------------------
add_executable(test_stage1 tests/test_stage1.cpp ${ENGINE_SOURCES})
add_executable(test_stage2 tests/test_stage2.cpp ${ENGINE_SOURCES})
add_executable(test_stage3 tests/test_stage3.cpp ${ENGINE_SOURCES})
add_executable(test_stage4 tests/test_stage4.cpp ${ENGINE_SOURCES})
add_executable(test_stage5 tests/test_stage5.cpp ${ENGINE_SOURCES})
add_executable(test_stage6 tests/test_stage6.cpp ${ENGINE_SOURCES})

enable_testing()
add_test(NAME Stage1Tests COMMAND test_stage1)
add_test(NAME Stage2Tests COMMAND test_stage2)
add_test(NAME Stage3Tests COMMAND test_stage3)
add_test(NAME Stage4Tests COMMAND test_stage4)
add_test(NAME Stage5Tests COMMAND test_stage5)
add_test(NAME Stage6Tests COMMAND test_stage6)