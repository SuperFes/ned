# Client/Server Protocol

The wire protocol between the editor and a **host**: the machine whose files, processes
and toolchain a project lives on. Its consumer is the remote agent (`ned-agent`), a
headless build over `ned_host` started with `ssh host ned-agent --stdio`. Local execution
is the degenerate case. The local host is the same server-side dispatcher reached through
an in-process transport, not a separate code path around it.

The editor is always authoritative for buffers: edit locally, send operations to the
host, sync on save. There is no remote-authoritative buffer and no attach mode (see
[Rejected](#rejected)).

## 1. Layers

| Layer | Owns | Lives in |
|---|---|---|
| Wire | frame boundaries, size cap, per-frame compression | `ned::editor::protocol` |
| Message | envelope, request/response correlation, streams, credit, cancellation | `ned::editor::protocol` |
| Session | `hello` handshake, versioning, capability set, agent deploy | `ned::editor::host` |
| Host API | `fs`, `proc`, `search`, `watch` methods | `ned::editor::host` |

The wire and message layers carry no ned semantics, so they sit beside
`FramedConnection.h` and `LineTransport.h`. Everything that knows what a host is goes in
a new `Editor/Host/` directory.

## 2. Wire

```
+----------------+---------+------------------------+
| length: u32 BE | flags:u8| payload (length bytes) |
+----------------+---------+------------------------+
flags bit 0: payload is zstd-compressed
flags bits 1-7: reserved, must be zero (a set bit is a protocol error)
```

- **Length cap.** The reader rejects a length over 64 MiB *before* allocating. Anything
  bigger, such as a large file read, travels as a stream of chunks (section 3). A
  rejected frame closes the connection, because there is no resynchronizing inside a
  length-prefixed stream.
- **Codec: CBOR** (RFC 8949) via nlohmann's `to_cbor`/`from_cbor`. In code it's the same
  `Json` type every other protocol here uses. Byte strings are native (`json::binary`),
  so file contents and process output cross the wire as the bytes they are, with no
  base64 or UTF-8 assumption. There is no JSON-on-the-wire mode. `ned --protocol-dump`
  decodes a captured byte stream to readable JSON for debugging.
- **Compression: zstd per frame**, negotiated in `hello` with `none` as a first-class
  answer. Even when zstd is negotiated, a payload under 1 KiB is sent uncompressed, and
  the per-frame flag makes that free. Over ssh, don't also enable `ssh -C`.
- **Timeouts.** Every blocking read and write carries a stall timeout from
  `ProcessTimeouts.h`, the same policy `LineTransport` uses: silence between frames is
  normal, silence mid-frame is a stall.

## 3. Messages

The envelope is the LSP shape, so the correlation logic each client already has carries
over:

```
request       {id: uint, method: str, params: any}
response      {id: uint, result: any} | {id: uint, error: {code: int, message: str, data?: any}}
notification  {method: str, params: any}
```

Ids are per-direction and never reused within a connection.

**Streams.** Any request may answer with a stream before its final response:

```
$/chunk   {id, seq: uint, data: any}     host -> editor (or editor -> host for uploads)
$/credit  {id, n: uint}                  receiver -> sender
response  {id, result}                   ends the stream
```

A stream starts with a credit window from `hello` (default 16 chunks). The sender stops at
zero credit, and the receiver grants more as it consumes. Credit is required, not an
optimization. `FramedConnection`'s reader posts every frame to the `EventLoop`, whose queue
is unbounded, so a fast host (a big search, clangd's stdout) would otherwise grow editor
memory without limit over a link that is itself keeping up fine.

**Cancellation.** `$/cancel {id}` has LSP's `$/cancelRequest` semantics. The request
still gets exactly one response, either an error with code `Cancelled` or a partial
result. A stream stops sending chunks once it sees the cancel.

## 4. Session

The first exchange on every connection, initiated by the editor:

```
hello  -> {protocol: {major, minor}, build: str, codecs: [str], window: uint}
       <- {protocol: {major, minor}, build: str, arch: str, codec: str,
           window: uint, capabilities: {fs: uint, proc: uint, search: uint, watch: uint}}
```

- A **major** mismatch is fatal. **Minor** is additive: a newer side doesn't send methods
  the older side's capability versions don't list.
- `build` is the agent binary's content hash. If it differs from the hash of the agent the
  editor would deploy for that `arch`, the editor redeploys and reconnects. A mismatch is
  never something to work around.
- **Deploy.** Agents go under the remote's `$XDG_DATA_HOME/ned/agent/<build>-<arch>/` with
  mode 0700. They are built per architecture, cross-compiled or prebuilt, because the
  remote may have no toolchain. Copying uses the same `ssh`/`sftp` transport
  (ControlMaster), so a deploy has no other dependencies.
- The in-process transport skips `hello`. Both ends are the same build.

## 5. Transports

Two transports, a closed set known at compile time, held as a `std::variant` in the host
connection rather than behind a virtual interface:

- **In-process.** It moves `Json` values between the client stub and the dispatcher on
  the same `EventLoop`. There is no encoding, no compression and no threads, so local
  overhead is one `Json` construction per operation. The handler code, the
  stream/credit/cancel machinery and the client stub are exactly what a remote
  connection runs. That is the point: the whole test suite exercises the protocol
  continuously.
- **stdio over ssh.** `ssh [ControlMaster opts] host <agent> --stdio`, with the wire
  format from section 2 over the child's stdin/stdout. The child's stderr goes to the
  diagnostics log, the way `FramedConnection` already drains a server's stderr. It is
  built on `FramedConnection` with a length-prefixed transport type beside
  `LineTransport`.

No TCP and no listening socket on either side.

## 6. Trust

- **Authentication and transport security belong to ssh.** That means the user's own
  config, agent, `known_hosts`, and a first-connection host-key prompt following
  `Project/Trust.h`'s precedent. The agent opens no socket, so running it adds no attack
  surface on the remote host: anyone who can speak to it already has a shell there.
- **The agent only executes.** Every trust decision is made on the editor side, and the
  agent never decides whether to run something. A remote project's `.ned/` config and
  plugins go through the same `Project/Trust.h` prompt as a local one.
- **Everything from the host is untrusted input.** That covers file names (terminal
  escapes, path traversal in a returned path), sizes, and stream lengths. A compromised
  remote must not be able to escalate into the local editor: the length cap, bounds
  checks on every decoded field, and paths normalized under the requested root before
  they reach UI code.

## 7. Host API

Method names are `<namespace>/<verb>`. Paths are host-side absolute paths, carried as
CBOR byte strings because POSIX paths are bytes, not UTF-8.

| Namespace | Methods | Notes |
|---|---|---|
| `fs` | `stat`, `read` (streamed), `write` (streamed upload + atomic rename), `readdir`, `rename`, `remove`, `mkdir`, `realpath` | `write` mirrors `Text/SavePlan.h`: temp file, fsync, rename, mode preserved. A failed write leaves the original intact. |
| `proc` | `spawn` → handle; `stdin` (editor-to-host stream), `stdout`/`stderr` (streams), `signal`, `wait`; `pty` variant | Byte streams only. LSP's Content-Length framing and ACP's line framing are parsed on the editor side over the relayed bytes. |
| `search` | `project` (streamed results) | Runs the agent's own in-process `Project/SearchEngine.h`, so results match exactly, including gitignore, binary sniffing and RE2. Unsaved edits travel with the request as `LiveContent`: modified buffers under the root only, a huge one as its editor-side matches (`preScanned`), and each canonical path from `fs/realpath` rather than a local syscall. |
| `watch` | `add`, `remove`; `$/watch/event` notifications | Relays inotify on the host. The editor's `FileWatch.h` consumers receive the same events either way. |

VCS, tasks, DAP adapters and language servers have no namespaces of their own. They are
processes, so they run through `proc` against the host's toolchain. That includes remote
debugging: a PHP/Xdebug adapter runs on the remote host, so Xdebug's connect-back stays
local to that machine and no port forwarding is needed.

### Plugging processes in

`FramedConnection<TransportT>` and the 48 `ChildProcess`/`PtyProcess`/`StreamedChild`
users go through the host. The deciding seam is the byte source under each framing
layer. `Lsp/Transport.h` and `LineTransport.h` are built on `ChildProcess` today, and
need to be built on a host process handle that is either a local `ChildProcess` (through
the in-process transport) or a relayed stream. This is the largest piece of the design,
and the exact shape (a template parameter on the framing layer versus a single host
process type wrapping both) is settled by the `proc` slice in section 8, against the
real call sites, not here.

### Out of scope for the first slices

- **Huge files** (`Buffer::FromHugeFile`, mmap) stay local-only. A remote file over
  `HugeFileThreshold()` opens through the ordinary streamed path, or is refused with a
  message if it's too large to hold.
- **Client-only mode** (no agent: per-operation `ssh`/`sftp`) would be a different
  backend behind the same host API. It's deferred: a toolchain-free host may not have
  `rg` either, leaving `grep -r` as the only search.

## 8. Staging

Each step is unit-tested on its own and lands working.

0. **Library split.** `ned_core` and `ned_host` carved out of `ned_lib` so the agent
   links no Notcurses, Janet or grammar engine. The agent must be a static (musl)
   executable: a remote with an older glibc can't run a locally built dynamic binary.
1. **Wire + codec.** Frame reader/writer, length cap, reserved-flag rejection, CBOR
   round-trip of binary payloads, zstd negotiation. Tested over a pipe pair.
2. **Messages.** Correlation, streams, credit, cancellation, under a fake peer: a flood
   that never exceeds the window, cancel mid-stream, exactly one response per request.
3. **In-process transport + `fs`.** `Buffer::FromFile` and the save path go through the
   local host. No user-visible change, and the existing load/save tests pass unchanged.
4. **`proc`.** Settle the process-seam shape from section 7, then move the framing layers
   onto it, one consumer family at a time (LSP, DAP, ACP, tasks, VCS, terminal).
5. **ssh transport + `hello` + deploy.** `ned-agent` executable, version handshake,
   host-key prompt, connection-drop failure handling.
6. **`search`, `watch`.** Sidebar listing, project search, external-modification and
   auto-revert against a remote root.

## Rejected

- **Daemon/attach mode** (`emacsclient`-style). The LSP broker already keeps language
  servers warm across `ned` processes, and sessions restore buffers. A process that owns
  several terminals would also need several Notcurses contexts in one process, a cost for
  something nothing needs.
- **TCP transport.** It brings no authentication or encryption, and ssh already provides
  both.
- **Port forwarding in the protocol.** Remote tools run remotely through `proc`.
  Forwarding a dev server's port is `ssh -L`'s job.
- **JSON on the wire.** It can't carry raw bytes without base64. `--protocol-dump` covers
  readability.
