# Trusted TLS gateway and client identity

The MUD remains a plain TCP backend, loopback by default. Keep encrypted transport
at a maintained TLS gateway rather than rewriting the game or weakening login
limits. Default/direct mode is unchanged when `GLAD_TRUSTED_PROXY` is unset.

## Optional strict PROXY v1 mode

Start the backend in its private data directory:

```sh
GLAD_BIND_ADDRESS=127.0.0.1 GLAD_TRUSTED_PROXY=127.0.0.1 /absolute/path/to/glad
```

`GLAD_TRUSTED_PROXY` must be one exact IPv4 address of the gateway's **backend
transport connection**, not a public player's address, hostname, CIDR or wildcard.
An empty/invalid value refuses startup. The IPv4 backend can still represent
IPv4 and IPv6 clients forwarded by that gateway.

When enabled, this entire listener requires:

1. The actual socket peer matches that trusted address.
2. A valid bounded PROXY v1 `TCP4` or `TCP6` header arrives within ten seconds.
3. Only then does the server send a welcome, consume Telnet/game input or permit
   authentication. Missing/invalid headers close without application output.

It is intentionally not a mixed direct/proxy listener. `UNKNOWN`, binary v2,
invalid addresses/ports, trailing fields, bare LF, control bytes and oversized
headers fail closed. Bytewise/fragmented and header-plus-payload reads are handled.
Later header-shaped commands never change identity. A ten-second deadline is
checked both during input and idle polling.

Full 128-bit logical addresses key the existing per-IP authentication budget.
IPv4-mapped IPv6 canonicalizes to the same bucket as its IPv4 address. The existing
five-attempt per-IP, sixty-attempt global per-minute limits and eight-job worker
queue remain enforced; this does not remove throttling or raise capacity.

## Gateway example

`examples/haproxy.cfg` is an illustrative HAProxy TCP/TLS configuration with
`send-proxy` (v1) to `127.0.0.1:3000` and TLS 1.2 minimum. Replace the certificate
path with an operator-managed certificate/key bundle for your actual hostname,
owned/readable only as appropriate for the service. Do not commit it or paste
private keys into chat. Validate the installed version/config before restarting:

```sh
haproxy -c -f /etc/haproxy/haproxy.cfg
```

The example's public port is 4000. Clients connect to that encrypted endpoint,
not directly to the backend. An optional IPv6 frontend line is included.
**Never enable `accept-proxy` on an Internet-facing frontend:** public clients must
not choose their forwarded identity. HAProxy should derive it from its accepted
socket. Chained gateways need a separately reviewed trust chain; not covered here.

PROXY protocol is an identity assertion, **not encryption or authentication**.
The trusted IP must not be shared with untrusted workloads. Loopback trusts local
processes; on multi-user/untrusted hosts isolate the gateway/backend with service
network namespaces and firewall policy. For a separate gateway host, restrict
backend ingress to that host on a protected network/tunnel. A trusted peer that
is compromised can forge identity, although passwords/global limits still apply.
Do not expose a raw public backend or use an unrestricted/wildcard trust list.

An ordinary TLS proxy without forwarded identity still collapses users into one
per-IP bucket. Enable this mode only with a gateway that sends exactly the supported
protocol; enabling it behind a non-PROXY gateway deliberately prevents logins.

## Verification and limits

`make check` now includes two additional isolated production-binary suites:

- Trusted-proxy TCP: actual trusted/untrusted transport peers, no early banner,
  split headers, malformed/unsupported/overlong headers, idle timeout, per-IP
  reconnect limits, distinct IPv4/IPv6 budgets, mapped aliases, post-admission
  spoof attempts and unchanged default/direct mode.
- TLS forwarding fixture: an ephemeral private certificate, normal CA/hostname
  verification, rejected wrong hostname/untrusted CA, a real TLS handshake,
  automatic socket-peer forwarding, two distinct clients sharing the same backend
  gateway, separate login budgets, staff menu and authorized shutdown.

Native tests exercise the parser byte-by-byte, input-time expiry, buffer cap,
full IPv6 keys and pending-admission command dispatch. The TLS fixture uses Python
stdlib asyncio/ssl and OpenSSL only to create a disposable test certificate; it
is **not** a production gateway implementation or a deployment recipe.

No actual HAProxy executable or Mudlet UI was available for this pass. The HAProxy
example still needs syntax/runtime validation with your installed version and
real certificate. Certificate issuance/renewal, production routing/firewall,
IPv6 frontend deployment and actual Mudlet echo/reconnect behavior are not
certified. The MUD itself does not terminate TLS, parse PROXY v2/TLVs or accept a
native IPv6 transport listener. No live service or public traffic was changed.

## Change and test failure log

Added optional trusted-peer admission, bounded framing/deadline state and full
logical-address rate keys; no account schema, native save fields, gameplay rules
or privileges changed. Pending peers cannot receive broadcasts or dispatch
commands. Connection teardown wipes the added buffers with the existing full
connection wipe.

Three initial fixture assertions were corrected: attempting login on an already
authenticated registration connection, expecting mixed-case rather than uppercase
staff menu text, and expecting a native-fixture banner without linking that fixture
into the connection list. They were test setup errors, not established runtime
failures. Corrected full suites were rerun; no failed run is counted as verification.
Direct-mode TCP/soak fixtures explicitly clear an inherited proxy setting and
force loopback, so testing from an operator shell cannot accidentally change
their listener's trust mode or bind them publicly.
The first Clang analyzer invocation rejected an unused linker option injected by
the local Nix compiler wrapper under `-Werror`. Rerunning with only the analyzer's
unused-command-line-argument warning suppressed passed, as did GCC `-fanalyzer`;
normal production/CI builds retain strict warnings-as-errors.
