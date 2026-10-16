# Modernization contributions: Copyright (c) 2026 LaWaylon Battles (aKa Yami).
"""Isolated negative fixtures for the source release checker."""
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("release_check", ROOT / "scripts/release_check.py")
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


def fixture():
    names = (ROOT / CHECK.MANIFEST).read_text().splitlines()
    return {name: (ROOT / name).read_bytes() for name in names}


def pack(files, extra=None):
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:gz") as archive:
        for name, data in files.items():
            item = tarfile.TarInfo("candidate/" + name)
            item.size, item.mode = len(data), 0o644
            archive.addfile(item, io.BytesIO(data))
        if extra:
            archive.addfile(extra[0], extra[1])
    return buffer.getvalue()


class ReleaseChecks(unittest.TestCase):
    def reject(self, function, *args):
        with self.assertRaises(CHECK.Invalid):
            function(*args)

    def test_current_manifest(self):
        self.assertEqual(CHECK.validate(fixture()), len(fixture()))

    def test_safe_archive(self):
        self.assertEqual(CHECK.archive_files(pack(fixture())), fixture())

    def test_unlisted_data(self):
        files = fixture()
        files["accounts.sqlite3"] = b"SQLite format 3\0"
        self.reject(CHECK.validate, files)

    def test_listed_data_still_denied(self):
        files = fixture()
        files["accounts.sqlite3"] = b"not really a database"
        files[CHECK.MANIFEST] = "\n".join(sorted(files)).encode() + b"\n"
        self.reject(CHECK.validate, files)

    def test_missing_member(self):
        files = fixture()
        del files["LICENSE"]
        self.reject(CHECK.validate, files)

    def test_duplicate_manifest(self):
        files = fixture()
        files[CHECK.MANIFEST] += b"README.md\n"
        self.reject(CHECK.validate, files)

    def test_unsafe_manifest(self):
        files = fixture()
        files[CHECK.MANIFEST] = b"../escape\n"
        self.reject(CHECK.validate, files)

    def test_binary(self):
        files = fixture()
        files["README.md"] = b"\x7fELF\0"
        self.reject(CHECK.validate, files)

    def test_private_key_redacted(self):
        files = fixture()
        secret = b"-----BEGIN " + b"PRIVATE KEY-----"
        files["README.md"] = secret
        with self.assertRaises(CHECK.Invalid) as result:
            CHECK.validate(files)
        self.assertNotIn(secret.decode(), str(result.exception))

    def test_token_redacted(self):
        files = fixture()
        secret = b"gh" + b"p_" + b"x" * 36
        files["README.md"] = secret
        with self.assertRaises(CHECK.Invalid) as result:
            CHECK.validate(files)
        self.assertNotIn(secret.decode(), str(result.exception))

    def test_copyright_required(self):
        files = fixture()
        files["commands.c"] = b"/* notice removed */"
        self.reject(CHECK.validate, files)

    def test_modernization_header_required(self):
        files = fixture()
        files["accounts.h"] = files["accounts.h"].replace(
            b"Copyright (c) 2026 LaWaylon Battles (aKa Yami)", b"removed")
        self.reject(CHECK.validate, files)

    def test_archive_traversal(self):
        item = tarfile.TarInfo("candidate/../escape")
        self.reject(CHECK.archive_files, pack(fixture(), (item, io.BytesIO())))

    def test_archive_duplicate(self):
        item = tarfile.TarInfo("candidate/README.md")
        self.reject(CHECK.archive_files, pack(fixture(), (item, io.BytesIO())))

    def test_archive_symlink(self):
        item = tarfile.TarInfo("candidate/link")
        item.type, item.linkname = tarfile.SYMTYPE, "/etc/passwd"
        self.reject(CHECK.archive_files, pack(fixture(), (item, None)))

    def test_archive_other_root(self):
        item = tarfile.TarInfo("elsewhere/file")
        self.reject(CHECK.archive_files, pack(fixture(), (item, io.BytesIO())))

    def test_archive_writable(self):
        item = tarfile.TarInfo("candidate/extra")
        item.mode = 0o666
        self.reject(CHECK.archive_files, pack(fixture(), (item, io.BytesIO())))

    def test_oversized_archive_member(self):
        item = tarfile.TarInfo("candidate/huge")
        item.size = CHECK.MAX_FILE + 1
        self.reject(CHECK.archive_files, pack(fixture(), (item, io.BytesIO(b"x" * item.size))))

    def test_directory_symlink(self):
        with tempfile.TemporaryDirectory(prefix="glad-release-negative-") as folder:
            path = Path(folder)
            (path / "link").symlink_to(ROOT / "README.md")
            self.reject(CHECK.directory_files, path)


if __name__ == "__main__":
    result = unittest.TextTestRunner(verbosity=1).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(ReleaseChecks))
    if not result.wasSuccessful():
        raise SystemExit(1)
    print(f"Release checker regression passed: {result.testsRun} tests.")
