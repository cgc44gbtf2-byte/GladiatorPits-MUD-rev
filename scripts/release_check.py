# Modernization contributions: Copyright (c) 2026 LaWaylon Battles (aKa Yami).
"""Read-only, fail-closed source-distribution checks; never extracts archives."""

import argparse
import io
from pathlib import Path, PurePosixPath
import re
import sys
import tarfile

MAX_FILE = 1024 * 1024
MAX_TOTAL = 16 * 1024 * 1024
MAX_MEMBERS = 256
MANIFEST = "RELEASE_FILES.txt"
PATTERNS = [
    ("private key", re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----")),
    ("GitHub token", re.compile(rb"\bgh[pousr]_[A-Za-z0-9]{36,}\b")),
    ("GitHub fine-grained token", re.compile(rb"\bgithub_pat_[A-Za-z0-9_]{40,}\b")),
    ("AWS access key ID", re.compile(rb"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b")),
]


class Invalid(ValueError):
    pass


def safe_name(name):
    if (not re.fullmatch(r"[A-Za-z0-9_./-]+", name) or
            PurePosixPath(name).is_absolute() or
            any(part in (".", "..") for part in name.split("/")) or
            "//" in name or name.endswith("/")):
        raise Invalid("Unsafe source path")
    return name


def validate(files):
    if MANIFEST not in files:
        raise Invalid("Missing release manifest")
    try:
        entries = files[MANIFEST].decode("ascii").splitlines()
    except UnicodeError as error:
        raise Invalid("Manifest must be ASCII") from error
    if not entries or len(entries) != len(set(entries)) or entries != sorted(entries):
        raise Invalid("Manifest must be nonempty, unique and sorted")
    for name in entries:
        safe_name(name)
    if set(entries) != set(files):
        raise Invalid("File set differs from release manifest")
    for name, content in files.items():
        safe_name(name)
        parts = PurePosixPath(name).parts
        if (any(p in (".git", "__pycache__", ".test-run", ".account-test-run",
                      ".storage-test-run", "node_modules") for p in parts) or
                Path(name).suffix.lower() in
                (".so", ".dll", ".exe", ".o", ".pyc", ".db", ".sqlite3", ".pem", ".key") or
                Path(name).name.startswith((".env", "accounts.sqlite3", ".glad-"))):
            raise Invalid("Forbidden release artifact: " + name)
        if len(content) > MAX_FILE or b"\0" in content:
            raise Invalid("Oversized or binary release member: " + name)
        try:
            content.decode("utf-8")
        except UnicodeError as error:
            raise Invalid("Non-UTF8 release member: " + name) from error
        for category, pattern in PATTERNS:
            match = pattern.search(content)
            if match:
                line = content.count(b"\n", 0, match.start()) + 1
                # Never include the matched credential or surrounding source.
                raise Invalid(f"Credential-shaped {category}: {name}:{line}")
        if (Path(name).suffix in (".c", ".h", ".py", ".sh", ".cfg") or
                name in ("LICENSE", "Makefile", ".github/workflows/native-ci.yml")):
            if b"Copyright (c) 2026 LaWaylon Battles (aKa Yami)" not in content:
                raise Invalid("Missing modernization copyright: " + name)
    required = {
        "LICENSE": b"Copyright (c) 2000-2021 Richard Woolcock",
        "glad.txt": b"don't remove the copyright notices",
        "glad.c": b"This code is copyright (C) 2000 by Richard Woolcock.",
        "commands.c": b"This code is copyright (C) 2000 by Richard Woolcock.",
        "player_record.c": b"Copyright (C) 2000",
        "player_record.h": b"Copyright (C) 2000 Richard Woolcock.",
        "player_storage.c": b"Copyright (C) 2000 Richard Woolcock",
    }
    for name, marker in required.items():
        if marker not in files.get(name, b""):
            raise Invalid("Missing retained attribution: " + name)
    if sum(map(len, files.values())) > MAX_TOTAL:
        raise Invalid("Release exceeds bounded source size")
    return len(files)


def directory_files(root, allow_git=False):
    root = Path(root)
    if root.is_symlink() or not root.is_dir():
        raise Invalid("Source root must be a real directory")
    files, total = {}, 0
    for path in root.rglob("*"):
        relative = path.relative_to(root)
        if allow_git and relative.parts[0] == ".git":
            continue
        name = relative.as_posix()
        if path.is_symlink():
            raise Invalid("Symlink in source directory")
        if path.is_dir():
            continue
        if not path.is_file() or path.stat().st_size > MAX_FILE:
            raise Invalid("Special/oversized source member")
        safe_name(name)
        files[name] = path.read_bytes()
        total += len(files[name])
        if total > MAX_TOTAL or len(files) > MAX_MEMBERS:
            raise Invalid("Source directory exceeds bounded size")
    return files


def archive_files(data):
    files, total, prefix, seen = {}, 0, None, set()
    try:
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:*") as archive:
            for member in archive:
                name = member.name.rstrip("/") if member.isdir() else member.name
                safe_name(name)
                if name in seen:
                    raise Invalid("Duplicate archive member")
                seen.add(name)
                if len(seen) > MAX_MEMBERS:
                    raise Invalid("Too many archive members")
                parts = name.split("/")
                if prefix is None:
                    prefix = parts[0]
                if parts[0] != prefix:
                    raise Invalid("Archive must have exactly one root directory")
                if member.isdir():
                    continue
                if len(parts) < 2 or not member.isfile() or member.size > MAX_FILE:
                    raise Invalid("Archive has unsafe type/size/root")
                if member.mode & 0o022:
                    raise Invalid("Group/world-writable archive file")
                total += member.size
                if total > MAX_TOTAL:
                    raise Invalid("Archive exceeds bounded source size")
                stream = archive.extractfile(member)
                if stream is None:
                    raise Invalid("Unreadable archive member")
                files["/".join(parts[1:])] = stream.read(MAX_FILE + 1)
    except (tarfile.TarError, EOFError) as error:
        raise Invalid("Malformed source archive") from error
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", default=".")
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--allow-git-metadata", action="store_true")
    args = parser.parse_args()
    try:
        if args.archive:
            if args.archive.stat().st_size > MAX_TOTAL:
                raise Invalid("Compressed archive exceeds bounded size")
            files = archive_files(args.archive.read_bytes())
        else:
            files = directory_files(args.root, args.allow_git_metadata)
        count = validate(files)
    except (Invalid, OSError) as error:
        # OSError may include arbitrary paths: report its type, not its contents.
        detail = str(error) if isinstance(error, Invalid) else type(error).__name__
        print("Release check rejected: " + detail, file=sys.stderr)
        return 1
    print(f"Release source check passed: {count} manifest files; text/attribution/pattern checks.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
