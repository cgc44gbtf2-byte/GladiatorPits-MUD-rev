# Attribution and redistribution

## Gladiator Pits

Modernization contributions: Copyright (c) 2026 LaWaylon Battles (aKa Yami).

Original game: Richard Woolcock (KaVir), copyright 2000; the repository's inherited
MIT `LICENSE` identifies copyright 2000-2021 Richard Woolcock. Preserve that file,
`glad.txt` and all original source copyright/permission notices in distributions
and derivatives. Original `glad.txt` section 1.3 explicitly permits use, free
distribution and commercial use provided source copyright notices are retained.
Its request for game credits is appreciated but described there as not required.

Provenance checked during this pass:

- [Upstream MIT LICENSE](https://github.com/servusdei2018/GladiatorPits-MUD/blob/master/LICENSE)
  matches the retained original copyright and permission text. This repository
  additionally credits LaWaylon Battles (aKa Yami) for modernization contributions.
- [Original author documentation](https://github.com/servusdei2018/GladiatorPits-MUD/blob/original/glad.txt)
  contains the quoted original permission.

This pass retains both; it does not invent a new license or claim to speak for
the original author. This is a provenance/notice audit, not a legal opinion.
There are no vendored Mudlet assets, plugins or source files in this distribution.
Mudlet is an optional external client, not a bundled dependency.

## Runtime dependencies

The source package does not vendor or distribute library binaries. Operators
install libsodium, SQLite and their platform's C/POSIX thread runtime through
their OS package manager. Build/test tools include Make, pkg-config, a C99 compiler
and Python 3; Git is used to prepare a pinned source archive.
OpenSSL's command-line tool creates ephemeral test certificates only; HAProxy is
an optional separately installed gateway. Neither is bundled with this source.

### libsodium: ISC

The [upstream license](https://github.com/jedisct1/libsodium/blob/master/LICENSE)
provides:

> ISC License
>
> Copyright (c) 2013-2026 Frank Denis
>
> Permission to use, copy, modify, and/or distribute this software for any
> purpose with or without fee is hereby granted, provided that the above
> copyright notice and this permission notice appear in all copies.
>
> THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
> WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
> MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
> ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
> WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
> ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
> OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

For any future binary package, carry the exact notice supplied with the actual
libsodium version bundled, not merely the current upstream copyright range.

### SQLite: public domain

SQLite's [copyright statement](https://www.sqlite.org/copyright.html) dedicates
its deliverable library code/documentation to the public domain. It distinguishes
some build tools/scripts that may have other licenses and discusses jurisdictions
where public-domain dedication may not be recognized.

## Future binaries and derivatives

No blanket licensing clearance is claimed for a future binary bundle: audit its
actual linked/bundled libraries, C runtime, TLS gateway and assets before shipping.
Keep original game notices in source containing original/derived game code. New
derivative branding, assets and dependencies need their own provenance review.
