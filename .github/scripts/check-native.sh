#!/usr/bin/env bash
# Shared CI/local entry point; never runs against a live game data directory.
set -euo pipefail

readonly compiler="${CC:-}"
readonly mode="${BUILD_MODE:-}"
case "$compiler" in
  gcc|clang) ;;
  *) printf '%s\n' 'Set CC to gcc or clang.' >&2; exit 2 ;;
esac
case "$mode" in
   # Deliberately omit -std: the Makefile must preserve its required dialect.
   optimized) flags='-O2' ;;
  sanitizers)
     flags='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer'
    ;;
  *) printf '%s\n' 'Set BUILD_MODE to optimized or sanitizers.' >&2; exit 2 ;;
esac
readonly flags
readonly warnings='-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Werror'

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." || exit 1
export ASAN_OPTIONS='detect_leaks=1:halt_on_error=1'
export UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'

"$compiler" --version
python3 --version
pkg-config --modversion libsodium sqlite3
make --jobs=1 clean
make --jobs=1 CC="$compiler" CFLAGS="$flags" WARNINGS="$warnings" all check
