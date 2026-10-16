"""Exercise the actual TCP server using only disposable player records."""

import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time


class Client:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.socket.settimeout(5)
        self.pending = b""
        self.expect(b"Welcome to the Gladiator Pits!")

    def send(self, *lines):
        self.socket.sendall(("\n".join(lines) + "\n").encode("ascii"))

    def expect(self, text):
        while text not in self.pending:
            data = self.socket.recv(8192)
            if not data:
                raise AssertionError(f"Connection closed before {text!r}")
            self.pending += data
        end = self.pending.index(text) + len(text)
        result, self.pending = self.pending[:end], self.pending[end:]
        return result

    def expect_closed(self):
        while self.socket.recv(8192):
            pass
        self.close()

    def close(self):
        self.socket.close()


def train(client, name):
    client.send(
        f"create {name}",
        "train",
        *(["str"] * 8),
        *(["dex"] * 8),
        *(["sta"] * 8),
        "siz",
        "leave",
        "score",
    )
    score = client.expect(b"Speed:7")
    assert b"Str:9 Dex:9 Sta:9 Siz:2 Wit:1" in score, score


def wait_for_port(process, directory):
    deadline = time.monotonic() + 5
    log = Path(directory, "log.txt")
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError("Server exited during startup")
        if log.exists():
            match = re.search(r"Mud started on port (\d+)", log.read_text())
            if match:
                return int(match.group(1))
        time.sleep(0.05)
    raise AssertionError("Server did not announce its listening port")


def main():
    binary = str(Path(sys.argv[1]).resolve())
    clients = []
    environment = dict(os.environ)
    environment["GLAD_SHUTDOWN_PASSWORD"] = "smoke-test-only"
    with tempfile.TemporaryDirectory(prefix="gladiatorpits-smoke-") as directory:
        process = subprocess.Popen(
            [binary], cwd=directory, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        try:
            port = wait_for_port(process, directory)
            alice = Client(port)
            clients.append(alice)
            train(alice, "Alice")
            bob = Client(port)
            clients.append(bob)
            train(bob, "Bob")

            alice.send("say smoke-room-message")
            alice.expect(b"You say 'smoke-room-message'")
            bob.expect(b"Alice says 'smoke-room-message'")
            alice.send("challenge Bob")
            alice.expect(b"You challenge Bob to a fight.")
            bob.send("accept")
            bob.expect(b"have entered the arena")
            bob.send("quit")
            bob.expect_closed()
            alice.expect(b"Bob vanishes into the crowd")

            alice.send("score")
            score = alice.expect(b"Speed:7")
            assert re.search(rb"Win-Loss-Kill:\s*1\s*-\s*0\s*-\s*0", score), score
            alice.send("quit")
            alice.expect_closed()

            alice = Client(port)
            clients.append(alice)
            alice.send("create Alice", "score")
            score = alice.expect(b"Speed:7")
            assert b"Str:9 Dex:9 Sta:9 Siz:2 Wit:1" in score, score
            assert re.search(rb"Win-Loss-Kill:\s*1\s*-\s*0\s*-\s*0", score), score
            alice.send("shutdown smoke-test-only")
            alice.expect(b"Shutdown by Alice")
            stdout, stderr = process.communicate(timeout=5)
            assert process.returncode == 0, (stdout, stderr)
            assert not stderr, stderr.decode(errors="replace")
            print("TCP smoke passed: creation, training, room chat, PvP surrender, "
                  "reconnect/save, and shutdown.")
        finally:
            for client in clients:
                client.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
