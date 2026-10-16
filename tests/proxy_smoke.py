# Modernization contributions: Copyright (c) 2026 LaWaylon Battles (aKa Yami).
"""Real TCP tests of the optional trusted PROXY v1 listener."""
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

from smoke import Client, PASSWORD, wait_for_port


class Proxied(Client):
    def __init__(self, port, address, family="TCP4", fragmented=False):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.socket.settimeout(5)
        self.pending = b""
        destination = "2001:db8::ff" if family == "TCP6" else "192.0.2.1"
        header = f"PROXY {family} {address} {destination} 12345 4000\r\n".encode()
        if fragmented:
            for byte in header:
                self.socket.sendall(bytes((byte,)))
        else:
            self.socket.sendall(header)
        self.expect(b"Welcome to the Gladiator Pits!")


def main():
    binary, admin = map(lambda s: str(Path(s).resolve()), sys.argv[1:3])
    env = dict(os.environ, GLAD_BIND_ADDRESS="127.0.0.1", GLAD_TRUSTED_PROXY="127.0.0.1")
    clients, processes = [], []
    with tempfile.TemporaryDirectory(prefix="glad-proxy-smoke-") as directory:
        root = Path(directory)

        def start(environment=env):
            (root / "log.txt").unlink(missing_ok=True)
            process = subprocess.Popen([binary], cwd=root, env=environment,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            processes.append(process)
            return process, wait_for_port(process, root)

        def connect(address, **options):
            client = Proxied(port, address, **options)
            clients.append(client)
            return client

        def denied(client, limit=False):
            client.send("login Proxyuser")
            client.expect(b"Password: ")
            client.send(PASSWORD if limit else "incorrect-fixture-password")
            client.expect(b"Authentication limit reached" if limit else b"Authentication failed")

        def closed(raw):
            raw.settimeout(12)
            try:
                assert not raw.recv(8192), "Rejected peer received application data"
            except ConnectionResetError:
                pass
            finally:
                raw.close()

        def raw_connection(source="127.0.0.1"):
            raw = socket.socket()
            raw.settimeout(5)
            raw.bind((source, 0))
            raw.connect(("127.0.0.1", port))
            return raw

        try:
            # Invalid trust configuration refuses startup before listener/account DB.
            bad = subprocess.run([binary], cwd=root,
                                 env=dict(env, GLAD_TRUSTED_PROXY="not-an-address"),
                                 capture_output=True, timeout=5)
            assert bad.returncode == 1 and b"Invalid GLAD_TRUSTED_PROXY" in bad.stderr
            assert not (root / "accounts.sqlite3").exists()
            process, port = start()

            raw = raw_connection("127.0.0.2")
            try:
                raw.sendall(b"PROXY TCP4 203.0.113.99 192.0.2.1 123 4000\r\n")
            except (BrokenPipeError, ConnectionResetError):
                pass
            closed(raw)  # Actual transport peer differs from the configured proxy.
            for header in (
                b"PROXY UNKNOWN\r\n", b"PROXY TCP4 bad 192.0.2.1 1 2\r\n",
                b"PROXY TCP4 203.0.113.1 192.0.2.1 65536 2\r\n",
                b"PROXY TCP4 203.0.113.1 192.0.2.1 1 2 extra\r\n",
                b"PROXY TCP4 203.0.113.1 192.0.2.1 1 2\n",
                b"login Proxyuser\r\n", b"\r\n", b"x" * 108,
                b"\r\n\r\n\0\r\nQUIT\n",  # Binary v2 signature is not accepted as v1.
            ):
                raw = raw_connection()
                raw.sendall(header)
                closed(raw)

            # No banner/broadcast/authentication before a valid, complete header.
            idle = raw_connection()
            idle.settimeout(0.2)
            try:
                data = idle.recv(1)
                raise AssertionError("Headerless peer received data or closed too early")
            except socket.timeout:
                pass
            first = connect("203.0.113.1", fragmented=True)
            first.authenticate("Proxyuser", register=True)
            first.close()
            for _ in range(4):
                attempt = connect("203.0.113.1")
                denied(attempt)
                attempt.close()
            first = connect("203.0.113.1")
            denied(first, limit=True)
            # Supplying a second header cannot change the admitted identity.
            first.send("PROXY TCP4 203.0.113.55 192.0.2.1 1 2", "who")
            first.expect(b"visible")
            denied(first, limit=True)
            other = connect("203.0.113.2")
            other.authenticate("Proxyuser")
            for _ in range(5):
                v6 = connect("2001:db8::1", family="TCP6")
                denied(v6)
                v6.close()
            v6 = connect("2001:db8::1", family="TCP6")
            denied(v6, limit=True)
            v6_other = connect("2001:db8::2", family="TCP6", fragmented=True)
            v6_other.authenticate("Proxyuser")
            mapped = connect("::ffff:203.0.113.1", family="TCP6")
            denied(mapped, limit=True)
            other.send("who")
            other.expect(b"visible")
            closed(idle)  # Ten-second header deadline, even without incoming bytes.
            result = subprocess.run([admin, "role", "Proxyuser", "administrator"],
                                    cwd=root, env=env, capture_output=True, timeout=5)
            assert result.returncode == 0 and not result.stderr
            other.send("shutdown")
            other.expect(b"Shutdown by ")
            _, stderr = process.communicate(timeout=8)
            assert process.returncode == 0 and not stderr, "Proxy server/sanitizer failed"

            # Default mode: header-shaped commands do not override the actual peer.
            direct_env = dict(env)
            del direct_env["GLAD_TRUSTED_PROXY"]
            process, port = start(direct_env)
            direct = Client(port)
            clients.append(direct)
            direct.send("PROXY TCP4 203.0.113.1 192.0.2.1 1 2", "who")
            direct.expect(b"visible")
            direct.authenticate("Proxyuser")
            direct.send("shutdown")
            direct.expect(b"Shutdown by ")
            _, stderr = process.communicate(timeout=8)
            assert process.returncode == 0 and not stderr
            print("Trusted proxy TCP passed: exact transport trust, mandatory/fragmented "
                  "headers, malformed/spoof/timeout rejection, independent IPv4/IPv6 "
                  "budgets, mapped-address normalization, default direct mode.")
        finally:
            for client in clients:
                client.close()
            for process in processes:
                if process.poll() is None:
                    process.terminate()
                process.communicate(timeout=10)


if __name__ == "__main__":
    main()
