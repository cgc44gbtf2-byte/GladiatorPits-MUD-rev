# Modernization contributions: Copyright (c) 2026 LaWaylon Battles (aKa Yami).
"""Exercise the actual TCP server using only disposable player records."""

import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time

PASSWORD = "smoke-test-password-only"
NEW_PASSWORD = "new-smoke-test-password-only"


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

    def authenticate(self, name, register=False, password=PASSWORD, telnet=False):
        self.send(f"{'register' if register else 'login'} {name}")
        self.expect(b"Password: ")
        if telnet:
            # Echo acknowledgment plus an 80x10 NAWS frame (payload includes LF).
            frame = bytes((255, 253, 1, 255, 250, 31, 0, 80, 0, 10, 255, 240))
            for byte in frame:
                self.socket.sendall(bytes((byte,)))
        self.send(password)
        if register:
            self.expect(b"Confirm password: ")
            self.send(password)
        self.expect(b"Account authenticated.")


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
    admin = str(Path(sys.argv[2]).resolve())
    clients = []
    environment = dict(os.environ, GLAD_BIND_ADDRESS="127.0.0.1")
    environment.pop("GLAD_TRUSTED_PROXY", None)
    with tempfile.TemporaryDirectory(prefix="gladiatorpits-smoke-") as directory:
        process = None

        def start():
            Path(directory, "log.txt").unlink(missing_ok=True)
            proc = subprocess.Popen(
                [binary], cwd=directory, env=environment,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            return proc, wait_for_port(proc, directory)

        def connect(port):
            client = Client(port)
            clients.append(client)
            return client

        def local(*args):
            result = subprocess.run(
                [admin, *args], cwd=directory, capture_output=True, timeout=5,
            )
            assert result.returncode == 0, "Local account operation failed"

        def stopped(proc):
            stdout, stderr = proc.communicate(timeout=5)
            assert proc.returncode == 0, (stdout, stderr)
            assert not stderr, stderr.decode(errors="replace")

        try:
            process, port = start()
            alice = connect(port)
            alice.send("create Alice")
            alice.expect(b"Permission denied")
            alice.socket.sendall(bytes((255, 250, 31, 0, 80, 0, 24, 255, 240)) + b"who\r\n")
            alice.expect(b"visible")
            alice.authenticate("Alice", register=True, telnet=True)
            train(alice, "Alice")
            bob = connect(port)
            bob.authenticate("Bob", register=True)
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

            bob = connect(port)
            bob.authenticate("Bob", telnet=True)
            bob.send("create Alice")
            bob.expect(b"Character unavailable")
            legacy = Path(directory, "Legacyhero")
            original = Path(directory, "Alice").read_bytes()
            legacy_bytes = b"Legacyhero".ljust(16, b"\0") + original[16:]
            legacy.write_bytes(legacy_bytes)
            bob.send("create Legacyhero")
            bob.expect(b"Existing unlinked saves require local staff approval")
            local("link", "Alice", "Legacyhero")
            assert legacy.read_bytes() == legacy_bytes
            bob.send("shutdown")
            bob.expect(b"Permission denied")

            alice = connect(port)
            alice.authenticate("Alice")
            alice.send("create Alice", "score")
            score = alice.expect(b"Speed:7")
            assert b"Str:9 Dex:9 Sta:9 Siz:2 Wit:1" in score, score
            assert re.search(rb"Win-Loss-Kill:\s*1\s*-\s*0\s*-\s*0", score), score
            local("role", "Alice", "administrator")
            alice.send("shutdown")
            alice.expect(b"Shutdown by Alice")
            stopped(process)

            # Restart: account credentials, role, ownership and gameplay persist.
            process, port = start()
            alice = connect(port)
            alice.authenticate("Alice")
            alice.send("create Alice")
            alice.expect(b"You step from the crowd.")
            other = connect(port)
            other.authenticate("Alice")
            alice.send("password")
            alice.expect(b"Current password: ")
            alice.send(PASSWORD)
            alice.expect(b"New password: ")
            alice.send(NEW_PASSWORD)
            alice.expect(b"Confirm password: ")
            alice.send(NEW_PASSWORD)
            alice.expect(b"Password changed.")
            other.expect_closed()
            alice.send("score")
            alice.expect(b"Speed:7")
            login = connect(port)
            login.send("login Alice")
            login.expect(b"Password: ")
            login.send(PASSWORD)
            login.expect(b"Authentication failed")
            login.authenticate("Alice", password=NEW_PASSWORD)
            login.send("account")
            login.expect(b"Role: administrator")
            limited = connect(port)
            limited.send("login Alice")
            limited.expect(b"Password: ")
            limited.send(NEW_PASSWORD)
            limited.expect(b"Authentication limit reached")
            local("role", "Alice", "player")
            alice.send("shutdown")
            alice.expect(b"Permission denied")
            local("role", "Alice", "administrator")
            alice.send("shutdown")
            alice.expect(b"Shutdown by Alice")
            stopped(process)

            # A detached authentication result cannot be applied to a new socket.
            process, port = start()
            ghost = connect(port)
            ghost.send("login Alice")
            ghost.expect(b"Password: ")
            ghost.send(NEW_PASSWORD)
            ghost.close()
            bob = connect(port)
            bob.authenticate("Bob")
            bob.send("account", "shutdown")
            bob.expect(b"Role: player")
            bob.expect(b"Permission denied")
            alice = connect(port)
            alice.authenticate("Alice", password=NEW_PASSWORD)
            alice.send("shutdown")
            alice.expect(b"Shutdown by Someone in the crowd")
            stopped(process)

            for path in Path(directory).iterdir():
                if path.name.startswith("accounts.sqlite3") or path.name == "log.txt":
                    content = path.read_bytes()
                    assert PASSWORD.encode() not in content
                    assert NEW_PASSWORD.encode() not in content
            assert Path(directory, "accounts.sqlite3").stat().st_mode & 0o077 == 0
            print("TCP smoke passed: Telnet framing, accounts, ownership/legacy protection, gameplay, "
                  "restart, password/session revocation, roles, rate limits, "
                  "disconnected auth jobs, and staff shutdown.")
        finally:
            for client in clients:
                client.close()
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
