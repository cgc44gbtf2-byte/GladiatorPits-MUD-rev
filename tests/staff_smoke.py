"""Exercise the actual in-game dashboard, using disposable TCP accounts only."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

from smoke import Client, PASSWORD, train, wait_for_port


def main():
    binary, admin = (str(Path(value).resolve()) for value in sys.argv[1:3])
    clients, process = [], None
    with tempfile.TemporaryDirectory(prefix="glad-staff-") as directory:
        def start():
            Path(directory, "log.txt").unlink(missing_ok=True)
            child = subprocess.Popen([binary], cwd=directory, env=dict(os.environ),
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            return child, wait_for_port(child, directory)

        def connect(port, name=None, register=False):
            client = Client(port)
            clients.append(client)
            if name:
                client.authenticate(name, register=register)
            return client

        def local(*arguments):
            result = subprocess.run([admin, *arguments], cwd=directory,
                                    capture_output=True, timeout=8)
            assert result.returncode == 0, "Local staff fixture action failed"

        def stop(owner):
            owner.send("shutdown")
            owner.expect(b"Shutdown by")
            stdout, stderr = process.communicate(timeout=8)
            assert process.returncode == 0, "Server stopped abnormally"
            assert b"AddressSanitizer" not in stderr and b"runtime error:" not in stderr
            assert PASSWORD.encode() not in stdout + stderr

        def change(client, command, success=True, password=PASSWORD):
            client.send(command)
            client.expect(b"Type CONFIRM")
            client.send("CONFIRM")
            client.expect(b"Current staff password: ")
            client.send(password)
            client.expect(b"Staff change applied and audited." if success else b"no change applied")

        try:
            process, port = start()
            owner = connect(port, "Ownerdemo", True)
            local("role", "Ownerdemo", "owner")
            mod = connect(port, "Moddemo", True)
            local("role", "Moddemo", "moderator")
            builder = connect(port, "Builddemo", True)
            local("role", "Builddemo", "builder")
            admin_client = connect(port, "Admindemo", True)
            local("role", "Admindemo", "administrator")
            player = connect(port, "Playerdemo", True)
            player.send("staff")
            player.expect(b"Permission denied. Authenticate or contact staff.")
            player.send("commands")
            command_list = player.expect(b"create")
            assert b"dashboard" not in command_list and b"staff" not in command_list
            builder.send("dashboard")
            menu = builder.expect(b"No web console.")
            assert b"staff world" in menu and b"staff accounts" not in menu
            builder.send("staff accounts")
            builder.expect(b"Permission denied")
            builder.send("staff world")
            builder.expect(b"No persistent area/item/mob")
            owner.send("staff accounts")
            directory_view = owner.expect(b"End of account directory.")
            assert b"Ownerdemo" in directory_view and b"Playerdemo" in directory_view
            train(player, "Savedhero")
            before = Path(directory, "Savedhero").read_bytes()
            mod.send("staff inspect Savedhero")
            live = mod.expect(b"Speed counter:")
            assert b"LIVE read-only snapshot" in live and b"STR:9 DEX:9" in live
            assert Path(directory, "Savedhero").read_bytes() == before
            owner.send("staff role Builddemo administrator")
            owner.expect(b"Type CONFIRM")
            owner.send("cancel")
            owner.expect(b"Staff action cancelled")
            owner.send("staff accounts")
            unchanged = owner.expect(b"End of account directory.")
            assert b"Builddemo" in unchanged and b"builder" in unchanged
            stop(owner)

            process, port = start()
            owner = connect(port, "Ownerdemo")
            mod = connect(port, "Moddemo")
            player = connect(port, "Playerdemo")
            change(owner, "staff role Builddemo moderator")
            mod.send("staff disable Ownerdemo")
            mod.expect(b"Permission denied")
            mod.send("staff role Playerdemo owner")
            mod.expect(b"Permission denied")
            change(mod, "staff disable Playerdemo")
            player.expect_closed()
            owner.send("staff audit")
            audit = owner.expect(b"End of staff audit.")
            assert b"role_changed" in audit and b"access_changed" in audit
            mod.send("staff inspect Savedhero")
            saved = mod.expect(b"Speed counter:")
            assert b"SAVED read-only snapshot" in saved and b"STR:9 DEX:9" in saved
            owner.send("staff online")
            owner.expect(b"End of connected-player preview.")
            stop(owner)

            process, port = start()
            owner = connect(port, "Ownerdemo")
            disabled = connect(port)
            disabled.send("login Playerdemo")
            disabled.expect(b"Password: ")
            disabled.send(PASSWORD)
            disabled.expect(b"Authentication failed")
            change(owner, "staff enable Playerdemo")
            change(owner, "staff role Builddemo administrator", False,
                   password="Wrong disposable staff password!")
            owner.send("staff accounts")
            owner.expect(b"End of account directory.")
            stop(owner)

            process, port = start()
            owner = connect(port, "Ownerdemo")
            mod = connect(port, "Moddemo")
            owner.send("staff role Builddemo administrator")
            owner.expect(b"Type CONFIRM")
            owner.send("CONFIRM")
            owner.expect(b"Current staff password: ")
            local("role", "Admindemo", "owner")
            local("role", "Ownerdemo", "builder")
            owner.send(PASSWORD)
            owner.expect(b"Staff change denied or unchanged")
            local("role", "Ownerdemo", "owner")
            owner.send("staff role Builddemo administrator")
            owner.expect(b"Type CONFIRM")
            owner.send("CONFIRM")
            owner.expect(b"Current staff password: ")
            owner.send(PASSWORD)
            owner.close()
            # The single auth worker finishes that proof before the next login.
            owner = connect(port, "Ownerdemo")
            owner.send("staff accounts")
            view = owner.expect(b"End of account directory.")
            row = next(line for line in view.splitlines() if b"Builddemo" in line)
            assert b"moderator" in row and b"administrator" not in row
            stop(owner)
            for file in Path(directory).iterdir():
                if file.name.startswith("accounts.sqlite3") or file.name == "log.txt":
                    assert PASSWORD.encode() not in file.read_bytes()
            print("In-game staff TCP passed: menus/roles, live/saved inspection, confirmation, "
                  "password proofs, moderation, audit, restart, revocation and disconnected actions.")
        finally:
            for client in clients:
                client.close()
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
