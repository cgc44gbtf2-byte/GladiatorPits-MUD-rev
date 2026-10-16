# Modernization contributions: Copyright (c) 2026 LaWaylon Battles (aKa Yami).
"""Certificate-verified TLS forwarding fixture; not a production TLS gateway."""
import asyncio
import os
from pathlib import Path
import queue
import socket
import ssl
import subprocess
import sys
import tempfile
import threading

from smoke import Client, PASSWORD, wait_for_port


class Gateway:
    def __init__(self, backend_port, cert, key):
        self.backend_port, self.cert, self.key = backend_port, cert, key
        self.ready, self.errors, self.tasks = queue.Queue(), [], set()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()
        self.port = self.ready.get(timeout=10)
        if isinstance(self.port, Exception):
            raise self.port

    async def forward(self, reader, writer):
        while True:
            chunk = await reader.read(8192)
            if not chunk:
                return
            writer.write(chunk)
            await writer.drain()

    async def accepted(self, reader, writer):
        task = asyncio.current_task()
        self.tasks.add(task)
        backend = None
        pipes = []
        try:
            source, sport = writer.get_extra_info("peername")[:2]
            incoming, backend = await asyncio.open_connection("127.0.0.1", self.backend_port)
            backend.write(f"PROXY TCP4 {source} 127.0.0.1 {sport} {self.port}\r\n".encode())
            await backend.drain()
            pipes = [asyncio.create_task(self.forward(reader, backend)),
                     asyncio.create_task(self.forward(incoming, writer))]
            done, _ = await asyncio.wait(pipes, return_when=asyncio.FIRST_COMPLETED)
            for complete in done:
                complete.result()
        except (ConnectionError, ssl.SSLError, asyncio.CancelledError):
            pass
        except Exception as error:
            self.errors.append(type(error).__name__)  # Never dump connection payloads.
        finally:
            for pipe in pipes:
                pipe.cancel()
            await asyncio.gather(*pipes, return_exceptions=True)
            for stream in (writer, backend):
                if stream:
                    stream.close()
                    try:
                        await stream.wait_closed()
                    except (ConnectionError, ssl.SSLError):
                        pass
            self.tasks.discard(task)

    async def serve(self):
        self.loop, self.stop = asyncio.get_running_loop(), asyncio.Event()
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(self.cert, self.key)
        server = await asyncio.start_server(self.accepted, "127.0.0.1", 0, ssl=context)
        self.ready.put(server.sockets[0].getsockname()[1])
        await self.stop.wait()
        server.close()
        await server.wait_closed()
        for task in list(self.tasks):
            task.cancel()
        await asyncio.gather(*list(self.tasks), return_exceptions=True)

    def run(self):
        try:
            asyncio.run(self.serve())
        except Exception as error:
            self.errors.append(type(error).__name__)
            self.ready.put(error)

    def close(self):
        self.loop.call_soon_threadsafe(self.stop.set)
        self.thread.join(timeout=10)
        assert not self.thread.is_alive() and not self.errors, "TLS forwarding fixture failed"


class SecureClient(Client):
    def __init__(self, port, context, source):
        raw = socket.socket()
        raw.settimeout(5)
        raw.bind((source, 0))
        raw.connect(("127.0.0.1", port))
        self.socket = context.wrap_socket(raw, server_hostname="localhost")
        assert self.socket.version() in ("TLSv1.2", "TLSv1.3")
        self.pending = b""
        self.expect(b"Welcome to the Gladiator Pits!")


def main():
    binary, admin = [str(Path(arg).resolve()) for arg in sys.argv[1:3]]
    env = dict(os.environ, GLAD_BIND_ADDRESS="127.0.0.1", GLAD_TRUSTED_PROXY="127.0.0.1")
    clients, gateway, process = [], None, None
    with tempfile.TemporaryDirectory(prefix="glad-tls-proxy-") as folder:
        root = Path(folder)
        cert, key = root / "fixture-cert.pem", root / "fixture-key.pem"
        result = subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
             "-keyout", str(key), "-out", str(cert), "-days", "1",
             "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"],
            capture_output=True, timeout=30)
        assert result.returncode == 0, "Unable to create isolated fixture certificate"
        key.chmod(0o600)
        context = ssl.create_default_context(cafile=str(cert))
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        try:
            process = subprocess.Popen([binary], cwd=root, env=env,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            port = wait_for_port(process, root)
            gateway = Gateway(port, str(cert), str(key))

            # A trusted CA does not disable hostname verification.
            raw = socket.create_connection(("127.0.0.1", gateway.port), timeout=5)
            try:
                context.wrap_socket(raw, server_hostname="wrong-host.invalid")
                raise AssertionError("Wrong TLS hostname was accepted")
            except ssl.SSLCertVerificationError:
                raw.close()
            raw = socket.create_connection(("127.0.0.1", gateway.port), timeout=5)
            try:
                ssl.create_default_context().wrap_socket(raw, server_hostname="localhost")
                raise AssertionError("Untrusted TLS certificate was accepted")
            except ssl.SSLCertVerificationError:
                raw.close()

            def client(source):
                connection = SecureClient(gateway.port, context, source)
                clients.append(connection)
                return connection

            first = client("127.0.0.2")
            first.authenticate("Tlsuser", register=True)
            first.close()
            for _ in range(4):
                attempt = client("127.0.0.2")
                attempt.send("login Tlsuser")
                attempt.expect(b"Password: ")
                attempt.send("incorrect-fixture-password")
                attempt.expect(b"Authentication failed")
                attempt.close()
            limited = client("127.0.0.2")
            limited.send("login Tlsuser")
            limited.expect(b"Password: ")
            limited.send(PASSWORD)
            limited.expect(b"Authentication limit reached")
            other = client("127.0.0.3")
            other.authenticate("Tlsuser")
            result = subprocess.run([admin, "role", "Tlsuser", "administrator"],
                                    cwd=root, env=env, capture_output=True, timeout=5)
            assert result.returncode == 0 and not result.stderr
            other.send("staff")
            other.expect(b"STAFF DASHBOARD")
            other.send("shutdown")
            other.expect(b"Shutdown by ")
            _, stderr = process.communicate(timeout=10)
            assert process.returncode == 0 and not stderr, "TLS-proxy backend/sanitizer failed"
            print("TLS proxy TCP passed: certificate/hostname verification, rejected wrong "
                  "hostname, real peer forwarding, independent client login budgets, "
                  "authenticated staff menu and normal shutdown.")
        finally:
            for connection in clients:
                connection.close()
            if gateway:
                gateway.close()
            if process:
                if process.poll() is None:
                    process.terminate()
                process.communicate(timeout=10)


if __name__ == "__main__":
    main()
