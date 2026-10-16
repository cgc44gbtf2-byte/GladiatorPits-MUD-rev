#!/usr/bin/env python3
# Modernization contributions: Copyright (c) 2026 LaWaylon Battles (aKa Yami).
"""Storage failures/recovery against real production TCP binaries, in a fixture."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

from smoke import Client, wait_for_port


def main():
    binary = str(Path(sys.argv[1]).resolve())
    admin = str(Path(sys.argv[2]).resolve())
    environment = dict(os.environ, GLAD_BIND_ADDRESS="127.0.0.1")
    environment.pop("GLAD_TRUSTED_PROXY", None)
    clients = []
    processes = []
    with tempfile.TemporaryDirectory(prefix="glad-storage-smoke-") as directory:
        root = Path(directory)

        def spawn(cwd=directory):
            process = subprocess.Popen(
                [binary], cwd=cwd, env=environment,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            processes.append(process)
            return process

        def start(cwd=directory):
            (Path(cwd) / "log.txt").unlink(missing_ok=True)
            process = spawn(cwd)
            return process, wait_for_port(process, cwd)

        def stopped(process, code):
            _, stderr = process.communicate(timeout=8)
            assert process.returncode == code, "Unexpected storage server exit status"
            if code == 0:
                assert not stderr, "Healthy storage server produced stderr"
            else:
                assert stderr.startswith((b"Player storage ",
                                          b"Cannot lock/recover player storage:")), \
                    "Expected a visible storage-failure console diagnostic"
                assert not any(marker in stderr for marker in
                               (b"AddressSanitizer", b"LeakSanitizer", b"runtime error:")), \
                    "Storage server produced sanitizer diagnostics"

        def connect(port):
            client = Client(port)
            clients.append(client)
            return client

        try:
            process, port = start()
            client = connect(port)
            client.authenticate("Storageuser", register=True)
            # A competing server must fail, without disturbing the live owner.
            stopped(spawn(), 1)
            assert process.poll() is None, "Competing server disturbed the active server"
            client.send("create Storagehero", "train", *(["str"] * 8),
                        *(["dex"] * 8), *(["sta"] * 8), "siz")
            client.send("score")
            client.expect(b"Str:9 Dex:9 Sta:9 Siz:2 Wit:1")
            (root / "Storagehero").mkdir(mode=0o700)
            client.send("leave")
            client.expect(b"Storage failure: gameplay halted.")
            stopped(process, 1)
            journal = root / ".glad-settlement"
            assert journal.is_file(), "Failed outcome lost its prepared intent"
            assert (root / "Storagehero").is_dir(), "Unsafe target was destructively removed"
            # Restart must not accept clients while replay is still impossible.
            stopped(spawn(), 1)
            assert journal.is_file(), "Failed recovery discarded pending intent"
            prepared = journal.read_bytes()
            damaged = bytearray(prepared)
            damaged[0] ^= 1
            journal.write_bytes(damaged)
            stopped(spawn(), 1)
            assert journal.read_bytes() == damaged, "Invalid intent was silently changed"
            journal.write_bytes(prepared)  # Only repair this deliberately corrupted fixture.
            (root / "Storagehero").rmdir()
            # Coordinated stopped-server backup includes ownership and pending intent.
            # Restore into a separate directory, never over the original.
            with tempfile.TemporaryDirectory(prefix="glad-storage-restore-") as restored:
                shutil.copytree(root, restored, dirs_exist_ok=True)
                restored_process, restored_port = start(restored)
                restored_client = connect(restored_port)
                restored_client.authenticate("Storageuser")
                restored_client.send("create Storagehero", "score")
                restored_client.expect(b"Str:9 Dex:9 Sta:9 Siz:2 Wit:1")
                operation = subprocess.run(
                    [admin, "role", "Storageuser", "administrator"], cwd=restored,
                    env=environment, capture_output=True, timeout=8,
                )
                assert operation.returncode == 0 and not operation.stderr, \
                    "Restored fixture administrator assignment failed"
                restored_client.send("shutdown")
                stopped(restored_process, 0)
                assert journal.read_bytes() == prepared, "Restore altered original pending intent"
            process, port = start()
            assert not journal.exists(), "Successful recovery did not retire its intent"
            client = connect(port)
            client.authenticate("Storageuser")
            client.send("create Storagehero", "score")
            client.expect(b"Str:9 Dex:9 Sta:9 Siz:2 Wit:1")
            assert (root / "Storagehero").is_file(), "Recovered record is missing"
            operation = subprocess.run(
                [admin, "role", "Storageuser", "administrator"], cwd=directory,
                env=environment, capture_output=True, timeout=8,
            )
            assert operation.returncode == 0 and not operation.stderr, \
                "Fixture administrator assignment failed"
            client.send("shutdown")
            stopped(process, 0)
            print("Storage recovery TCP passed: failed save halts, exclusive lock, "
                  "blocked/invalid intent refuses startup, replay preserves training, "
                  "coordinated backup/restore, ownership/login and ordinary staff shutdown.")
        finally:
            for client in clients:
                client.close()
            for process in processes:
                if process.poll() is None:
                    process.terminate()
                    process.communicate(timeout=8)


if __name__ == "__main__":
    main()
