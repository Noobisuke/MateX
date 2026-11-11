#!/usr/bin/env python3
"""Drives the real matex executable through python-chess, the way a chess GUI would.

    python3 tests/uci_check.py path/to/matex        (needs:  pip install chess)
    py tests\\uci_check.py build\\matex.exe          (Windows: run from the project folder; the program is in build\\)

With no argument it looks for matex / matex.exe in ./build and in the current folder.

Checks: handshake and options, legal moves over whole games against a random mover (with fixed depth, with a clock and with
movetime), analysis with stop, mate detection, the FEN/move round trip, and that the engine keeps working after bad input.
"""
import os
import random
import sys
import time

import chess
import chess.engine



def find_engine(arg):
    """Finds the matex program: the given path (with or without .exe), or the usual build locations."""
    names = [arg] if arg else ["build/matex", "matex", "build/Release/matex", "build/Debug/matex"]
    for name in names:
        for candidate in (name, name + ".exe"):
            if os.path.isfile(candidate):
                return os.path.abspath(candidate)
    sys.exit("Cannot find the matex program (" + ", ".join(names) + ").\n"
             "Build it first (cmake .. ; make, inside build/) and pass its path, e.g.  python3 tests/uci_check.py build/matex"
             "   (Windows:  py tests\\uci_check.py build\\matex.exe)")


path = find_engine(sys.argv[1] if len(sys.argv) > 1 else None)
print("engine:", path)
failures = 0


def check(cond, msg):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", msg)


def play_game(engine, limit_for, seed, max_plies=120, engine_color=chess.WHITE):
    """Engine against a random mover. Returns (plies played, board)."""
    rng = random.Random(seed)
    board = chess.Board()
    plies = 0
    while not board.is_game_over(claim_draw=False) and plies < max_plies:
        if board.turn == engine_color:
            res = engine.play(board, limit_for(board))
            check(res.move is not None and res.move in board.legal_moves, f"illegal engine move {res.move} in {board.fen()}")
            if res.move is None or res.move not in board.legal_moves:
                break
            board.push(res.move)
        else:
            board.push(rng.choice(list(board.legal_moves)))
        plies += 1
    return plies, board


with chess.engine.SimpleEngine.popen_uci(path) as engine:
    check("MateX" in engine.id.get("name", ""), f"id name: {engine.id}")
    for opt in ("Hash", "Clear Hash", "Move Overhead", "Techniques", "EvalFeatures", "Debug Log File"):
        check(opt in engine.options, f"option {opt} advertised")
    engine.configure({"Hash": 32, "Move Overhead": 20})

    # 1. whole games, three kinds of limits, both colours
    for seed, (name, lim) in enumerate([
        ("depth 3", lambda b: chess.engine.Limit(depth=3)),
        ("movetime 40ms", lambda b: chess.engine.Limit(time=0.04)),
        ("clock 3s+0.05", lambda b: chess.engine.Limit(white_clock=3.0, black_clock=3.0, white_inc=0.05, black_inc=0.05)),
    ]):
        for color in (chess.WHITE, chess.BLACK):
            plies, board = play_game(engine, lim, 100 + seed, engine_color=color)
            check(plies > 10, f"game ({name}, {'white' if color else 'black'}) lasted only {plies} plies")
            print(f"  game {name:14s} as {'white' if color else 'black'}: {plies} plies, result {board.result(claim_draw=False)}")

    # 2. the engine must win a trivial mate quickly and report it as a mate score
    board = chess.Board("6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1")
    info = engine.analyse(board, chess.engine.Limit(depth=4))
    check(info["score"].is_mate() and info["score"].white().mate() == 1, f"mate in 1 reported: {info['score']}")
    check(info["pv"][0] == chess.Move.from_uci("a1a8"), f"mating move: {info['pv']}")

    # 3. analysis that must be stopped by the GUI
    board = chess.Board()
    with engine.analysis(board) as analysis:
        t0 = time.time()
        depth_seen = 0
        for info in analysis:
            depth_seen = max(depth_seen, info.get("depth", 0))
            if depth_seen >= 6 or time.time() - t0 > 20:
                break
    check(depth_seen >= 6, f"infinite analysis reached depth {depth_seen}")
    pv_ok = True
    b = chess.Board()
    for m in info.get("pv", []):
        if m not in b.legal_moves:
            pv_ok = False
            break
        b.push(m)
    check(pv_ok, "the PV of an analysis line is legal")

    # 4. several different positions: the move is always legal, with and without history
    for fen in ["r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"]:
        b = chess.Board(fen)
        res = engine.play(b, chess.engine.Limit(depth=3))
        check(res.move in b.legal_moves, f"legal move in {fen}")
    b = chess.Board()
    for u in "g1f3 g8f6 f3g1 f6g8".split():
        b.push_uci(u)
    res = engine.play(b, chess.engine.Limit(depth=4))
    check(res.move in b.legal_moves, "legal move after a repetition cycle")

    # 5. options can be changed between games, and a bad value does not break the engine
    engine.configure({"Techniques": "none", "EvalFeatures": "none"})
    res = engine.play(chess.Board(), chess.engine.Limit(depth=3))
    check(res.move in chess.Board().legal_moves, "plays with techniques and features off")
    engine.configure({"Techniques": "default", "EvalFeatures": "default"})
    engine.protocol.send_line("setoption name Hash value banana")
    engine.protocol.send_line("garbage line")
    res = engine.play(chess.Board(), chess.engine.Limit(depth=2))
    check(res.move in chess.Board().legal_moves, "still answers after bad input")
    engine.ping()

print("FAILURES:", failures)
sys.exit(1 if failures else 0)