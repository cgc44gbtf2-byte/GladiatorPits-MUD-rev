"""Bounded, opt-in multiplayer soak. Only synthetic, temporary data is used."""

import argparse
import concurrent.futures
import ctypes
import itertools
import json
import os
from pathlib import Path
import re
import socket
import sqlite3
import struct
import subprocess
import tempfile
import threading
import time

from smoke import Client, PASSWORD, wait_for_port


class Body(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char * 16), ("stats", ctypes.c_int * 5),
                *[(name, ctypes.c_int) for name in
                  ("speed", "room", "wins", "losses", "kills", "damage")],
                ("opponent", ctypes.c_void_p)]


sequence = itertools.count()
latencies = []


class Peer(Client):
    def __init__(self, port, source):
        self.socket = socket.socket()
        self.socket.settimeout(10)
        self.socket.bind((source, 0))
        self.socket.connect(("127.0.0.1", port))
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.pending = b""
        self.expect(b"Welcome to the Gladiator Pits!")

    def rpc(self, *commands):
        token = f"soakmarker{next(sequence)}"
        self.pending = b""
        started = time.monotonic()
        self.send(*commands, "say " + token)
        response = self.expect(f"You say '{token}'".encode())
        latencies.append((time.monotonic() - started) * 1000)
        return response

    def outcome(self):
        choices = (b"Account authenticated.", b"Authentication busy;",
                   b"Authentication limit reached;", b"Authentication failed")
        while not any(choice in self.pending for choice in choices):
            data = self.socket.recv(8192)
            assert data, "Authentication connection unexpectedly closed"
            self.pending += data
        return next(choice.decode() for choice in choices if choice in self.pending)


def measure(pid):
    text = Path(f"/proc/{pid}/status").read_text()
    return {
        "rss_kib": int(re.search(r"VmRSS:\s+(\d+)", text)[1]),
        "fds": len(list(Path(f"/proc/{pid}/fd").iterdir())),
        "threads": int(re.search(r"Threads:\s+(\d+)", text)[1]),
    }


def counters(root, name):
    path = root / name
    if not path.exists():
        return None
    data = path.read_bytes()
    assert len(data) == ctypes.sizeof(Body), "Native record size changed"
    record = Body.from_buffer_copy(data)
    assert record.name.decode() == name and not record.opponent
    assert tuple(record.stats) == (9, 9, 9, 2, 1)
    assert 0 <= record.damage <= 78
    result = [record.wins, record.losses, record.kills]
    assert min(result) >= 0
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("admin")
    parser.add_argument("--seconds", type=int, default=300)
    parser.add_argument("--label", default="manual")
    args = parser.parse_args()
    assert 15 <= args.seconds <= 3600, "Use a bounded 15..3600 second duration"
    binary, admin = str(Path(args.binary).resolve()), str(Path(args.admin).resolve())
    env = dict(os.environ, GLAD_BIND_ADDRESS="127.0.0.1")
    all_peers, processes, samples = [], [], []
    expected = {f"Soaka{chr(97+i)}": [0, 0, 0] for i in range(24)}
    names = list(expected)
    players = [None] * 24
    stats = {"surrenders": 0, "natural_deaths": 0, "anonymous_churn": 0,
             "graceful_fight_disconnects": 0, "reset_fight_disconnects": 0,
             "auth_burst": {}, "slow_reader_server_reclaimed": False}
    stop_sampling = threading.Event()
    with tempfile.TemporaryDirectory(prefix="glad-multiplayer-soak-") as directory:
        root = Path(directory)
        process = None

        def start():
            (root / "log.txt").unlink(missing_ok=True)
            proc = subprocess.Popen([binary], cwd=root, env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            processes.append(proc)
            return proc, wait_for_port(proc, root)

        def peer(source):
            client = Peer(port, source)
            all_peers.append(client)
            return client

        def local(*arguments):
            result = subprocess.run([admin, *arguments], cwd=root, env=env,
                                    capture_output=True, timeout=10)
            assert result.returncode == 0 and not result.stderr, "Fixture admin failed"

        def parallel(function, values):
            with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
                return list(pool.map(function, values))

        def connect_player(index, register=False):
            client = peer(f"127.0.1.{index+1}")
            client.authenticate(names[index], register=register)
            players[index] = client
            return client

        def train(index):
            client = players[index]
            response = client.rpc(
                "create " + names[index], "train", *(["str"] * 8),
                *(["dex"] * 8), *(["sta"] * 8), "siz", "leave", "score")
            assert b"Str:9 Dex:9 Sta:9 Siz:2 Wit:1" in response
            assert counters(root, names[index]) == expected[names[index]]

        def fight(pair):
            first, second = pair
            response = players[first].rpc("challenge " + names[second])
            assert b"You challenge " in response
            response = players[second].rpc("accept")
            assert f"[{names[second]} and {names[first]} have entered the arena]".encode() in response

        def natural(pair, offline=False):
            first, second = pair
            missing = [i for i in pair if not (root / names[i]).exists()]
            assert len(missing) <= 1, "Both characters unexpectedly disappeared"
            if not missing:
                return
            loser = missing[0]
            winner = second if loser == first else first
            expected[names[winner]][0] += 1
            expected[names[winner]][2] += 1
            assert counters(root, names[winner]) == expected[names[winner]]
            expected[names[loser]] = None
            stats["natural_deaths"] += 1
            if not offline:
                expected[names[loser]] = [0, 0, 0]  # A new life, not restored progress.
                train(loser)
                fight(pair)

        def surrender(pair):
            first, second = pair
            loser, winner = (first, second) if rounds % 2 == 0 else (second, first)
            client = players[loser]
            if rounds % 2 == 0:
                client.send("quit")
                client.expect_closed()
                stats["graceful_fight_disconnects"] += 1
            else:
                client.socket.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                         struct.pack("ii", 1, 0))
                client.close()
                stats["reset_fight_disconnects"] += 1
            expected[names[winner]][0] += 1
            expected[names[loser]][1] += 1
            deadline = time.monotonic() + 5
            while (counters(root, names[winner]) != expected[names[winner]] or
                   counters(root, names[loser]) != expected[names[loser]]):
                assert time.monotonic() < deadline, "Surrender failed to settle exactly once"
                time.sleep(0.01)
            connect_player(loser)
            response = players[loser].rpc("create " + names[loser], "score")
            assert b"Str:9 Dex:9 Sta:9 Siz:2 Wit:1" in response
            fight(pair)
            stats["surrenders"] += 1

        def shutdown(operator):
            operator.send("shutdown")
            operator.expect(b"Shutdown by ")
            _, stderr = process.communicate(timeout=10)
            assert process.returncode == 0, "Server failed during orderly shutdown"
            assert not stderr, "Unexpected console/sanitizer diagnostics"

        def sample():
            while not stop_sampling.wait(1):
                if process.poll() is not None:
                    break
                samples.append(measure(process.pid))

        try:
            process, port = start()
            parallel(lambda i: connect_player(i, True), range(24))
            parallel(train, range(24))
            operator = peer("127.0.1.251")
            operator.authenticate("Soakoperator", register=True)
            local("role", "Soakoperator", "administrator")
            assert b"Role: administrator" in operator.rpc("account")
            spectators = [operator] + [peer(f"127.0.3.{i+1}") for i in range(7)]
            assert b"Permission denied" in players[4].rpc("shutdown")

            # Exceed the eight-slot queue, not the per-IP/global attempt limits.
            burst = [peer(f"127.0.2.{i+1}") for i in range(12)]
            for i, client in enumerate(burst):
                client.send("login " + names[i])
                client.expect(b"Password: ")
            for client in burst:
                client.send(PASSWORD)
            for client in burst:
                outcome = client.outcome()
                assert outcome in ("Account authenticated.", "Authentication busy;")
                stats["auth_burst"][outcome] = stats["auth_burst"].get(outcome, 0) + 1
                client.close()
            time.sleep(0.3)
            operator.rpc("who")  # Process EOFs before measuring the stable baseline.

            # An unread tiny receive window must not retain a live server descriptor.
            before_pressure = min(measure(process.pid)["fds"] for _ in range(3))
            slow = socket.socket()
            slow.settimeout(5)
            slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
            try:
                slow.connect(("127.0.0.1", port))
                greeting = b""
                while b"Welcome to the Gladiator Pits!" not in greeting:
                    chunk = slow.recv(1024)
                    assert chunk, "Slow-reader connection closed before acceptance"
                    greeting += chunk
                    assert len(greeting) < 8192, "Unexpected greeting size"
                assert measure(process.pid)["fds"] >= before_pressure + 1, \
                    "Slow-reader acceptance was not observed"
                # Stop reading only after acceptance, then provoke real backpressure.
                try:
                    slow.sendall(b"who\n" * 20000)
                except (BrokenPipeError, ConnectionResetError):
                    pass
                deadline = time.monotonic() + 8
                while measure(process.pid)["fds"] > before_pressure:
                    assert time.monotonic() < deadline, "Slow-reader descriptor was not reclaimed"
                    operator.rpc("who")
                    time.sleep(0.05)
                stats["slow_reader_server_reclaimed"] = True
            finally:
                slow.close()

            natural_pairs = [(0, 1), (2, 3)]
            surrender_pairs = list(zip(range(4, 24, 2), range(5, 24, 2)))
            parallel(fight, natural_pairs + surrender_pairs)
            baseline = measure(process.pid)
            sampler = threading.Thread(target=sample, daemon=True)
            sampler.start()
            started = time.monotonic()
            next_round, rounds = started + 20, 0
            while time.monotonic() - started < args.seconds:
                assert process.poll() is None, "Server exited during soak"
                for pair in natural_pairs:
                    natural(pair)
                parallel(lambda client: client.rpc("who"), players + spectators)
                for _ in range(5):
                    transient = peer("127.0.4.1")
                    transient.rpc("who")
                    transient.send("quit")
                    transient.expect_closed()
                    stats["anonymous_churn"] += 1
                if time.monotonic() >= next_round:
                    parallel(surrender, surrender_pairs)
                    rounds += 1
                    next_round = time.monotonic() + 20
                time.sleep(min(5, max(0, started + args.seconds - time.monotonic())))
            stats["active_seconds"] = round(time.monotonic() - started, 2)
            stop_sampling.set()
            sampler.join(timeout=3)
            final = measure(process.pid)
            assert final["fds"] <= baseline["fds"] + 1, "Steady-state descriptors grew"
            assert final["threads"] == baseline["threads"] == 2, "Thread count changed"
            assert final["rss_kib"] - baseline["rss_kib"] < 64 * 1024, \
                "RSS grew by 64 MiB or more; investigate before accepting this run"
            if args.seconds >= 120:
                assert stats["natural_deaths"] > 0 and rounds >= 2
            shutdown(operator)  # Release active fighters without fabricating surrender.
            for pair in natural_pairs:
                natural(pair, offline=True)
            for name in names:
                assert counters(root, name) == expected[name]
            assert not (root / ".glad-settlement").exists(), "Settlement remains after shutdown"
            before_restart = {name: (root / name).read_bytes()
                              for name in names if (root / name).exists()}
            for client in all_peers:
                client.close()

            # Restart the same data: verify every owner, record, role and dead life.
            process, port = start()
            parallel(connect_player, range(24))
            for index, name in enumerate(names):
                response = players[index].rpc("create " + name, "score")
                match = re.search(rb"Win-Loss-Kill:\s*(\d+)\s*-\s*(\d+)\s*-\s*(\d+)", response)
                assert match and list(map(int, match.groups())) == (expected[name] or [0, 0, 0])
                if expected[name] is None:
                    assert b"Str:1 Dex:1 Sta:1 Siz:1 Wit:1" in response
                    assert not (root / name).exists(), "Dead life resurrected on restart"
                else:
                    assert b"Str:9 Dex:9 Sta:9 Siz:2 Wit:1" in response
                    assert (root / name).read_bytes() == before_restart[name]
            operator = peer("127.0.1.251")
            operator.authenticate("Soakoperator")
            assert b"Role: administrator" in operator.rpc("account")
            shutdown(operator)
            with sqlite3.connect(f"file:{root}/accounts.sqlite3?mode=ro", uri=True) as db:
                assert db.execute("PRAGMA integrity_check").fetchone() == ("ok",)
                assert db.execute("SELECT count(*) FROM accounts").fetchone() == (25,)
                assert db.execute("SELECT count(*) FROM characters").fetchone() == (24,)
            ordered = sorted(latencies)
            stats.update({
                "label": args.label, "requested_seconds": args.seconds,
                "steady_clients": 32, "players": 24, "concurrent_arenas": 12,
                "rpc_samples": len(ordered), "rpc_p95_ms": round(ordered[int(len(ordered)*0.95)], 3),
                "rpc_max_ms": round(max(ordered), 3), "baseline": baseline,
                "final": final, "peak_rss_kib": max(s["rss_kib"] for s in samples),
                "peak_fds": max(s["fds"] for s in samples), "restarts_verified": 1,
                "native_record_bytes": ctypes.sizeof(Body), "sanitizer_stderr_empty": True,
                "saved_progress_and_ownership_verified": True,
            })
            print(json.dumps(stats, sort_keys=True), flush=True)
        finally:
            stop_sampling.set()
            for client in all_peers:
                client.close()
            for proc in processes:
                if proc.poll() is None:
                    proc.terminate()
                _, diagnostics = proc.communicate(timeout=10)
                if diagnostics:
                    # Preserve useful failure evidence, not credentials or raw DB data.
                    lines = diagnostics.decode(errors="replace").splitlines()
                    selected = [line.replace(PASSWORD, "[fixture credential omitted]")
                                for line in lines if re.search(
                                    r"AddressSanitizer|LeakSanitizer|runtime error:|SUMMARY:|^\s*#\d+",
                                    line)]
                    if selected:
                        print("\n".join(selected[:30]), flush=True)


if __name__ == "__main__":
    main()
