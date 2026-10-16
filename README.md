# Gladiator Pits

[![Build status](https://ci.appveyor.com/api/projects/status/e4l064rgu0fe5hax?svg=true)](https://ci.appveyor.com/project/servusDei2018/gladiatorpits-mud)

(A Go rewrite is available [here](https://github.com/servusDei2018/GladiatorPits-Go).)

### Building

This maintains the [original](https://github.com/servusdei2018/GladiatorPits-MUD/tree/original) C game rather than rewriting it. The modernization pass preserves its stat formulas, training rules, PvP combat, progression, and native binary player record layout while improving I/O safety, persistence, and portability. See [MODERNIZATION.md](MODERNIZATION.md) for the audit, phased plan, change/failure log, and remaining issues.

Build on a POSIX system with a C99 compiler and Make:

```sh
make
```

The default TCP port remains 3000. A different port can be selected at build time, for example with `make CPPFLAGS=-DPORT=4000`.

### Running and player data

Run the executable from a private data directory: player records and `log.txt` are stored in its working directory. Back up existing player files before testing any new build. No migration or reset is performed; records from incompatible architectures or damaged files are rejected without modification.

The `shutdown` command is retained, but its former public source-code credential is no longer used. Configure `GLAD_SHUTDOWN_PASSWORD` through your runtime environment or secret store; do not commit it. If it is unset or empty, remote shutdown is denied.

This is still the original passwordless character-loading model over plain TCP. Do not treat this pass as production authentication or transport security. The historical `glad.txt` is retained unchanged, but its old shutdown credential instructions are superseded by this README.

### Tests

The game has no third-party runtime packages. The TCP smoke test needs Python 3's standard library:

```sh
make check       # 100 core regression checks plus a real TCP smoke test
make sanitize    # repeat both suites with AddressSanitizer and UndefinedBehaviorSanitizer
make clean
```

Tests use disposable directories and an ephemeral TCP port, not live player records. GCC 15 and Clang 20 have been verified; use `make clean` before switching compiler or build flags.

### History

Gladiator Pits is a [MUD](https://en.wikipedia.org/wiki/MUD), written by Richard Woolcock, who wrote the now-famous [GodWars](https://en.wikipedia.org/wiki/GodWars) codebase.

Popular in the 20th century, MUDs are text-baased Multi-player games, similar to our modern day MMOs.

Gladiator Pits, of which this source code is the only public version (to my knowledge) still available, is a excellent example of a MUD, clearly commented, with precision and intuitivity.

### License

Richard Woolcock wrote in `glad.txt`, <blockquote>This code is copyrighted to me, however you're welcome to use it for anything 
you wish, so long as you don't remove the copyright notices from any source 
files containing any of the mud source code.</blockquote>

And therefore, the most applicable license appears to be the MIT license.

### Disclaimer

I in no way claim authorship, nor authority, over this excellent piece of code,
but release it, under its author's original terms and conditions, to the general
public.
