#pragma once
// Stage 19: the UCI (Universal Chess Interface) front end.
//
// UciEngine owns the game state (position + the keys of the positions before it, for repetition detection), one
// transposition table, the options, and a worker thread that runs the search so that "stop" and "quit" can be read
// while the engine thinks. Output goes to any std::ostream (a test passes a stringstream, matex passes std::cout).
//
// Supported commands: uci, isready, ucinewgame, position, go, stop, quit, setoption, debug.
// Extra debugging commands: d, eval, perft N, divide N, bench [depth], options.
//
// Threads: if the standard library has no usable std::thread (some MinGW builds) or MATEX_NO_THREADS is defined, "go"
// runs in the calling thread: time, depth and node limits still work, but "stop" can only be read after the search ends
// and "go infinite" is capped (see kNoThreadInfiniteMs).
#include "../board/position.hpp"
#include "../search/search.hpp"
#include "../search/tt.hpp"
#include "timeman.hpp"
#include <atomic>
#include <fstream>
#include <iosfwd>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "../search/threads.hpp"

#if MATEX_THREADS
#include <mutex>
#include <thread>
using MateXMutex = std::mutex;
using MateXThread = std::thread;
#else
struct MateXMutex { void lock() {} void unlock() {} };
struct MateXThread { bool joinable() const { return false; } void join() {} };
#endif

constexpr const char* MATEX_NAME = "MateX";
constexpr const char* MATEX_VERSION = "0.19";
constexpr int kNoThreadInfiniteMs = 5000;

// Splits a line on whitespace (spaces, tabs, CR).
std::vector<std::string> splitTokens(const std::string& line);

// Fixed benchmark positions used by "bench" (and by the tests).
const std::vector<std::string>& benchFens();

class UciEngine {
public:
    explicit UciEngine(std::ostream& out);
    ~UciEngine();
    UciEngine(const UciEngine&) = delete;
    UciEngine& operator=(const UciEngine&) = delete;

    // Handles one input line. Returns false when the engine should exit ("quit").
    bool handleLine(const std::string& line);
    // Reads lines until "quit" or end of input. At end of input a finite search is allowed to finish; an infinite one is stopped.
    void run(std::istream& in);

    void waitForSearch();            // blocks until the current search has finished and its "bestmove" has been written
    bool searching() const { return searching_.load(); }

    // Introspection (tests).
    const Position& position() const { return pos_; }
    const std::vector<uint64_t>& history() const { return history_; }
    int hashMB() const { return hashMB_; }
    int moveOverhead() const { return moveOverhead_; }
    int threads() const { return threads_; }
    bool debug() const { return debug_; }
    static bool threadsAvailable();

private:
    void emit(const std::string& line);
    void info(const std::string& text) { emit("info string " + text); }

    void cmdUci();
    void cmdPosition(const std::vector<std::string>& t);
    void cmdGo(const std::vector<std::string>& t);
    void cmdSetOption(const std::vector<std::string>& t);
    void cmdNewGame();
    void cmdDisplay();
    void cmdEval();
    void cmdPerft(int depth, bool divideOutput);
    void cmdBench(const std::vector<std::string>& t);

    void searchTask(GoParams go, Position pos, std::vector<uint64_t> history, SearchOptions opts, int64_t timeMs);
    void stopAndJoin();

    std::ostream& out_;
    MateXMutex outMutex_;
    std::ofstream log_;
    std::string logPath_;

    Position pos_;
    std::vector<uint64_t> history_;  // hashes of the positions BEFORE pos_, oldest first (the search's repetition detection)
    TranspositionTable tt_;
    SearchOptions searchOptions_;
    int hashMB_ = 16;
    int moveOverhead_ = 30;
    int threads_ = 1;  // Stage 20: Lazy SMP search threads (UCI option "Threads")
    bool debug_ = false;

    std::atomic<bool> stop_{false};
    std::atomic<bool> searching_{false};
    std::atomic<bool> infiniteSearch_{false};
    MateXThread worker_;
};
