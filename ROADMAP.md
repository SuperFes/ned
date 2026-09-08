# Ned Roadmap

What's still open. Completed work is deliberately not tracked here — the detailed
per-feature design/decision records this file used to carry were pruned 2026-08-20,
2026-08-25, 2026-09-06, and again 2026-09-07; full history lives in git
(`git log --follow ROADMAP.md`, `git show <rev>:ROADMAP.md`, or `git log --grep=<slug>`
for a specific feature — most shipped items below name the slug to search for). Current
architecture is documented in `CLAUDE.md`. When an item here ships, replace its entry
with a one-line pointer to the shipping commit (or delete it outright) rather than
writing up what was done — the writeup belongs in the commit message, this file is a
todo list, not an archive. A shipped feature that left behind a genuine gap gets its own
short, standalone `- [ ]` item under the relevant section, not a paragraph of "here's
what shipped" with the gap buried at the end of it.

## Vision

An Emacs-class terminal editor: buffers, windows, keymaps, modes, minibuffer,
kill-ring, undo tree, isearch — "everything is a programmable command," with Janet
filling Elisp's role. The whole editor is a Janet-scriptable environment, not a C++
app with a config file bolted on. Modern, memory-safe C++23; TUI rendered with
Notcurses.

## Guiding Constraints

- **Memory safety.** No raw owning pointers — `unique_ptr`/`shared_ptr`,
  `string`/`string_view`, `vector`/`span`. Janet's C heap is external and stays
  malloc'd internally.
- **Programmability first.** New editor capability = a named command reachable from
  keybindings, `M-x`, and Janet uniformly — not hardcoded control flow.
- **XDG Base Directory compliance.** Config → `$XDG_CONFIG_HOME/ned/`, user data →
  `$XDG_DATA_HOME/ned/`, caches → `$XDG_CACHE_HOME/ned/`, other persistent state →
  `$XDG_STATE_HOME/ned/`. Never a bare dot-file in `$HOME`.
- **Keep `Source/UI/` loosely coupled from the TUI library where it's cheap.**

## Open Items

### Embedded Language

- [ ] **Jank replaces Janet** — replace the internal scripting representation with
      [jank](https://github.com/jank-lang/jank). Feasibility was investigated against a
      real local install on 2026-09-08 (`jank-0.1-alpha`, `~/.local`, Clang/LLVM 23);
      everything below is measured on this machine, not read off documentation. **Verdict:
      an embedded jank with a working JIT is real and already builds today** — a
      from-scratch host program that initializes the runtime, loads `clojure.core`, evals
      new source at runtime, and calls jank functions from C++ took an afternoon. The
      blockers are not "can it be embedded"; they are memory footprint, a
      garbage-collector/threading contract that ned currently violates 39 times over, and
      0.1-alpha API ergonomics.

      **The central fork: static vs. dynamic runtime.** The install ships two archives.
      `libjank-static-runtime.a` links no LLVM at all (a hello-world embed is 2.7 MB, 0.5
      ms to init) but refuses at runtime with *"The 'eval' feature is unsupported in a
      static runtime"* — it is AOT-only. `libjank-dynamic-runtime.a` carries the codegen
      and C++-interop objects (`cpp_*`, `builder`, `clang`) and does eval, at the cost of
      dynamically linking `libclang-cpp.so.23` (84 MB) and `libLLVM.so.23` (182 MB). Ned's
      whole scripting model — `init.janet` evaluated at startup, `ned/register-command`
      defining commands at runtime, a REPL panel, project-local plugins — is eval. **Ned
      needs the dynamic runtime, and therefore ships a hard dependency on a matching
      Clang/LLVM.** That is the single largest difference from Janet, which is a ~1 MB
      library with no toolchain dependency at all.

      **Measured, embedded, on this machine** (host program in
      `git log --grep=jank-feasibility` if kept; recipe below):

      | | |
      |---|---|
      | PCH read (68 MB, warm page cache) | ~20 ms |
      | `runtime::context` construction | ~80 ms |
      | `clojure.core` load (AOT-compiled into the archive) | ~25 ms |
      | first eval — runtime usable | **~170 ms** |
      | first `defn` after that (one-time JIT warm-up) | ~85 ms |
      | every subsequent form | **0.1–0.2 ms** |
      | 50-form config file (the `init.janet` shape) | ~5 ms |
      | 100k C++→jank calls | ~19 ms (0.19 µs/call) |
      | process wall time, start to exit | 0.32 s |
      | **peak RSS** | **~237 MB** |

      Startup and call cost are fine — a realistic config file is ~5 ms, and the
      per-form cost after warm-up is negligible. **RSS is the problem**: ned with a file
      open is currently ~28 MB resident. Embedding jank makes the floor ~10× that before
      a single buffer is loaded, which sits badly beside the huge-file work that went in
      specifically to keep memory bounded.

      **Boehm GC and ned's threads — smaller than it first looks.** The GC is not a choice:
      BDWGC (`pthread_support.c.o`, `mark.c.o`, `thread_local_alloc.c.o`, ...) is statically
      baked into both jank archives, and jank's object model is built on it
      (`new (UseGC) context{}`, `IMMER_HAS_LIBGC=1`). Swapping in another GC library is not
      an available move; ned can only manage its *interaction* with the one jank brings.

      Upstream bdwgc already ships the fix, and ned would invent nothing — but its
      *zero-effort* mechanism doesn't reach us. `gc_pthread_redirects.h` macro-redirects
      `pthread_create` → `GC_pthread_create` (present in the archive, along with
      `GC_pthread_join`/`_detach`/`_exit`/`_cancel`/`_sigmask`), which auto-registers the
      thread with no application code at all. Verified: a raw `pthread_create` through
      that macro survives allocation + collection cleanly. Equally verified: **the same
      build with `std::thread` still aborts**, because libstdc++ makes the `pthread_create`
      call inside an already-compiled `.so` where no macro of ned's can reach it.
      `-Wl,--wrap=pthread_create` (bdwgc's `GC_USE_LD_WRAP` path) doesn't rescue it either
      — same reason, and this jank build ships no `__wrap_pthread_create` anyway.

      For `std::thread` the answer is bdwgc's explicit API, which is all the "wrapper"
      anyone needs — `GC_get_stack_base` + `GC_register_my_thread` on entry,
      `GC_unregister_my_thread` on exit, about eight lines of RAII over the library's own
      functions. Verified under load: four `std::thread`s doing 500 allocations and 40
      collections each, concurrently, all clean. `gc_cpp.h` is no help here; it covers
      allocation (`class gc`, `gc_cleanup`), not threads.

      Three thread classes, each measured:

      | thread does | result |
      |---|---|
      | never touches jank | **safe unregistered** — 50 main-thread collections, 15k spins, fine |
      | allocates / calls into jank | **hard abort** — `Collecting from unknown thread`, core dump |
      | merely *holds* a jank ref | **silent corruption** — object collected and its memory reused under load |

      That third row is the dangerous one, and it only shows up under pressure: with light
      load the held value looks intact, and it takes ~400k allocations plus 200 collections
      on another thread before the contents turn to garbage. So the rule is not "threads
      that allocate" but **"any thread that touches a jank object at all must be
      registered"** — there is no safe read-only tier.

      The good news is that ned already satisfies this almost for free. Scripting is
      main-thread-only *by existing design*: every background subsystem marshals through
      `EventLoop::Post`, and the contract is written down (`TestRunner.h`: "including a
      Janet-backed parser -- always runs on the main thread"; `VcsRunner.h` the same, which
      is precisely why a Janet VCS provider splits argv-building from subprocess-running).
      None of the four files including a Janet header creates a thread. BDWGC registers the
      main thread itself at `GC_init`. So the initial port needs **zero** thread
      registration — the 39 `std::thread`/`std::jthread` in the tree are all in the
      "oblivious" row.

      What that buys is an invariant to *defend*, not a migration to perform: the
      eight-line RAII scope above, kept on the shelf for the day some thread legitimately
      needs to touch scripting, plus a debug-build assertion in the binding layer that it is
      running on the registered thread. Worth having early, because the failure it guards is
      silent corruption rather than a crash.

      **What ned would actually have to change.** Smaller than it looks: only four files
      outside `Source/Janet/` include a Janet header (`Editor/JanetSymbolComplete.cpp`,
      `UI/JanetReplPanel.cpp`, `UI/BufferView.cpp`, `main.cpp`). `ScriptingSession`'s
      language-agnostic seam did its job — the other ~117 files mentioning Janet only name
      `ned/set-*` bindings in comments. The real work is the 149 `env.Register<>` bindings
      in `EditorBindings.cpp` (3.2k lines across `Source/Janet/`), `JanetVcsProvider`, the
      bundled `vcs-git.janet` plugin, ~1.2k lines of Janet-specific tests, and every
      user's `init.janet`.

      One genuine architectural *win* to weigh against all this: `jank_closure_create`
      takes a `void *context`. Janet's `JanetCFunction` has a fixed signature with nowhere
      to put one, which is the entire reason `ScriptingSession`'s
      `ScriptingSessionScope`/`CommandContextScope` current-session globals exist — the
      codebase's one deliberate piece of global-ish state. Closures with context delete
      that workaround outright. (Partially verified: a native closure interned as
      `ned/insert` *is* invoked from jank source, but neither shape tried for retrieving
      the context inside the callback worked, and both failed by crashing rather than
      erroring — representative of the alpha's rough edges.)

      **Things not yet raised that will need answers:**
      - **`clojure.core` cannot be bootstrapped through the C API.** `jank/c_api.h` has no
        module loader; `main.cpp`'s sequence (`__rt_ctx = new (UseGC) context{}`,
        `jank_load_clojure_core_native()`, `module_loader.add_load_fn`, `load_module`) is
        C++-only. Ned must adopt jank's C++ headers, not just the C shim.
      - **Inversion of control.** `jank_init_dynamic(argc, argv, ..., fn)` calls *your*
        function; jank wants to own the outermost frame. Ned's `main.cpp` composition root
        and `EventLoop::Run` would have to move inside that callback.
      - **`-rdynamic` is mandatory and conflicts with `--gc-sections`.** The JIT resolves
        symbols against the host executable's dynamic symbol table; without it, evaluating
        anything fails with `Symbols not found: [...jank::runtime::obj::small_integer...]`.
        Verified both ways. Ned's link line and binary size are both affected.
      - **PCH as a deployment artifact.** The dynamic runtime needs a 68 MB
        `incremental.pch`, generated per install into
        `~/.cache/jank/<target-triple>-<binary-version-hash>/`. Ned would have to locate,
        version-match, and possibly generate it — and decide what happens when it's absent
        or stale.
      - **Sanitizers.** ned's suite is expected to stay clean under ASan/UBSan and treats
        findings as real bugs. An ASan build of the embed links and runs, but
        LeakSanitizer reports jank's own allocations (`jit::processor` `strdup`s and
        similar). A suppression file becomes a permanent fixture, which weakens the
        guarantee the current policy rests on.
      - **`jank print-cflags` is not yet a usable build integration.** It emits
        `-I/Development/Jank/compiler+runtime/...` build-tree paths from wherever jank was
        compiled, an unresolved `-Ljank_lib_link_dirs_prop-NOTFOUND`, and omits the
        LLVM/clang/crypto libs the dynamic runtime actually needs (`-lclang-cpp -lLLVM
        -lcrypto` had to be added by hand). It also emits `-l` flags mixed with `-I`, so
        argument order matters. A CMake integration would be hand-rolled today.
      - **Invasive global compile flags.** jank's flags include `-femulated-tls`,
        `-fno-stack-protector`, `-D_FORTIFY_SOURCE=0`, `-fwrapv`, `-DPOINTER_MASK=...`,
        `-DGC_THREADS`, `-D_GLIBCXX_USE_CXX11_ABI=1`. How far these have to propagate
        through `ned_lib` (versus being confined to the TUs that include jank) is
        unresolved and matters for the rest of the codebase's codegen.
      - **C++23 is fine.** jank ships `-std=c++20`, but its headers compile clean in a
        C++23 TU — ned does not have to downgrade. (Verified.)
      - **Two languages at once, or a hard cut?** `ScriptingSession` could host both
        during a transition, at the cost of carrying two runtimes (and two GCs) in one
        process. Worth deciding early; it changes everything downstream.
      - **User-facing cost.** Every `init.janet` in the world becomes an `init.jank`.
        Ned's own `vcs-git.janet` plugin, the Janet REPL panel, `JanetSymbolComplete`'s
        binding-aware completion, and the `ned/register-test-parser` callback convention
        all get rewritten.

      **Suggested sequencing** (nothing here commits to the swap):
      1. Add a debug assertion that scripting entry points run on the registered thread,
         and a small RAII `GcThread` for future use. Cheap, and it turns the
         main-thread-only rule from an undocumented convention into something enforced
         before it can be broken.
      2. Stand up a throwaway `ned_lib`-linked spike that embeds jank beside Janet and
         exposes ~5 real bindings through it — enough to feel the closure-context
         ergonomics and confirm the RSS number in a real ned process rather than a toy.
      3. Only then decide static-vs-dynamic, dual-runtime-vs-hard-cut, and whether 237 MB
         of baseline RSS is a price this editor is willing to pay.

      **Reproduction recipe** (what worked, after several that didn't): compile with
      `jank print-cflags`, source file *before* the flags (they contain `-l`s), swap
      `-ljank-static-runtime` → `-ljank-dynamic-runtime`, drop `-Wl,--gc-sections`, add
      `-rdynamic -L/usr/lib/llvm/23/lib64 -lclang-cpp -lLLVM -lcrypto`, read
      `~/.cache/jank/$(jank print-binary-version)/incremental.pch` into memory and hand it
      to `jank_init_dynamic`, then do `main.cpp`'s `clojure.core` bootstrap inside the
      callback. Note `jank print-cflags` must not be word-split by a shell that doesn't
      (fish keeps it as one argument — build the argv programmatically).

      Upstream is reportedly making embedded builds easier in the coming weeks (noted
      2026-09-08); several rough edges above — `print-cflags` build-tree leakage, the
      C-API module-loading gap, PCH distribution — are exactly the kind of thing that
      might disappear on their own. Worth re-checking before investing in workarounds.

### Remote Execution & Server Protocol

- [ ] **Design ned's own client/server protocol** (raised 2026-09-08 — unstarted, no design
      committed yet; this entry records the shape of the problem and what's already known,
      not a spec). The motivating idea: rather than a remote ned shipping buffers back and
      forth, send *the operation* to where the files are and return only the result — a
      project-wide search, a refactor, a script evaluation. Round-trip count, not bandwidth,
      is what makes remote editing feel bad, so this is likely faster as well as simpler.

      **The load-bearing design decision — local is the degenerate case.** The protocol
      should be the *only* interface, with in-process execution as one transport behind it
      rather than a bypass around it. Two things follow. It can't rot: every local keystroke
      exercises the same path a remote session uses, so remote stops being a bolt-on that's
      broken every time it's picked back up. And it makes "where does this script run"
      a transport question rather than an architectural one — the same request answered
      in-process, by a local subprocess, or by a host across a socket.

      That last point interacts directly with the jank analysis above: if scripts execute
      where the files are, the heavy runtime (jank + Clang/LLVM + a 68 MB PCH, ~237 MB RSS)
      lives on whichever side actually runs them. A remote session's client could then be
      genuinely thin — and, per the same analysis, a headless binary already links
      `libned_lib.a` cleanly with no scripting runtime at all (verified: `Text/` +
      `ProjectSearch` + `GitIgnore`, 8.7 MB, `-ljanet` removed entirely). Only 12 of 316
      objects in `ned_lib` touch anything named "janet", three of those are the tree-sitter
      *grammar* rather than the runtime, and the whole UI-side coupling is one call
      (`Environment::BindingNamesWithPrefix`, for binding-aware completion). The split is
      already there to be taken.

      **Compression must be negotiated, and "none" must be first-class.** Nothing
      compression-related is linked into ned today — `ldd build/ned` shows no zstd, zlib,
      lzma or brotli — so any codec is a new dependency, and assuming one is present on
      both ends is exactly the trap to avoid. Compress per-frame payloads, not the stream,
      or the encoding can't change after the handshake; advertise available codecs at
      handshake and fall back to identity. LSP's own `capabilities` exchange is the model,
      and this codebase already understands it well.

      **This must inherit four protocol bugs already paid for, not rediscover them.** Ned
      has built four framed clients — LSP (`Content-Length` JSON-RPC), DAP (a `seq`/`type`
      envelope over LSP's framing), ACP (newline-delimited JSON), and the broker (an
      `AF_UNIX` relay) — and each of these was a real, root-caused, user-visible failure:
      - an unbounded blocking `connect()` froze a live editor when the daemon's backlog
        filled (`LspBrokerConnect.cpp` now does the non-blocking-connect + `poll` dance);
      - joining a reader thread under a held mutex wedged the daemon for hours;
      - `poll()` on a `-1` fd with a `-1` timeout parks forever — the root cause of the
        `ctest -j8` timeouts;
      - an unbounded `WriteAll` hung the UI, fixed by a per-client writer thread in
        `LspClient`/`DapClient`/`AcpClient`.
      A new protocol gets timeouts on every blocking call, a non-blocking connect, an
      asynchronous write queue, and no lock held across a join — by construction, on day
      one. `EventLoop::Post` is the existing, proven way results come back to the main
      thread; the protocol layer should not invent a second one.

      **Open questions worth settling before any code:** framing (length-prefixed binary vs.
      reusing the `Content-Length` shape already implemented three times); whether requests
      are JSON (nlohmann is already vendored) or something denser; how a long-running remote
      operation streams partial results and gets cancelled (LSP's `$/progress` and
      `$/cancelRequest` are the obvious prior art, already handled in `LspManager`);
      versioning and forward compatibility; and authentication/transport (bare `AF_UNIX`
      locally vs. stdio-over-ssh vs. TCP — the broker's socket-path and trust conventions
      in `BrokerSocketPath.cpp` are the local precedent).

      **Security is not a later concern here.** "Execute this script over a socket" is a
      remote code execution surface by definition. Ned already gates project-local
      `.ned/init.janet` behind `ProjectTrust`'s content-hash registry precisely because
      opening a directory shouldn't run arbitrary code; the same discipline has to extend
      across a transport, where the threat model is strictly worse. Decide the trust model
      alongside the framing, not after it.

- [ ] **One connection class instead of three copies of it** (raised 2026-09-08 —
      prerequisite for the protocol work above, and worth doing on its own merits).
      `LspClient`, `DapClient` and `AcpClient` each hand-roll the same machine, and the
      headers say so outright: *"Threading, lifetime, and member-declaration order all
      mirror LspClient"* (`DapClient.h`), *"mirrors LspClient's own stderrThread_ exactly"*
      (both), *"see LspClient.h's own comment on writeCv_"* (both). All three carry the
      identical member set — `readThread_`/`stderrThread_` declared *before* `transport_`
      so its destructor's fd close unblocks them, then `writeThread_` with
      `writeMutex_`/`writeCv_`/`writeQueue_`/`drainQueueOnStop_` declared *after* it for
      the mirror-image reason. That ordering is a load-bearing correctness invariant
      currently defended by a comment repeated in three files: reorder two members in one
      of them and you get a hang, not a compile error.

      **The seam is clean, because only the top of the stack actually differs.** Framing
      differs (LSP and DAP share `Content-Length` via `Lsp/Transport.h`; ACP is
      newline-delimited with its own). Envelope and dispatch differ (JSON-RPC id matching;
      DAP's `seq`/`type` request-response-event; ACP's genuinely bidirectional,
      async-capable handlers). Handshake gating differs (LSP queues until `initialized`).
      Everything *below* "turn bytes into one frame" — process spawn, the read loop, the
      stderr loop, the write queue and its thread, `EventLoop::Post` marshalling, shutdown
      ordering — is byte-for-byte the same idea three times.

      **Shape, respecting this project's no-pure-virtuals rule:** composition and
      `std::function`, not an abstract base with virtual `DispatchFrame`. That is also
      already the house idiom — the whole `Set*`/register-then-connect convention across
      `UI/` and the managers works exactly this way.
      - `Transport` becomes a concept with concrete implementations rather than one class:
        child-process pipes (today's `Process/ChildProcess`), `AF_UNIX`
        (`LspBrokerConnect.cpp`'s non-blocking-connect + `poll` dance, currently 326 lines
        living alone), and later TCP/stdio-over-ssh for the remote protocol.
      - `FramedConnection` owns the threads, the queue, the member order and the shutdown
        sequence exactly once, parameterized by a read-a-frame callable and an on-frame
        callable. `LspClient`/`DapClient`/`AcpClient` each *own one* instead of
        reimplementing it, keeping only their envelope, dispatch and handshake logic.

      **The payoff compounds with the protocol item above.** The four bugs already paid for
      — unbounded blocking `connect()`, join-under-mutex, `poll(-1, -1)` parking forever,
      unbounded `WriteAll` — get fixed in one place, and any new client (the server
      protocol, a future MCP or nREPL endpoint) inherits all four by construction instead
      of re-earning them. It also deletes the "reorder these members and it hangs" hazard
      from two of the three files.

      **Honest risk:** this is a pure refactor of the most concurrency-sensitive and most
      historically bug-prone code in the tree, all of which currently works. It is only
      worth doing behaviour-preserving, one client at a time, leaning on the existing
      safety net — `LspClientTest`/`DapClientTest`/`AcpClientTest`/`LspTransportTest`/
      `AcpTransportTest`/`ChildProcessTest`/`TaskProcessTest`/the three broker tests, ~4000
      assertions across the three clients — kept green at every step, and re-run under the
      `sanitize` preset rather than just `default`. Do it *before* the server protocol, so
      the new protocol is the first consumer rather than a fourth copy.

### Language Intelligence

- [ ] **Java & Kotlin bundled language support** (raised 2026-09-03 — user wants broad,
      general-purpose Java support, Kotlin alongside it, and Android development to not
      be painful). A system-installed `libtree-sitter-java.so` (confirmed present on this
      machine, e.g. Gentoo's own package) has no queries because a compiled grammar
      `.so` never carries them — `queries/*.scm` are separate text files that live in the
      grammar's own repo, not in the shared library; this is normal for every grammar,
      not a Java-specific gap. `DynamicGrammar.h`'s `dlopen`/runtime-load path (currently
      the only way to reach a non-bundled grammar via `init.janet`) would still need
      those query files sourced from somewhere, so it doesn't actually save the real
      work. The clean path is instead the same one every one of the ~20 bundled
      languages already takes: a real `ned_add_treesitter_grammar(tree-sitter-java
      https://github.com/tree-sitter/tree-sitter-java.git <tag>)` `FetchContent` entry in
      `CMakeLists.txt` (pulls the grammar's own `queries/highlights.scm` etc. with it,
      compiled in via `ned_embed_treesitter_query`, no system package involved at all —
      same reasoning applies to Kotlin, whose absence from the system's own tree-sitter
      packages is irrelevant to ned's build). A community-maintained
      `tree-sitter-kotlin` grammar exists (verify current maintainer/repo/tag before
      wiring it in — unlike Java's grammar, it isn't the tree-sitter org's own). Once
      grammars are in, "broad support" is the same checklist every bundled language
      gets, applied to two more:
      - `TreeSitterMode`/`TreeSitterModeFromLanguage` one-line `JavaMode()`/`KotlinMode()`
        factories (`Mode.h`'s existing pattern), `*-tags.scm` for the symbol-kind gutter,
        smart-indent queries (`cpp-indents.scm`'s own precedent), `*-tests.scm` for test
        discovery (JUnit 4/5's `@Test` annotation, Kotlin's `kotlin.test`/JUnit-on-Kotlin).
      - LSP: `eclipse.jdt.ls` (jdtls) for Java, `kotlin-language-server` (or Kotlin's
        newer official LSP, verify current recommendation) for Kotlin — both configured
        the ordinary `ned/set-lsp-command` way, nothing bundled/auto-detected (this
        project's own stated policy). `Editor/Lsp/LspRootResolver.cpp`'s per-language
        root-marker table gets `java`/`kotlin` entries: `pom.xml`/`build.gradle`/
        `build.gradle.kts`/`settings.gradle.kts`.
      - Test running: `TestOutputParser.h` already has a `"junit-xml"` parser (Maven
        Surefire/Gradle both emit JUnit XML reports) — Java/Kotlin projects need zero new
        parser work, just `ned/set-test-command`/`ned/set-test-results-file` pointed at
        `mvn test`/`gradle test` and the report path.
      - DAP: `java-debug` (the adapter behind VS Code's own Java debugging, also usable
        standalone) — `ned/set-dap-adapter`/`ned/set-dap-launch`, same as any other
        language, no new `DapManager` work.
      - **Android development** mostly falls out of the above once Java/Kotlin +
        `Editor/Tasks/`'s existing generic task-runner (`ned/set-task-command` pointed at
        `./gradlew ...`) + the already-bundled XML mode (layout files) are in place — no
        Android-specific engineering needed for editing/building/testing an Android
        project. A real gap worth naming separately: `adb logcat` streaming and a
        one-click "install + run on device/emulator" flow have no natural home in
        anything that exists today — worth scoping only if plain shelled-out
        `adb`/`gradlew` tasks prove too manual in practice, not speculatively.

Shipped here, one slug each for `git log --grep=`: `go-bundled-language`,
`csharp-bundled-language` (both via `FetchContent` like every bundled grammar — a
system-installed `.so` never carries its own `queries/*.scm`, which is why leaning on one
is never the shortcut it looks like), `resolver-gaps` and `lsp-document-link`
(go-to-file-at-point, LSP-first via `textDocument/documentLink`),
`protocol-stall-timeout-split`, `lsp-multiroot` + `lsp-multiroot-cache-scoping` +
`lsp-workspace-folders`, `listpopup-scroll`.

- [ ] Whether Markdown fenced code blocks / Org `#+BEGIN_SRC` blocks should get the same
      real-LSP-sync treatment HTML `<script>`/`<style>` embedded documents already have
      is an open question — spawning a live language server per code fence in an
      ordinary notes file could be noisy for illustrative/incomplete snippets.
- [ ] `LspManager.cpp`'s `PathToUri` doesn't percent-encode, while its `UriToPath` now
      decodes (`lsp-document-link`, after clangd's own encoded targets proved every
      URI-carrying response was missing paths outside the unreserved set). Nothing has
      needed the outgoing direction yet — it would change the URI every `didOpen` sends,
      so it was left alone deliberately rather than overlooked. Revisit if a project path
      with a space/`#`/`?` in it ever misbehaves.

**LSP completion fidelity** (scoped 2026-09-07 from a full survey of the path).
Shipped since, one slug each for `git log --grep=`: `completion-fidelity` (honor
`textEdit` — per-item replace ranges, both the TextEdit and InsertReplaceEdit shapes,
`itemDefaults`, and one replace-`[replaceStart, point)` accept rule for every source,
which also fixed a real line-corrupting bug against servers that fuzzy-match
server-side; plus incremental narrowing — `sortText`/`filterText`/`isIncomplete`
parsed, the item set kept across keystrokes and refiltered locally via `FuzzyMatch.h`,
the server re-asked only when it said `isIncomplete` or point left the word) and
`completion-popup-scroll`. Both live in `Editor/CompletionSession.h`, not the
`Source/UI/CompletionController.*` this file originally proposed — none of that logic
needs a terminal, so it follows `IncrementalSearch`/`SnippetSession`'s precedent
instead and is unit-tested without a `Screen`.

`completion-resolve` closed the rest of the wire surface:
`completionItem/resolve` (debounced on selection change, merging
`documentation`/`detail`/`additionalTextEdits` back into the live session),
`additionalTextEdits` (an accepted `std::vector` adds its `#include`, in the accept's
own single undo step), server-declared `triggerCharacters` with a real `triggerKind: 2`,
plus `preselect` and `commitCharacters`.

- [ ] `commitCharacters` and `preselect` are honored only where a server actually
      declares them — no default set is ever substituted. That turned out to be
      less protective than it sounds: verified live, `typescript-language-server`
      sends `{".", ",", ";", "("}` on *every* item, so `ned/set-lsp-commit-characters`
      (default on, VS Code's own default) is the switch for anyone who doesn't want
      `;` accepting whatever suggestion happened to be showing. Revisit the default
      if that proves annoying in practice.
- [ ] `additionalTextEdits` are applied against the buffer as it stands at accept
      time, not as it stood when the server computed them. Anything positioned
      *before* point is unaffected by the intervening keystrokes (typing a newline
      dismisses the session outright, so no line above point can shift), which covers
      every real case — an `#include`/`import` near the top of the file. An edit
      positioned after point on point's own line would be off by the characters typed
      since; no server has been observed to send one.
- [ ] Considered and deliberately *not* prioritized (recorded so it stays a conscious
      call): merging non-LSP candidates into the same popup. `RequestCompletionAtPoint`
      (`BufferView.cpp:5427-5438`) is a mutually-exclusive cascade — LSP if running, else
      Janet-binding completion in janet-mode, else dabbrev — so buffer words and snippet
      triggers are strict fallbacks that vanish the moment a server attaches, never ranked
      alongside server items. A real fix means a completion-source abstraction and a
      source-neutral candidate type. `completion-fidelity` moved this closer without
      doing it: `CompletionSession`'s own `CompletionCandidate` is already the wrapper
      such a type would grow out of, but its payload is still the LSP wire struct and
      the fallback sources still synthesize LSP items to fit it. Bigger than the
      fidelity work above and independent of it.

### Mouse Ergonomics

Design stance: over SSH/tmux/a bare terminal, mouse support is genuinely unreliable (no
capture semantics, TUI subprocesses inside `TerminalPanel` don't receive forwarded
clicks at all) — so the mouse must never
be the *only* path to a control; every mouse action needs a keyboard equivalent that
already exists or gets added alongside it. That said, "unreliable as the sole path"
doesn't mean "not worth it" — a right-click menu scoped to exactly what's under the
cursor is often faster than keyboarding to a location and running a named command, even
for a keyboard-first user. Treat mouse work as an accelerant layered on existing
commands, never a replacement for them.

Shipped here, one slug each for `git log --grep=`: `context-menu` (the right-click sweep
across `BufferView` content+gutter, `TabBar`, `ProjectSidebar`, `VcsPanel`, plus
double/triple-click select, gutter click, middle-click paste, scrollback click-drag),
`mouse-hover` (hover tooltips), `hunk-context-menu` (hunk stage/unstage/revert, and
`vcs-revert-hunk` as a genuinely new capability), `sidebar-drag-drop`.

- [ ] Sidebar drag-and-drop leaves the dragged file open in whichever pane was already
      focused, in addition to the drop target — the row's own press-time preview-open
      fires before any drag is known to be one. Fixing it means moving long-tested
      click-vs-double-click timing off Pressed and onto Released, which is why it was
      accepted as a v1 trade-off rather than restructured (`sidebar-drag-drop`).

### Navigation & Search

Shipped here, one slug each for `git log --grep=`: `full-commit-diff-view`,
`auto-collapse-on-build`.

- [ ] **Multibuffer gaps, remainder**: `project-find-references`' RE2 text-scan
      fallback path (no LSP server running for the buffer) and the real
      `textDocument/references` path both still build one excerpt per match with no
      upper bound on total work done — auto-collapse only degrades *display*
      gracefully, not the search itself. `VisitResultUnderPoint`'s jump-to-source also
      stays line-granularity even though `Buffer::ExcerptRange` already carries the
      exact source byte range that would let it preserve the intra-line column.
- [ ] A real visual side-by-side 3-way merge/diff view. `AutoMerge` auto-resolves the
      common case and drops real `<<<<<<<`/`=======`/`>>>>>>>` conflict markers into the
      buffer for a genuine divergence, but a real conflict is still hand-edited text,
      not a visual diff — see "Merge Conflict Resolution Mode" below, which scopes a
      chord/mouse-driven *resolution* workflow over these same markers without needing
      this visual diff first.
- [ ] Native Vim-mode `]c`/`[c` binding (gitsigns' own convention) for
      `vcs-next-hunk`/`vcs-previous-hunk` — the global `C-c v N`/`P` binding already
      works under Vim mode via the shared keymap-stack fallthrough, so this is polish,
      not a functional gap.

### Editor Ergonomics

Shipped here, one slug each for `git log --grep=`: `terminal-panel-scrollback`,
`jumplist-ring`, `changelist-ring`, `dot-repeat-count-override`, `vim-global-marks`,
`vim-magic-translation`, `vim-macro-register`, `dap-round-3` through `dap-round-5`,
`session-persistence-round-2`, `snippet-expansion-gaps`, `bundled-snippets`,
`multiple-terminal-tabs`, `unified-left-dock`, `test-runner-gaps` (gutter-click
run-this-test plus a pre-run `▸` affordance, the failures-only degradation surfaced
instead of silently degrading, and go's basename-only `file:line` resolved through the
import path it already reports).

- [ ] **Terminal-side mouse forwarding** — clicks/wheel inside `TerminalPanel` are
      consumed by the panel itself (focus, scrollback ring); a TUI subprocess running
      inside it (e.g. `htop`, `vim`) never receives a forwarded mouse event.
- [ ] **OSC 52/title integration inside the embedded terminal** — a program running
      inside `TerminalPanel` that emits its own OSC 52 clipboard/title sequences isn't
      relayed anywhere; unrelated to `Editor/Clipboard.h`'s own OSC 52 *write* path for
      ned's own copy/paste commands, which already works.
- [ ] **DAP gaps, remainder**: data breakpoints (tied to a live variable rather than a
      source line) have no natural entry point yet. Thread-focus reattachment across a
      session restart is deliberately excluded even if revisited — a fresh session has
      entirely new thread IDs, nothing meaningful to reattach it to.
- [ ] **No server/daemon mode** — no `emacsclient`-equivalent; one process per terminal,
      no way to keep a warm process (buffers, LSP connections, undo history) alive and
      attach a new terminal client to it.
- [ ] A results-buffer line whose path doesn't resolve still silently opens an empty
      scratch buffer of that name (`BufferView::JumpToPathLine` →
      `BufferList::OpenOrCreateFile`, which creates on miss by design — that's what
      `find-file` on a new path needs). `test-runner-gaps` fixed this at the
      *producer* for `*test results*` (`Editor/TestRun/TestSourceResolver.h` resolves
      each path at rebuild time, so the line carries a real one), but every other
      `path:line:` producer — project search/replace, the agenda, `DiagnosticsLog`,
      blame — still hands its path straight through. The general fix is at
      `VisitResultUnderPoint`: report a miss rather than creating. Left alone because
      those producers all write paths that do exist today.
- [ ] `TestSourceResolver`'s basename search picks the shallowest candidate when
      nothing disambiguates (no go package hint, no directory components in the
      reported path, several same-named files) — deterministic, but it can be the
      wrong file. A prompt-to-choose would be the honest answer; not worth it until
      the silent wrong pick is actually seen.
- [ ] **Nested snippet placeholders' inner stops** (`${1:foo ${2:bar}}` keeps only the
      literal text "foo bar", the inner `$2` tabstop is dropped). Confirmed *not*
      reachable via the tree-sitter fold-depth machinery (`CodeFold.h` re-derives a
      fold's extent from a fresh AST parse on demand; a snippet body has no
      grammar/parser behind it to re-derive anything from). The real blocker: field 2's
      range would need to sit properly *contained inside* field 1's range, and
      `Buffer::SnippetRange`'s relocation/gravity model currently only understands
      "disjoint or adjacent" — true nesting needs new relocation semantics in
      `Text/Buffer.h`, not a parser gap. Not attempted.
- [ ] Hunk unstage matches point against the *cached* staged diff, which drifts when
      unstaged edits exist earlier in the file — exact in the common stage-then-undo
      flow; revisit only if it bites.
- [ ] **`libned` as a real shared library** — `ned_lib` (static today) exists solely so
      `ned_tests` can link real editor code without pulling in `main()`; a static lib
      already does that job. Worth revisiting only if a second real consumer shows up
      (an embedding use case, a separate CLI tool) — would need symbol-visibility
      curation and SONAME/ABI-versioning discipline that don't pay for themselves yet.
- [ ] A friendlier, possibly visual surface for browsing/editing ned's own settings
      beyond hand-writing `init.janet` — real live-editing already exists for themes
      specifically (`save-theme`/`ned/theme-set`); a general settings surface would
      generalize that. Vague, unscoped.
- [ ] **Alternate "modern" keymap (VS Code/JetBrains-style)** — tabled 2026-09-04
      discussion. The itch: Emacs's C-w/M-w/C-y read as arbitrary next to the
      now-universal C-x/C-c/C-v cut/copy/paste convention. Audited and found to be a
      real structural conflict, not a simple rebind: C-x and C-c are Emacs *prefix*
      keys here (C-x owns file/window ops, C-c is this codebase's own mode/user prefix
      with dozens of bindings), and C-v is already bound (scroll-page-down). Retrofitting
      the default keymap would evict all of those, not just rename two keys. C-w/C-y
      also aren't plain cut/paste — they're `KillRing` ops (a ring, not a single slot),
      so a literal rebind needs to keep kill-ring semantics under new trigger keys, not
      just alias them. Right approach if this gets picked back up: a third selectable
      full keymap (`ned/set-keymap-style` or similar: `emacs` default, `vim`, `modern`)
      reusing the existing command set wholesale, the same shape `ned/set-vim-mode`
      already proves out — not a patch on the Emacs default, since that default stays
      load-bearing for `C-c`'s existing feature bindings either way. Not started; no
      keymap table drafted yet.
- [ ] **Bracketed-paste multi-cursor distribution** (paste-perf-and-drag-drop
      follow-up, scoped down 2026-09-06). A real terminal paste (via
      `BufferView::HandleBulkPastedText`/`Buffer::InsertAtPoint` fast path) inserts at
      a single point only, even with secondary cursors active — v1 deliberately
      matches the existing middle-click-paste precedent rather than `yank`'s
      `ForEachCursor`-based per-cursor splitting. Revisit if multi-cursor editing and
      real terminal paste turn out to be used together often enough to matter.

### Merge Conflict Resolution Mode (New Feature)

Shipped here, one slug each for `git log --grep=`: `merge-conflict-resolution` (the
`Text/ConflictHunk.h` model, the `merge-take-*` commands on a `C-c x <letter>` prefix,
hunk navigation, the themed ours/theirs/base tint) and `conflict-quick-keys` (the
conflict-scoped `M-o/t/b/d/k/n/p` layer). No modal state was introduced at all, a
deliberate simplification over the original scoping — resolution is ordinary commands
over ordinary buffer text, so "never a modal trap" fell out for free.

- [ ] **A per-hunk inline mouse action row** (take-ours/take-theirs/take-both/take-neither
      as clickable text, `BufferView`'s existing gutter-click precedent) — the mouse-driven
      fast path the keyboard-only v1 above is missing; not required, scope as a follow-up.
- [ ] **Whole-file / whole-hunk-run bulk actions** — "take all ours"/"take all theirs"
      for a file with many mechanically-identical hunks (e.g. a lockfile or generated
      file where one side is always right) — a `VcsPanel` per-file action, not a
      per-hunk one; scope only if real usage shows the per-hunk loop is too slow for
      that case.
- [ ] Out of scope for v1: rebase/cherry-pick conflict *sequences* (resolve, `git rebase
      --continue`, repeat) — the hunk-resolution primitive above is what such a sequence
      would be built on later, but driving the sequence itself needs its own `VcsRunner`
      plumbing (`RequestRebaseContinue`/abort/skip) not touched here.

### Jupyter Notebooks

Feasible, but subsystem-sized — closer in total scope to the LSP and DAP builds
combined than to any single feature shipped so far. Three genuinely separable pieces,
each with its own verdict:

**The kernel protocol client** (the hard, unavoidable part) — real interactive
notebooks need the actual Jupyter messaging protocol: 5 ZeroMQ sockets (shell/iopub/
stdin/control/heartbeat) carrying HMAC-signed multipart JSON, addressed via a
connection file (ports + key) written after the kernel process is spawned. There's no
shortcut around this — `jupyter console --simple-prompt`'s text-only REPL loses rich
output entirely (no `image/png`, no structured tables), and shelling out to `nbconvert
--execute` per run loses the interactive "run one cell, keep kernel state" loop that's
the entire point.
- [ ] ZeroMQ becomes a new dependency (libzmq C library + cppzmq's header-only C++
      wrapper) — pullable via `FetchContent` like everything else, but a heavier build
      dependency than anything currently vendored.
- [ ] HMAC-SHA256 message signing has no existing primitive in this tree. Hand-rolling
      SHA256+HMAC (~150 lines, a well-specified algorithm, low stakes since it's
      same-machine IPC integrity rather than a real security boundary) fits this
      codebase's own precedent (hand-rolled LSP/DAP/ACP framing) better than pulling in
      a full crypto library for one function.
- [ ] The client itself is the same shape already proven three times over
      (`Lsp/LspClient.h`, `Dap/DapClient.h`, `Acp/AcpClient.h`): background `jthread`
      read loop, `EventLoop::Post` marshaling every frame onto the main thread, a
      manager above it owning lifecycle/handshake/in-flight-request bookkeeping — the
      wire format differs (ZMQ multipart + HMAC vs. `Content-Length` or
      newline-delimited JSON), the architecture doesn't.
- [ ] Kernel discovery should walk `share/jupyter/kernels/*/kernel.json` directly
      rather than shelling out to `jupyter kernelspec list --json`, so this doesn't
      hard-depend on the `jupyter` CLI being on `$PATH` at all.

**Rich output rendering** (two real synergies with existing subsystems, one real gap)
- [ ] Tables (`text/html` DataFrame reprs) parse into a grid and align via the
      *existing* `Editor/Table.h` toolkit (`SplitRow`/`ComputeColumnWidths`/`PadCell`
      are already format-agnostic) — a small `<table>`-extraction layer on top, not a
      new engine.
- [ ] Images (`image/png`, the matplotlib case) reuse the *existing* pixel-graphics
      path already proven live in `Minimap.cpp`: `ncvisual_from_rgba` +
      `ncvisual_blit(..., NCBLIT_PIXEL)` already renders arbitrary RGBA buffers as real
      terminal pixels where the terminal supports it, falling back gracefully where it
      doesn't, exactly as Minimap does today. The one missing piece is a PNG decoder —
      nothing in this tree parses PNG; a single-header vendor (stb_image-style) is the
      pragmatic addition, not a hand-rolled zlib+PNG implementation.
- [ ] `image/svg+xml` (also common from plotting libraries) has no rendering path
      anywhere in this codebase and no terminal protocol renders vector graphics
      directly — v1 would skip it, or later shell out to `rsvg-convert`/similar as an
      optional external tool.
- [ ] Error tracebacks arrive ANSI-colored — needs stripping or a small ANSI-to-`Cell`
      translator, not the full `libvterm` emulator `TerminalPanel` uses (that's built
      for a live interactive shell; this is a static blob of text).
- [ ] `text/plain` (every rich mimetype's required fallback in nbformat) needs nothing
      new.

**The editing/UI model** (the real structural departure) — ned's whole editing surface
is built on `Buffer` = one Rope of text; a notebook's natural unit is a *sequence of
cells*, each with its own source text, type (code/markdown/raw), and non-text output
data. Nothing today models "many independently-editable text regions plus non-text
data, composed as one document." `Editor/EmbeddedDocuments.h` is the nearest existing
precedent and isn't that close — it builds *virtual, non-editable, width-preserving*
documents for LSP sync only, not real independently-editable cells.
- [ ] Two shapes are open: **(a)** a `NotebookView` widget (parallel to `BufferView`)
      directly composing several real per-cell `Buffer`s + a per-cell `Mode` (a code
      cell in a Python kernel gets full `PythonMode` highlighting, potentially real LSP
      sync too) plus non-editable output panels between them; **(b)** something closer
      to Org's outline-over-flat-text trick — one `Buffer` in a synthetic linear
      representation with cell boundaries as markers, translating to/from `.ipynb` JSON
      only at load/save. (a) is more work but composes cleanly with everything
      `Buffer`/`Mode`/LSP already assume; (b) is a smaller structural add but forces
      outputs (images, rich tables) into a `Buffer`'s text model, which they
      fundamentally aren't. (a) is the likely right call precisely because it reuses
      more of the existing machinery, not less.
- [ ] File identity gets murkier under (a): does each open notebook register its cell
      `Buffer`s in `BufferList` (so they'd leak into `switch-to-buffer`/the tab bar), or
      does `NotebookView` own them privately outside `BufferList` entirely? The latter
      is probably right but is a real departure from "everything is a buffer."

**Explicit constraint carried over from the Org Babel "won't do" entry below**: a
`.ipynb` *is* a code-execution artifact by definition — unlike a `.org` file, which is
ostensibly prose that Babel would silently turn into one — so building this at all is a
different call than Org Babel was. The same principle still applies inside it, though:
opening a notebook file must never execute anything; every cell run is one explicit
user action, mirroring how `Dap/`'s launch step and `Tasks/`'s run command already
require an explicit trigger rather than firing on buffer-open.

**Suggested phasing** (each phase independently shippable/demoable):
1. Parse/serialize nbformat v4 JSON into an in-memory `Notebook` struct — pure,
   unit-testable, no kernel/UI involved; round-trip fidelity is the only bar.
2. Read-only `NotebookView`: render existing cells (source + already-saved outputs)
   with the rendering pieces above — proves the rendering story before any protocol
   work starts.
3. Kernel protocol client + manager (ZMQ, HMAC, spawn/handshake), no UI — testable
   headlessly against a real `ipykernel`, the same way `LspClient`'s tests run against
   real `clangd`.
4. Wire "run cell" into `NotebookView`, one cell at a time, no kernel-state UI polish.
5. Everything else (interrupt/restart kernel, kernel-status indicator, variable
   inspector, notebook-wide "run all") is incremental once 1-4 exist.

Won't do in v1 regardless of the above: ipywidgets (a separate protocol layered on top
of the base one), collaborative/real-time editing, any kernel-specific special-casing
beyond whatever `kernel.json` advertises.

**Reuse candidates once this exists**: the rich-output-cell renderer (table via
`Table.h`, image via the pixel-blit path, ANSI-stripped text) is generic over "a block
of structured content below some source," not Jupyter-specific — `AcpPanel`'s
transcript has the same open gap (lightweight markdown rendering, better tool-call/
table output display, both listed in its own ROADMAP entry above) and is a stronger,
nearer-term reuse target than Jupyter itself. If Org Babel is ever revisited despite
the "won't do" below, this same renderer (and possibly `NotebookView`'s cell-execution
UI) is the natural substrate for displaying a `#+BEGIN_SRC` block's results, rather than
a third bespoke implementation — worth designing the renderer as its own reusable piece
rather than embedding it directly in `NotebookView` for exactly this reason.

### Named Projects & Multi-Project Sidebar (New Feature)

Local-only slice shipped (`Editor/ProjectRegistry.h`, `switch-project`/`open-project`
on `C-c P s`/`C-c P o`, `Editor/TerminalTabLauncher.h` for opening a picked project in a
new terminal tab — tmux, screen, Konsole, WezTerm, Ghostty, kitty live-verified; GNOME
Terminal shipped but unverified) — see `git log --grep=named-projects`.

Still open, all genuinely gated on Remote Development below (a registry entry's root
staying local-only for now is a storage-shape choice, not a hole in what shipped):

- [ ] **Remote project URIs (`user@host:/path`)** — a registry entry's root can be a
      remote URI using the same syntax `scp`/`ssh` already accept (no bespoke URI
      grammar to invent). Actually opening one is gated on Remote Development's own
      file-I/O seam (`Buffer::FromFile`'s remote path) existing first — this bullet is a
      UI/storage change on top of that, not a substitute for it.
- [ ] **`~/.ssh/config` awareness** — parse (not shell out to `ssh -G`, which resolves
      one host at a time and is awkward to batch-query for autocomplete) the user's own
      `~/.ssh/config` — `Host`/`HostName`/`User`/`Port`/`IdentityFile`/`ProxyJump`,
      wildcard `Host` patterns included — to default the user/port/key when a typed
      `host:/path` URI doesn't repeat what SSH config already knows, and to drive
      host-name autocomplete on the open/connect prompt. A small self-contained parser
      (the format is simple and line-oriented) rather than a new dependency.
- [ ] **Autocomplete on the open/connect prompt** — project name (registry) first, then
      host (parsed `~/.ssh/config` `Host` entries) for an unregistered target, then
      remote path (once connected, via lazy per-directory SFTP `readdir` — the same
      expand-on-demand shape `ProjectSidebar` already uses locally). `Editor/
      FuzzyMatch.h` is the natural filter for all three, matching every other
      fuzzy-completed prompt in this codebase.
- [ ] **SSH transport: shell out vs. libssh** — either is fine; leaning shell-out first,
      consistent with Remote Development's own phase-(a) plan below. Shelling out to
      real `ssh`/`sftp` (`Editor/Process/ChildProcess.h`'s existing subprocess-wrapping
      precedent) means zero new build dependency and automatic, free reuse of the
      user's own config/agent/`known_hosts` handling — the cost is a process-spawn
      round trip per operation and no persistent multiplexed connection without also
      managing `ssh -M`/`ControlMaster` sockets by hand (worth trying before reaching
      for `libssh` — it may close most of the multiplexing gap with no new dependency
      at all). Direct `libssh` linkage buys one auth handshake + many channels and
      programmatic SFTP with no per-call subprocess spawn, at the cost of a new
      `FetchContent` dependency and reimplementing config/agent/known-hosts handling
      the real `ssh` binary already gives away for free.
- [ ] **`TerminalTabLauncher` remainder** — Terminator, Tilix, and any other emulator
      with a real CLI/IPC way to join a running instance are a documented remainder,
      not v1; add on the same table-driven pattern once someone actually needs one. GNOME
      Terminal's own handler shipped but was never live-verified (not installed in the
      environment this shipped in) — worth a real check the first time it's reachable.

### Remote Development (SSH Remote Editing)

The goal: edit files on a remote host over SSH without ned itself running remotely —
comfortable enough that it doesn't feel like a degraded mode. See "Named Projects &
Multi-Project Sidebar" above for how a remote root is named/stored/opened/switched to;
this section is the actual file-I/O/search/agent mechanics underneath one. Two shapes
are on the table and genuinely undecided: **(a) client-only**, everything shells out to
`ssh`/`sftp` per operation, no code footprint on the remote host at all; **(b) client +
thin remote agent**, a small companion binary deployed to the remote host (the
JetBrains Gateway/VS Code Remote-SSH precedent) that does what a bare SSH session is
genuinely bad at — fast recursive search, live file-change notification, and, the big
one, running the actual language server/debugger/task/VCS subprocesses *on* the remote
host against its real toolchain rather than against a locally-synced copy missing the
remote system's headers/deps/`compile_commands.json`. Likely path: build (a) first
since it's simpler and gets editing working at all; add (b) only once search latency or
LSP-against-the-wrong-toolchain prove it's needed in practice, not speculatively.

**File I/O & the editing model**
- [ ] A remote-file I/O seam behind `Buffer::FromFile`/the save path (an interface, not
      a hardcoded `std::filesystem` call) so a remote buffer's `Rope`/`UndoTree`/
      multi-cursor/etc. stay completely unchanged — read the whole file once (`sftp`/
      `ssh host cat`), edit it exactly like a local buffer, write it back on save
      (matching the instinct: edit locally, sync on save, not a live remote-authoritative
      buffer). This is most of "make it work at all" and touches no core editing code.
- [ ] `ExternallyModified()`/`AutoRevert`/`AutoMerge`/`FileWatch` all assume a cheap
      local `stat` — decide what they mean for a remote buffer: probably a `stat`-only
      round trip on the existing poll tick (no live inotify without a remote agent),
      likely with a longer poll interval over a slow link.
- [ ] Remote path display throughout the UI — tab bar, mode line, sidebar title, buffer
      names — needs a clear `user@host:/path` presentation distinct from local paths.
- [ ] Save/revert/external-modification error handling for a connection that can drop
      mid-operation — a local-disk write basically can't fail; a remote one can,
      constantly, and needs real user-facing recovery (`Editor/ProcessTimeouts.h`'s
      existing settings-module shape is the right precedent for configurable timeouts).

**Project tree & search**
- [ ] Remote directory listing for the sidebar — plain SFTP `readdir`, lazily per
      expanded directory (matches `ProjectSidebar`'s existing expand-on-demand model, no
      new mechanism needed), just slower per round trip; needs an async/spinner
      treatment so a slow link doesn't freeze the UI (`AsyncFileLoader`'s own precedent).
- [ ] **Remote project search is the one place a bare SSH session is genuinely
      insufficient** — shipping every file across the wire to search locally would be
      very slow past a LAN. Two real options: (a) shell out to a remote `rg`/`grep` if
      present on the remote host — zero deployment, but an extra dependency assumption
      and search semantics that won't exactly match ned's own `.gitignore`/binary-
      detection rules; (b) a remote agent reusing ned's actual `ProjectSearch`/
      `GitIgnoreMatcher`/RE2 engine, guaranteeing identical results locally and remotely.
      This is the strongest concrete argument for building the agent at all.
- [ ] Project-wide replace, find-references, and the agenda/TODO scan all sit on top of
      `ProjectSearch` today — decide whether they degrade gracefully in client-only mode
      or need the same remote-agent path search does.

**The remote agent/service** (explicitly OK to defer — scoped here, not started)
- [ ] Define what it actually needs to do beyond search: fast directory listings without
      N round trips; cheap change notification (a remote `inotify` watcher relayed back,
      closing the same gap `FileWatch.h` solves locally); optionally hosting the real
      LSP/DAP/task-runner/VCS subprocesses on the remote machine against its own
      toolchain and proxying their stdio back — likely the single biggest design lift in
      this whole feature, bigger than local file editing itself, since it means
      `LspManager`/`DapManager`/`TaskRunner`/`VcsRunner` would all need a "spawn this
      subprocess remotely instead of via `ChildProcess::posix_spawn`" seam.
- [ ] Transport/protocol for talking to the agent: an SSH-forwarded Unix socket or a
      `ssh -L`-forwarded local TCP port, framed the same way `Lsp/Transport.h`/
      `Acp/Transport.h` already are — the codebase has built this exact client/manager/
      transport shape three times now (LSP, DAP, ACP); a fourth following the same
      precedent is low-risk relative to inventing something new.
- [ ] Auto-deployment: detect the agent is missing or stale on the remote host and
      self-install it (`scp`/`sftp` push + `chmod +x`), the VS Code Remote-SSH/JetBrains
      Gateway precedent, with a version handshake so a stale cached copy gets replaced
      rather than silently mismatching.
- [ ] **Language/toolchain choice for the agent is genuinely open.** Reusing `ned_lib`'s
      own C++ `ProjectSearch`/`GitIgnoreMatcher`/RE2/`ChildProcess` code as a headless,
      UI-free build gives identical search/gitignore semantics locally and remotely for
      free and avoids a second implementation to keep in sync — but `ned_lib` doesn't
      currently separate cleanly from Notcurses/Janet, so this needs `ned_lib` split
      into a real UI/scripting-free "core" library first (a legitimate, if mechanical,
      restructuring — also exactly what a future `libned`-as-shared-library consumer
      would need, see that item above). Rust or Go remain reasonable fallbacks if that
      split turns out costlier than a from-scratch rewrite — genuinely easier static
      linking/cross-compilation for "any x64 host" at the cost of reimplementing and
      hand-syncing gitignore/search-matching semantics a second time. Decide once the
      C++ split's real cost is known, not before.
- [ ] Repo/build story: a standalone repo (this is a genuinely separable concern from
      the core editor), pulled back into ned's own build via CMake `FetchContent` like
      every other bundled dependency. The agent binary itself needs to target the
      *remote* host's architecture, not the build machine's — either cross-compiled at
      ned's build time or fetched prebuilt per-arch (GitHub-releases-style), since a
      build toolchain isn't guaranteed to exist on an arbitrary remote host.
- [ ] Perhaps we could have a way to enable and execute a remote debug session,
      particularly useful for things like PHP, and the like, where we could remotely
      debug something that's happening live in production.

**Connect UX**
- [ ] A connect dialog/command (`ned-connect` or similar): host, user, port, key/agent
      selection, jump-host/bastion support. Sourcing defaults from `~/.ssh/config` and
      the remembered-projects list are covered by "Named Projects & Multi-Project
      Sidebar" above rather than a separate mechanism here.
- [ ] Host-key verification / first-connection trust prompt — `ProjectTrust.h`'s
      existing hash-based, disuse-expiring trust registry (built for `.ned/init.janet`)
      is a good precedent for this UX rather than silently trusting or reimplementing
      OpenSSH's own `known_hosts` handling from scratch.
- [ ] Reconnection/resilience story for a dropped connection mid-session: unsaved local
      buffer content already survives (it's just memory), but in-flight remote
      operations (search, save, LSP requests) need a clear timeout/retry/failure surface
      rather than hanging silently.
- [ ] Confirm mixing local and remote buffers in the same window layout genuinely "just
      works" once the I/O seam exists (`WindowManager`'s panes are already
      buffer-agnostic) rather than assuming it without checking.
- [ ] Port-forwarding scope check: is this only for reaching the remote agent's own
      socket, or also user-facing forwarding of a remote dev-server port back to the
      local machine (JetBrains Gateway does the latter) — worth naming as a distinct,
      smaller feature rather than silently bundling it in.

### Collaboration & AI

Shipped here — see `git log --grep=ACP` for the panel's own long tail (interrupt/
spinner, thought/text split, streaming debounce, collapsed tool calls, composer
word-motion/history, auto-reconnect, word-wrap, checkpoint/rewind, Markdown rendering,
@-mention autocomplete) and `--grep=panel-dock` for the tabbed bottom dock that now hosts
Terminal/ACP/Debug Console on one tab strip.

- [ ] **AI-assisted editing (ACP) gaps** (validated live 2026-08-26 against Claude
      Code's own ACP adapter): no scrollback in the panel; `terminal/*`
      tool-call support and `elicitation/create` structured forms are undeclared as
      client capabilities; no multiple concurrent agents/sessions (still one at a time,
      `Dap/`'s own precedent); no `session/load` history replay;
      `session/set_config_option`/`session/set_mode` aren't surfaced to the user; no
      per-agent environment-variable override (`ChildProcess`'s `posix_spawn` always forwards the
      parent's global `environ`); no per-agent "character" (display-name/accent color).
      Separately: `Keymap::AmbiguousBindings()` is diagnostic-only (a
      `CommandsTest.cpp` regression test), not enforcement — `Keymap::Bind` still lets a
      caller construct an unreachable-by-typing binding; a real structural fix (Emacs'
      own `define-key` semantics: reject/restructure a bind that would shadow an
      existing command) would change `Bind`'s signature across every call site
      including `ned/define-key`.
- [ ] **Known rough edge**: a right-docked `AcpPanel`'s resize handle has no visually
      reserved border the way `ProjectSidebar`'s divider column does (right-dock mode
      stays a fully separate, byte-for-byte-unchanged standalone overlay from the
      `PanelDock`-hosted bottom-dock mode).

Shipped here, one slug each for `git log --grep=`: `acp-mcp-tool-bridge` and
`acp-mcp-tool-bridge-remainder` (`Editor/Mcp/`, `ned/set-acp-mcp-bridge`, ~20 tools plus
`capture_note`; the original transport question resolved to a real local MCP server,
forced by the spec — stdio is the only transport every agent must support, so
`ned --mcp-stdio-relay` is a dumb byte pump into the live process's own Unix socket where
`LspManager`/`VcsRunner`/`TestRunner`/open `Buffer`s actually live), `dap-acp-bridge`
(structured DAP tools + `dap-ask-agent`), `acp-context-auto-attach` (`@buffer`/
`@selection` composer mentions, `ask-agent-about-line`), `acp-composer-prose-check`, and
`Text/LineDiff.h`'s `SplitLines`/`DiffLines`/`UnifiedDiff` with the agent-edit diff
preview built on it.

Deliberately cut from those slices, still open:
- [ ] **Real rename/code-action apply + `goto(file, line)` navigation** — all three
      need a live `WindowManager`/`BufferView` (`ApplyProjectEdit`'s multi-file
      transaction machinery for the first two — `ProjectUndoManager` recording, file
      create/rename/delete via `DocumentChangeOp`; `BufferView::JumpToPathLine` for the
      third) that `McpToolRegistry` doesn't have and doesn't currently reach. Not a
      thin wrapper the way everything shipped so far is — a real follow-up, not
      attempted here.

- [ ] **A `dap_get_pointer_graph`-shaped MCP tool** — the pointer-graph/memory/
      disassembly/thread/function-and-exception-breakpoint surface stayed out of the DAP
      tool set on purpose (not part of the ask/step/inspect loop that slice targeted),
      but the graph one specifically needs real work rather than another thin wrapper:
      `BufferView::ExpandPointerGraphNode`'s cycle-detection traversal would have to be
      extracted into a UI-free helper first (`Editor/PointerGraphNode.h`'s data shape is
      already reusable, the traversal isn't).

- [ ] **Real-time collaborative editing** (CRDT-based) — the biggest lift in this file;
      last.
- [ ] VCS: "generalize the two-callback plugin shape past version control" (cloud CLIs,
      Terraform, Docker) remains an open idea, not a plan.

Explicitly *not* pulled from prior research: OpenCode's session-sharing (needs a hosted
backend, out of scope for a local-first editor) and a unified command palette (a stated
non-goal, see below).

### Documentation & Companion Tooling

- [ ] **Internal/developer docs: Doxygen.** `/** */` doc-tags on the C++ side
      (namespace/class-hierarchy aware, matching this codebase's `ned::text`/`editor`/
      `janet`/`ui` layering), themed with Doxygen Awesome. Wired via CMake's
      `find_package(Doxygen)` + `doxygen_add_docs()` as an opt-in `docs` target, not part
      of `ALL` — a build/release-time step, nothing ned does at runtime. clang-doc was
      considered and rejected as still too early-stage (LLVM's own docs warn of bugs/
      crashes on real codebases).
- [ ] **End-user docs: mdBook + pandoc, one Markdown source.** A `book/src/` tree (the
      `Docs/*.md` files migrate in near-verbatim) renders via mdBook into a
      navigable/searchable site, deployed to GitHub Pages via its first-party
      `actions/starter-workflows/pages/mdbook.yml` template — self-hostable later too,
      it's plain static output. Man pages come from pandoc (`pandoc -s -t man`) run over
      a curated subset of the same source files (CLI invocation, the settings/variables
      reference, the scripting API reference — not the whole book; prose guides don't
      map to man's NAME/SYNOPSIS/DESCRIPTION shape). The scripting API reference page is
      generated, not hand-written: a small `Tools/` binary linking `ned_lib`, walking
      `CommandRegistry`/the `ned/*` Janet binding table, dumping the doc strings already
      passed to `Register<Fn>` into a `.md` file the mdBook build consumes (mirroring
      Helix's `cargo xtask docgen` pattern for its own keymap/command reference pages).
      Sphinx+MyST was considered — it natively builds man+PDF+HTML from one source too —
      but rejected in favor of mdBook to avoid adding a Python toolchain to a project
      that currently has none, and for mdBook's first-party GitHub Pages support.
- [ ] **Environment setup tool** (`ned-setup` or similar) — first-run detection: shell
      integration, plus scanning the system for installed tree-sitter grammars and
      *generating an editable Janet file* loaded from `init.janet`. Deliberately a
      standalone, inspectable generator — silent runtime auto-detection was considered
      and rejected (system grammar layouts aren't portable).
- [ ] **Tree-sitter-assisted formatter** with JetBrains-level per-rule configurability
      ("a dprint clone that is actually awesome") — a substantial project per language,
      not a utility. Scope it once concrete gaps left by external formatters are known.
- [ ] **Cookbook entries for debugger-adjacent tools that already work with zero new
      code** (audit finding, 2026-09-06 — a documentation gap, not a code gap):
      Valgrind (`valgrind --vgdb=yes --vgdb-error=0` + DAP `Attach`, memcheck errors
      arrive as ordinary `stopped` events) and Docker/embedded/OpenOCD targets
      (`Attach`'s adapter/config is opaque argv + JSON, so `cortex-debug`-style or
      Docker-aware adapters already work via `ned/set-dap-adapter`/`ned/set-dap-attach`)
      both need a worked example in the docs, not new `DapManager` code.

### Notcurses Patches Worth Upstreaming (Watch List)

Not submitted anywhere yet — a deliberate choice (2026-09-06), not an oversight. Recorded
so the research doesn't have to be redone before actually opening anything.

- [ ] **`CMake/PatchNotcursesNulKey.cmake`** (Ctrl+Space/Ctrl+@ swallowed as a NUL byte)
      and **`CMake/PatchNotcursesMouseWheel.cmake`** (SGR wheel-right, Cb=67, misdecoded
      as a motion+release) are both still-reproducible bugs against upstream
      `dankamongmen/notcurses` `master` as of 2026-09-06 (verified live against the
      current `src/lib/in.c`, not just our pinned `v3.0.17`) — checked GitHub issues for
      both ("Ctrl+Space", "NUL", "0x00", "wheel") and found nothing matching, so these
      look like genuinely novel, unreported bugs. Both patches are already small,
      root-caused, and general (not ned-specific workarounds), so they're close to
      PR-ready as-is whenever we decide to open them.
- [x] ~~Bracketed paste mode~~ — shipped 2026-09-06 (`CMake/PatchNotcursesBracketedPaste.cmake`),
      see `git log --grep=paste-perf-and-drag-drop`. Upstream issue
      [#2704](https://github.com/dankamongmen/notcurses/issues/2704) has been open since
      2023 with a maintainer-endorsed design sketch from `tstack` (`lnav`'s maintainer)
      that was never turned into a PR — our own patch deliberately took a simpler shape
      (no `fbuf`/`paste_content` field inside Notcurses itself; all buffering happens in
      `EventLoop::Run()`, which already owns the right place for it), so it isn't a
      drop-in match for that sketch if we ever revisit upstreaming this specific gap.

### Known Test Flakiness / Non-Critical Issues (Watch List)

Real, reproduced, non-urgent — each is safe to leave as-is for now, but worth fixing
opportunistically rather than re-discovering from scratch. Add to this list instead of
just fixing-and-forgetting or letting it fade from memory between sessions. Fixed entries
are removed once shipped rather than kept as a writeup here — see `git log --grep=flak`
for closed-issue history.

As of 2026-09-08: `ctest -j8` is clean under both the `default` and
`sanitize` presets, and so is the single-process `./build/ned_tests` (see the build/test
note at the end of this file for why that is a separate check worth making). One
documented behavioral limitation, not a flake:

- A save of a file with more than one hard link writes that file's own inode in place
  (`Text/FilePreservation.h`'s `ShouldWriteInPlace`) rather than taking the usual
  temp-file-then-rename path, because a rename always produces a new inode and would
  leave every other link on the stale content. That trade costs the atomic path's crash
  protection for those files specifically: a crash or a full disk mid-write can leave one
  truncated, recoverable only from the `Editor/Backup.h` version written moments earlier.
  `ned/set-preserve-hard-links-on-save false` opts back into the atomic path (and back
  into breaking the link). Fixing this properly would need a write-then-relink scheme that
  POSIX doesn't really offer; not worth building until someone actually hits it.

### Named Non-Goals (Leaning "Won't Do", Kept Visible So It's a Conscious Call)

- [ ] A plugin marketplace/package registry (VSCode extensions, MELPA/straight.el).
      Ned's model is one Janet-scriptable environment plus opt-in project-local plugins
      gated by `ProjectTrust`'s hash-based trust registry — a marketplace implies a
      supply-chain-trust problem this project has deliberately stayed out of.
- [ ] A single fuzzy command palette unifying M-x/find-file/switch-buffer into one popup
      (VSCode/Sublime's Cmd+Shift+P). Real Emacs keeps these as separate, purpose-built
      commands with their own bindings — consistent with this project's Emacs-class-
      parity vision, so this reads as a different, already-chosen philosophy.

### Native Windows Port (Idea, Unstarted — Design Sketch Only)

Raised alongside the system-clipboard work (`Editor/Clipboard.h`'s WSL detection covers
running ned as a Linux binary under WSL today, shelling out to `clip.exe`/
`powershell.exe` over WSL's own interop — already shipped). A *native* Windows build
(running directly under Windows Terminal or a raw console host, no WSL layer) is a
distinct, much larger effort: this codebase is POSIX throughout, not just at one or two
call sites, so "port" means replacing the platform layer wholesale:

- **Process spawning** (`Editor/Process/ChildProcess.h`, `posix_spawn` + `pipe`) needs a
  `CreateProcess`/anonymous-pipe implementation behind the same `WriteAll`/`ReadSome`/
  `WaitForExit`/`Kill` surface — every LSP/DAP/ACP/task/VCS subprocess integration in
  `Editor/` is built on this one class.
- **The embedded terminal panel** (`Editor/Terminal/PtyProcess.h`, `forkpty`) needs
  ConPTY (`CreatePseudoConsole`) instead — a real API, but a different threading/
  handle-lifetime shape than a POSIX pty fd pair.
- **Raw terminal I/O** (`UI/TerminalColorProbe.h`'s `termios`/`poll` raw-mode probe, and
  OSC 52) needs the Win32 console API or, on a recent-enough Windows Terminal, VT
  passthrough.
- **Notcurses itself** would need to build and run against the Win32 console/Windows
  Terminal target — worth checking Notcurses' own upstream platform support before
  committing, since ned's `UI/` layer sits directly on it with no abstraction gap.
- A PowerShell-flavored bundled theme would be a small addition once the port exists —
  `UI/ThemeRegistry.h`'s fixed name→factory table is exactly the extension point.
- **LSP broker self-staleness detection** (`Editor/Lsp/LspBrokerMain.cpp`'s executable-
  identity check) is Linux-specific (`/proc/self/exe`, and depends on rename-over-a-
  running-binary being legal at all — the exact thing that lets a rebuild replace
  `build/ned` while the broker daemon still has the old inode mapped). Windows
  generally can't do that swap in the first place — the OS locks a running executable's
  file, so a rebuild while the broker is up would fail outright rather than silently
  going stale. A native port needs a different mechanism entirely (or may not need one,
  if Windows' own lock makes the failure mode "rebuild fails with a clear error"
  instead of "silent staleness").

Unscoped beyond this sketch — process spawning is the obvious dependency root; nothing
else works without it.

## Maybelist (Speculative — Neither Committed nor Rejected)

Ideas worth remembering but not worth scoping yet — too undecided for "Open Items",
not disliked enough for "Won't do". Promote or delete on revisit rather than letting
these accumulate detail in place.

- [ ] **Merge-aware cross-session undo** — persistent undo (`Editor/PersistentUndo.h`,
      shipped 2026-08-25) content-gates: on reopen, restores the full tree only if the
      file's current on-disk content exactly matches some node already in the persisted
      tree (any node, not just the tip — covers "quit without saving" for free); no
      match at all just discards the persisted history outright and the buffer starts
      fresh. The fancier version would three-way-merge a genuinely novel external change
      into the persisted history instead of discarding it (base = last-persisted content,
      ours = tree tip, theirs = fresh disk content), reusing `Text/ThreeWayMerge.h`/
      `Editor/AutoMerge.h`'s existing machinery to splice one merge node onto the old tip.
      Deferred because it means synthesizing an undo node for content the user never
      actually typed — a real risk of `undo` doing something surprising later — worth it
      only if the content-gate default proves too lossy in practice.
- [ ] **AI edit-prediction** (Zed's Zeta: predicting the next multi-line edit from
      cursor/edit history, distinct from LSP-driven completion or ACP's chat) has no
      equivalent here. A different feature from everything `Acp/` already provides, and
      probably needs some model-serving backend of its own — worth naming as a conscious
      gap rather than assuming ACP already covers "AI in the editor." Sketched further
      2026-09-07, still unscoped:
      - **The backend is the real fork.** Reusing `Editor/Acp/` costs no new
        infrastructure and already talks to a live agent, but ACP is conversational
        request/response — latency is seconds, which is fine for an explicit
        "predict my next edit" command and wrong for anything that fires as you type. The
        alternative is a dedicated fast path (a small HTTP client, or a local model), which
        gets the latency but adds a subsystem plus a credentials/config story ned doesn't
        have today. Likely order: prove the predictions are worth anything via the cheap
        explicit-command-over-ACP version *before* building latency infrastructure for
        them.
      - **Two pieces already exist.** `Text/LineDiff.h`'s `UnifiedDiff` (built for the ACP
        edit preview) is exactly the recent-edits-as-a-diff prompt input this needs, and
        `UndoTree` supplies the history behind it.
      - **The genuinely new work is rendering.** Nothing draws a multi-line inline
        suggestion — ghost text was removed from completion in favor of `ListPopup`
        (`completion-popup`), so a multi-line ghost overlay is a new `BufferView` paint
        path, not a revival of the old one.
- [ ] **Open-source game-dev platform support** (raised 2026-09-03, explicitly a list to
      pick from, not a commitment to any of it). The real fork in each candidate is
      whether it needs a *new base language* ned doesn't speak yet, or whether it rides
      on one already planned above — worth deciding by language, not by engine:
      - **Godot** (GDScript, optionally C# via Mono) — the most popular open-source
        engine, so the strongest adoption case. Needs a GDScript tree-sitter grammar (a
        community one exists; verify current maintainer/repo before wiring it in, same
        caveat as Kotlin's above) plus the same highlight/fold/indent/tags checklist.
        Real open question, not yet verified live: Godot 4's built-in GDScript language
        server reportedly speaks LSP over a TCP socket to a *running Godot editor
        instance*, not as a spawned stdio subprocess — if true, that's a genuine
        mismatch with `Lsp/Transport.h`'s subprocess-+-pipes assumption and would need a
        real transport-layer addition (a raw-socket `Transport`), not just a
        `ned/set-lsp-command` entry. Confirm before scoping.
      - **Bevy** (Rust, no visual editor — code-first ECS) — needs nothing
        Bevy-specific; general Rust language support shipped 2026-09-04, so this is now
        just `rust-analyzer` + `lldb-dap`/`codelldb` config, the same as any other
        already-bundled language.
      - **LÖVE (Love2D)** and **Defold** (both Lua-based, open source, no bundled mode
        for Lua at all today) — gated on general Lua support, not engine-specific work.
        `lua-language-server` already understands both frameworks' APIs via
        community-maintained meta/addon files.
      - **C#** (needed for Godot-via-Mono) — bundled language support shipped, see the
        Language Intelligence section above; `OmniSharp`/`csharp-ls` are the LSP options,
        config-only, not touched by that work.
      - **GDevelop** — event-based, largely no-code; not a natural fit for a text editor
        regardless of open-source status. Listed only to record it was considered and
        set aside.
      - **Game-dev debugging**: Bevy (Rust) and LÖVE/Defold (Lua, via
        `local-lua-debugger-vscode`) both debug through the ordinary
        `ned/set-dap-adapter` mechanism once their language support lands, nothing
        game-specific needed; Godot/GDScript is the one real unknown — verify whether
        Godot 4 exposes a real DAP-speaking debug adapter at all before assuming this is
        just a config entry (may share the LSP transport quirk noted above).
        Collaboration/multi-client synced debugging (a bespoke sketch-annotation,
        synced-viewing feature seen in some existing GDB frontends) doesn't fit ned's
        single-user terminal model — considered and set aside, not planned.
- [ ] **LSP broker "server mode"** (raised 2026-09-06, following the fileOperations
      capabilities fix and its live fallout) — today's `Editor/Lsp/LspBroker*` daemon
      always self-terminates ~1 minute after its last attached client disconnects
      (`LspBrokerMain.cpp`'s `kWholeDaemonIdleTimeout`), specifically so a stale process
      never outlives a `ned` binary rebuild for long: `BrokerRouter` caches one real
      `initialize` handshake result — success *or* failure — per `(root, language)` key
      for its own process lifetime, and a live bug showed this can otherwise strand every
      future attacher on a failure cached from a client-capabilities bug that was already
      fixed and rebuilt. A real "server mode" (deliberately kept warm regardless of
      client presence — e.g. a systemd user service, so a fresh `ned` launch never pays
      even the broker's own startup cost) would disable or greatly lengthen that idle
      timeout, which reopens exactly this staleness risk on a much longer timescale.
      The prerequisite this entry used to list — the daemon noticing its own on-disk
      executable changed and restarting itself — is **already built** (`/proc/self/exe`
      device/inode/mtime, re-checked on the idle sweep; watched firing live 2026-09-07),
      so server mode no longer needs it designed, only kept working. Not scoped further
      than that; no server-mode design exists yet.

Also shipped, one slug each for `git log --grep=`: `broker-reader-deadlock` (that same
daemon deadlocking in its own idle sweep — the bug is why `Tests/LspBrokerDaemonTest.cpp`
and the daemon's ASan coverage exist at all, both of which any server-mode work should
build on), `code-coverage-gutter`.

- [ ] Raw per-file `.gcov` output has no parser — `Editor/Coverage/CoverageOutputParser.h`
      handles lcov's `.info` format only (which covers `lcov`, `llvm-cov export
      -format=lcov`, and `gcovr --lcov`). `.gcov` is a directory-scan problem rather than
      the single-document parse every other `Editor/*OutputParser.h` does, so it was left
      out deliberately; revisit only if `.info` proves insufficient in practice.

## Won't Do (at Least Not Soon)

- **Org Babel** — subsystem-sized, and arbitrary code execution triggered by opening a
  text file: a security surface to design around deliberately, not bolt on. If this
  ever gets revisited, the rich-output-rendering half of "Jupyter Notebooks" above
  (table/image/text cell rendering) is the natural substrate to reuse rather than a
  second bespoke renderer — see that section's own note.
- **Org table formula/spreadsheet engine** — a small programming language of its own.
- **Org export backends / publishing pipeline** — each a standalone tool-sized effort.
- **Alt+Click cursor creation** — mouse-dependent in a terminal app where SSH/tmux
  mouse passthrough is unreliable; keyboard multi-cursor commands cover it.
- **X11 primary-selection support for middle-click paste** — `Editor/Clipboard.h`'s
  `PasteFromPrimarySelection`/`ResolvedPrimarySelectionPasteCommand` are deliberately
  Wayland-only (`wl-paste --primary`), a stated user preference given X11's declining
  share; no xclip/xsel `-selection primary` fallback.
- **Bundling or requiring a JVM** for anything (heavy checkers like `ltex-ls` stay
  opt-in user configuration).
- **Accessibility (screen-reader support)** — a Notcurses raw-cell-grid TUI has no
  accessibility tree of any kind; not evaluated or pursued. Named here so it's a
  conscious gap rather than an oversight (2026-08-25 audit).

## Notes for Whoever Builds Next

- Build/test: `cmake --preset default && cmake --build build`, then
  `ctest --test-dir build`. Sanitizer opt-in: `-DNED_ENABLE_SANITIZERS=ON` with
  `-DCMAKE_BUILD_TYPE=Debug` — the suite is expected clean; a finding is a real bug.
- `ctest` and `./build/ned_tests` are two genuinely different checks, not a convenience
  pair — run both before calling a change clean. `ctest` gives each case its own process
  (isolation, plus `--timeout N` names a hang instead of wedging the run, and `-j8`
  surfaces cross-process races over shared paths); the single-process binary is the only
  thing that catches global-state bleed between cases, and it runs them back-to-back with
  no per-case process startup in between, which has surfaced timing races `ctest` shows as
  reliably green. Both flavors have been found here, and each mode missed the other's
  (2026-09-08: `ctest -j8` alone caught the protocol-client poll deadlock and never saw
  the LSP frame-count race; the single-process run was the exact inverse). Never run two
  `./build/ned_tests` binaries concurrently — they contend and wedge each other; use
  `ctest` for parallelism.
- `ned_tests` is deliberately hermetic against the terminal and the desktop it runs on,
  via static-initialized guard translation units that flip a process-wide switch before
  any case runs (`Tests/ClipboardTestGuard.cpp` → `SetClipboardEnabled`/`SetOsc52Enabled`,
  `Tests/TerminalOutputTestGuard.cpp` → `ned::ui::SetHeadlessOutputForTesting`,
  `Tests/ProseCheckerTestGuard.cpp`, `Tests/SigpipeTestGuard.cpp`). Anything new that
  writes to the real terminal, the system clipboard, or shells out to a desktop tool
  needs the same treatment — add a switch and a guard rather than fixing it at each call
  site, since a test that legitimately re-enables the feature locally would otherwise
  reintroduce the escape.
- When you finish an item above, delete it (or replace it with a one-line pointer) in
  the same commit — don't leave a `[x]` writeup behind. Keeping this file short is the
  point.
