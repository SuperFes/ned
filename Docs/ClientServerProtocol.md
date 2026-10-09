# Client/Server Protocol

The wire protocol between the editor and a **host**: the machine whose files, processes
and toolchain a project lives on. Its consumer is the remote agent (`ned-agent`), a
headless build over `ned_host` started with `ssh host ned-agent --stdio`. Local work never
goes through it: the editor calls the same functions the agent's handlers run
(`Text/FileContents.h`, `Text/SavePlan.h`, `Process/ChildProcess.h`, ...) directly, and the
protocol is exercised by tests against the real agent.

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
optimization: it keeps a cooperating host's frames moving at the pace the editor consumes
them. A host that ignores it meets `FramedConnection`'s read-ahead cap instead (64 MiB of
frames posted and not yet handled): the reader stops, and the host's writes block on its
own pipe. The cap pauses, never disconnects. Measured against a gigabit link with the main
thread stalling a second at a time, it stays within 1% of unbounded read-ahead.

**Idle limits.** A request may carry an idle limit: once that long passes with nothing heard
for it (its sending, or its last chunk), the editor fails it locally with `TimedOut` and
cancels it on the host, dropping the late answer. `fs` requests use the request timeout
(`ProcessTimeouts.h`), a write plus the time its content takes up a 256 KiB/s link, and a
5 s editor tick expires them. A search carries none, since a large tree can be quiet for
long, and the user can cancel it.

**Cancellation.** `$/cancel {id}` has LSP's `$/cancelRequest` semantics. The request
still gets exactly one response, either an error with code `Cancelled` or a partial
result. A stream stops sending chunks once it sees the cancel.

## 4. Session

The first exchange on every connection, initiated by the editor:

```
hello  -> {protocol: {major, minor}, codecs: [str], window: uint}
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

  `Remote/Deploy.h` does it only when the agent session (section 5) finds none of the
  editor's builds installed, in two more sessions. A probe, fed to `sh -s` so a non-POSIX
  login shell never parses it, prints the architecture, the agent root and the installed
  `<build>-<arch>` directories after a marker line that skips anything a login script
  printed. When the local agent's build isn't among them, it goes up on the stdin of one
  `sh -c` session, under a temporary name moved into place, so an interrupted upload never
  leaves a partial agent. Starting an agent touches its directory, and an upload removes
  the other `<build>-<arch>` directories nothing has started from in 30 days; an editor
  that still wants one uploads it again. Local agents come from `libexec/ned/agent/<arch>/`
  (`$NED_AGENT_DIR` overrides): `cmake --install build-agent-<arch> --component agent
  --prefix <prefix>` puts a static build there, the editor's build tree included.
- `Remote/Connection.h` puts it together: master, an agent session (deploying when it finds
  none), then `hello` sent as request id 1 before an `Endpoint` takes over the connection
  (and numbers past it). `Connect` blocks, so a `Connector` runs it on a worker that a stop
  cancels at any step, and hands a `Connection` to the main thread.
- `Remote/Hosts.h` keeps one `Connection` per destination for the editor, sharing an
  attempt between callers. `remote-connect`, `remote-disconnect` and
  `remote-project-search` (`host:/path`, results streamed into `*search results*`) run
  through it. Their host prompts offer registered projects on hosts first, by name, then
  `Remote/KnownHosts.h`'s names (ssh_config and known_hosts), plus what the typed name
  resolves to under resolv.conf's search domains (`Remote/HostLookup.h`, through a
  killable `getent ahosts`). Each row says where ssh_config takes its host
  (`SshConfig`, evaluated as ssh does: Host and Match blocks, Includes in place, first
  value wins). A destination on a port other than 22 is an ssh URI,
  `ssh://[user@]host:port`, and a file there is `ssh://[user@]host:port/path`, as scp
  writes one; a known_hosts entry on another port is offered that way, unless a config
  host already leads there. A project registry root may be a `host:/path`
  (`open-project`), and switching to it opens it in the Remote panel.
- A remote buffer (`Text/RemoteFile.h`) holds a host's file and has no local path, so
  nothing that reads or writes a buffer's local file (the watcher, LSP, VCS, backups,
  autosave, revert) touches a same-named file here. `find-file host:/path` opens one
  through `fs/read` (`Remote/RemoteBuffer.h`); its mode, indentation and persisted undo
  history come from the host path and its content, never a local `.editorconfig`.
  `save-buffer` encodes it with the local save policy and writes it through `fs/write`
  (`RemoteSaver`), expecting the mtime it last saw; a file changed on the host is
  refused, and the next save asks before overwriting it. `next-error` and RET on a remote
  search's `host:/path:line:` results open the host's file at that line.
- `Remote/Watch.h` tells each connected host what to watch: the remote buffers' files and
  the Remote panel's expanded directories, resent after a reconnect. A buffer whose file
  an event names is stat'd, and read only if its timestamp moved on from the buffer's,
  so its own saves stay quiet. Then `Remote/ExternalChanges.h` reverts it when unmodified,
  merges into its edits otherwise, or, with `ned/set-auto-revert` or
  `ned/set-auto-merge` off, marks it so the next save asks first. The panel relists a
  directory whose entries changed, and every directory it shows on a host whose burst
  says it lost events.

## 5. Transports

One transport:

- **stdio over ssh.** `ssh [ControlMaster opts] host <agent> --stdio`, with the wire
  format from section 2 over the child's stdin/stdout. The child's stderr goes to the
  diagnostics log, the way `FramedConnection` already drains a server's stderr. It is
  built on `FramedConnection` with a length-prefixed transport type beside
  `LineTransport`.

  `Remote/Ssh.h` owns one foreground `ssh -M -N` master per host, with its control socket
  at `$XDG_RUNTIME_DIR/ned/ssh/<pid>-<n>`. The socket appears only once login succeeds,
  which is how readiness is detected; a master that exits first reports ssh's own stderr.
  The agent and deploy commands run as sessions through it, so only the first connection
  pays for login (about 1 s to the test host, against ~0.4 s, or five round trips, per
  later session). Stopping the master with `-O exit` ends every session on it, and an
  editor starting up sends that to masters whose `<pid>` is gone. Every
  invocation passes `RemoteCommand=none`, `RequestTTY=no`, `ClearAllForwardings=yes`
  and keepalives, and `ControlPersist=no` keeps a user's persisting config from
  backgrounding the master out of the editor's ownership. Sessions also pass
  `BatchMode=yes`. The master instead passes `BatchMode=no` and answers ssh's questions
  (a password, a passphrase, a one-time code, an unknown host key) through
  `SSH_ASKPASS_REQUIRE=force`. Its `SSH_ASKPASS` is ned itself, which relays each
  question over a socket beside the control socket to the editor's minibuffer.

  The agent session is one `sh -c` script given every local `<build>-<arch>`. It picks the
  one matching `uname -m` that is installed, prints `ned-agent-stdio-1 <path>` and `exec`s
  it, so a host that already has the agent costs one session; otherwise it prints
  `ned-agent-missing-1`, and the editor deploys and runs the script again. A login script
  can print ahead of any command (bash reads `~/.bashrc` for one run over ssh), so the
  editor skips stdout through the marker before `hello`. The agent says nothing until
  `hello`, so its frames start clean.

No TCP and no listening socket on either side.

## 6. Trust

- **Authentication and transport security belong to ssh.** That means the user's own
  config, agent and `known_hosts`. An unknown host key is ssh's own question, asked in
  the minibuffer, and accepting it writes `known_hosts` just as answering it in a
  terminal would. The agent opens no socket, so running it adds no attack
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
| `fs` | `stat`, `read` (streamed), `write` (atomic rename), `readdir`, `rename`, `remove`, `mkdir`, `realpath`, `project-root` | `Host/FsCodec.h` has the wire forms; all of them exist. `mkdir`, `rename` and `remove` run `Project/FileOps.h`, as the local project tree does: `mkdir` makes missing parents, `rename` refuses an existing destination and doesn't cross filesystems, and `remove` takes a directory with everything in it. `readdir` streams entries (name, kind through a symlink, whether it is one) in chunks of 1024; `realpath` takes a relative path against the agent's working directory, the login's home. `project-root` runs `Project/Root.h`'s `DetectProjectRoot` on the host, the VCS walk only when the editor's own setting asks for it, so a remote buffer's processes start where a local one's would. A request may name a language's root `markers` (`Project/Root.h`'s `FindMarkerRoot`), looked for first when detecting, so a remote buffer's language server starts in the same root a local one's would (from `fs` version 5). A read runs `Text/FileContents.h`'s `ReadFileContents` and streams 256 KiB chunks; a write runs `Text/SavePlan.h`'s `ReplaceFileContent` (temp file, fsync, rename, symlinks followed, mode and xattrs kept), so a failed write leaves the original intact. A write's `expect` carries the mtime the editor read, and a file changed since is refused. Its content travels in the request, under the frame cap, until a streamed upload is needed. |
| `proc` | `spawn` (streamed output, exit as its result), `write`, `close-stdin`, `signal`; `resize` for a pty | `Host/ProcCodec.h` has the wire forms. Byte streams only: LSP's Content-Length framing and ACP's line framing are parsed on the editor side over the relayed bytes. See "Plugging processes in" below. |
| `search` | `project` (streamed results) | Runs the agent's own in-process `Project/SearchEngine.h`, so results match exactly, including gitignore, binary sniffing and RE2. The request carries the editor's search settings (scan threads, `.editorconfig` charsets), and matches stream in walk order as files finish, coalesced into fuller chunks while the editor's credit is spent. Unsaved edits travel with the request as `LiveContent`: modified buffers under the root only, a huge one as its editor-side matches (`preScanned`), and each canonical path from `fs/realpath` rather than a local syscall. |
| `watch` | `set`; `$/watch/event` notifications | `Host/WatchCodec.h` has the wire forms. `set` replaces everything the connection watches, so the editor resends the whole set when it changes and after a reconnect, never a diff the two sides could disagree about. A file is watched by its name in its directory, a directory by every entry directly in it, both through `FileWatch.h` on the host. Each debounced burst is one notification naming the paths as `set` did, symlinks and all, with `overflow` when inotify lost events. |

VCS, tasks, DAP adapters and language servers have no namespaces of their own. They are
processes, so they run through `proc` against the host's toolchain. That includes remote
debugging: a PHP/Xdebug adapter runs on the remote host, so Xdebug's connect-back stays
local to that machine and no port forwarding is needed.

### Plugging processes in

`FramedConnection<TransportT>`, every Transport, `StreamedChild` and `TaskProcess` already
take an already-open `ChildProcess`, or the descriptors one wraps. So a host's process is
handed to them as one: `Remote/Proc.h` spawns it on the host and returns a `ChildProcess`
over local socketpairs, with a relay moving bytes between those and the host's streams.
The framing layers, and everything above them, can't tell it from a local child.

Only the half of `ChildProcess` that owns a process differs: waiting, signalling and the
destructor's hang-up. A relayed child holds a `process::ProcessControl` for that half
instead of a pidfd. That one is an interface rather than a variant because `ned_core`,
where `ChildProcess` lives, can't name the editor's remote types; it is called a handful
of times per process, never per byte. Its exit arrives through the main thread, so a
relayed child's `Kill()` and destructor send the signal and return rather than waiting
for the reap, and it is never waited for on the main thread.

Consumers get a process from one place, `Remote/Placement.h`'s `SpawnBeside`, so a
different seam later means changing that place and `ChildProcess`, not the call sites.

**Where a process runs.** A process that works on a buffer or its project runs where
the buffer's file is: on its host for a remote buffer (`Text/RemoteFile.h`), connecting
first if need be, in the project root `fs/project-root` finds there unless the caller
names a directory. A task re-run from its output buffer runs where it last ran. The clipboard, desktop helpers, URL opening, the LSP
broker and ssh itself are always local.

**A remote buffer's repository.** VCS commands for a remote buffer run on its host, in
the repository `fs/project-root` walks up to (always walking for a VCS marker, whatever
the project-root setting, since it is the repository that matters). A provider's
`Detect` runs here, so the provider comes from the markers it names instead (`:markers`
for a Janet one), looked for with `fs/stat` in registration order and cached per
repository; one without markers is never used for a remote repository. A hunk's patch
reaches the host as `-` on stdin when the provider takes it there (`:patches-on-stdin`).
Repository-wide commands and the VCS panel work in the active buffer's repository
(`Vcs/Runner.h`'s `RepoFor`); one with no file of its own, a `*vcs*` buffer or the
commit message, works in the last one found.

**Terminals.** A terminal tab opens beside the buffer focused when it is first shown, so
a remote buffer's opens on its host, in the project root found there, as a login shell
(`$SHELL -l`, through `/bin/sh` to find it). A restart reconnects to the same host. The
tab's label names the host, and one that can't start shows why in place of its output.
Reveal in Terminal from the project sidebar takes the first terminal here. From the Remote
panel (`t`) it `cd`s a running shell on that row's host, or else opens a tab whose shell
starts in that directory.

**Language servers.** A remote buffer's language server runs on its host, with the same
command as a local one, outside the broker. It starts in the root its language's markers
name there, found by `fs/project-root`, and only on a host already connected: syncing a
buffer never opens a connection. It is sent no `processId`, since it can't see this
process. Its URIs are host paths, so what it names comes back as a file on that host
(`Text/RemoteFile.h`'s `FileRef`): a jump opens it there, references list `host:/path`
lines, and an edit opens the host's files it touches before changing them. A server-sent
`workspace/applyEdit` edits only those already open, and file creates, renames and
deletes on a host are refused. Local file operations and watched-file changes reach only
local servers.

**Debug adapters.** Starting or attaching from a remote buffer runs its language's adapter
on that buffer's host, in the project root found there, connecting first if need be.
The launch or attach configuration goes as configured, so its paths must make sense on the
host. Breakpoints key a host's file as `host:/path`; a session is told only about those on
its own machine, by their paths there, and what it reports (where it stopped, frames,
loaded sources, disassembly) comes back as files on that host. Run-to-cursor and
jump-to-line refuse a file on another machine.

**Agents (ACP).** An agent started while a remote buffer is focused (the agent prompt,
or the panel reconnecting) runs on that buffer's host, with the same command as a local
one, in the project root found there; every conversation on that connection works there.
Its `fs/read_text_file` reads an open buffer's live text, else the host's file, and
`fs/write_text_file` writes the host's file, which an open buffer then takes up. The paths
it names are that host's: following it and its tool locations open the files there, and a
conversation keeps its host after the agent has gone. A turn's review and rewind cover the
host's files that were open here, with the "after" read from the host as the turn ends,
and a rewind edits the open buffer, leaving it to be saved. It is offered none of ned's
MCP tools, since the bridge is a local socket, and no additional directories. In the
composer, `@buffer` names a host file by its path there for an agent on that host, and
`@` completion offers the host's files open here.

**Main-thread waits.** A remote process can't be waited on from the main thread, which
is what moves its bytes. A call site that blocks there on a child (the toolchain probes)
becomes asynchronous, or moves to a worker, before it can run remotely. One that hands a
child a local temp file's path passes the content on stdin instead, written from a thread
of its own (`StreamedChild::Feed`), since that input moves through the main thread too. A
terminal's typing is queued the same way (`StreamedChild::Send`).
The format command runs this way for a remote buffer (`Editor/FormatRun.h`):
save-buffer and format-buffer hand the run to BufferView, which applies the output only
if the buffer hasn't changed since, and then saves. A local buffer's still runs
synchronously.

```
proc/spawn   params {handle: uint, argv: [bytes], cwd: path | null,
                     env: {name: bytes | null}, stderr: "discard" | "merge" | "capture",
                     pty: {rows: uint, cols: uint} | null}
             chunk  {fd: 1 | 2, data: bytes}
             result {exit: int | null}       null: killed by a signal
proc/write   params {handle, data: bytes}    answered once written
proc/close-stdin params {handle}             after every write before it
proc/signal  params {handle, signal: "kill" | "term" | "int" | "hup"}
proc/resize  params {handle, rows, cols}     a pty's new size
proc/release params {handle}                 stdin closes, or a pty hangs up
```

A child on a pty (`Process/Pty.h`) has the terminal for stdout and stderr both, so its
`stderr` is `"merge"`; the editor sets `TERM` in `env`, since the emulator is its own.
An agent offers `pty` and `resize` from `proc` capability 2.

The editor numbers `handle`, so it can write before the spawn answers anything.
Cancelling `proc/spawn` kills the process group, as a dropped connection does. Output
is paced by credit, granted as the relay hands each chunk on rather than as it arrives,
so a consumer that stops reading stops the host's child rather than filling editor
memory; stdin is paced by one `write` in flight per process.

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
3. **`fs`.** A load splits into reading (`Text/FileContents.h`) and decoding
   (`Buffer::FromContents`), a save into encoding and replacing the file
   (`Text/SavePlan.h`), and the agent's `fs` handlers run the same functions a local load
   and save do. Local files call them directly: load and revert are synchronous in many
   places, which an in-process host would keep only by blocking its dispatcher.
4. **`proc`.** In order, each landing working:
   1. `ChildProcess` takes a working directory and environment, sends any signal, and
      can be backed by a `ProcessControl`.
   2. Credit the receiver grants by hand, for a stream relayed onward at its consumer's
      pace.
   3. The agent's `proc` handlers.
   4. `Remote/Proc.h`: a host's process as a relayed `ChildProcess`.
   5. Tasks and the test runner for a remote buffer.
   6. VCS against a remote buffer's repository, patches on stdin.
   7. The formatter as a `ChildProcess` over stdin and stdout, then on its host.
   8. A pty variant (`resize`) for terminal tabs.
   9. LSP for remote buffers, outside the broker; then DAP and ACP.
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
