# TODO

Committed work, and known issues worth fixing. When an item ships, delete it; the
writeup belongs in the commit message. The public picture lives in `ROADMAP.md`;
speculative and rejected ideas live in `MAYBE.md`.

A deliberate design cut with no trigger to reopen it is not a todo. It goes in the
commit message or a source comment, not here.

## Releases

1.0 is a promise about the *Janet surface* (`Register<>` bindings and command names), not
about features. Declaring it makes breaking any of them a major-version event. Criteria:
- [ ] Janet API frozen.
- [ ] No known data-loss paths.
- [ ] Release artifacts.
- [ ] The mdBook site (`Docs/book.toml`) published.

## Unverified Against Real Environments

- [ ] The six capability-gated DAP requests added with the debug panel (`modules`,
      `loadedSources`, `breakpointLocations`, `stepInTargets`, `setExpression`,
      `completions`) are unit-tested against a fake adapter only. Exercise them against a
      real lldb-dap and debugpy session. `breakpointLocations` must read an empty list as
      "no opinion".
- [ ] The GNOME Terminal handler in `TerminalTabLauncher` has never run live.
- [ ] `C-Tab`/`C-S-Tab` in the modern keymap are untested on a legacy (non-kitty-protocol)
      terminal.

## Editor & UI

- [ ] **Merge view: index-stage source.** Sides come from conflict markers today, so git's
      clean auto-merges aren't shown. Diff the `:1:`/`:2:`/`:3:` stages (a new Provider
      verb) against the merged buffer into the same `AlignedChunk` list. `DiffLines` is an
      O(m*n) LCS, so this needs a linear-space diff first.
- [ ] **The Files, VCS and Debug panels onto panel modes.** Their keys are still raw
      codepoint checks, so `ned/define-panel-key`, which-key and `describe-bindings`
      can't see them. The Remote panel's `remote-panel-mode` (`Editor/PanelMode.h`,
      `UI/PanelKeys.h`) is the pattern; the VCS panel's footer legend would read its
      keys from the mode rather than from fixed strings.
- [ ] **Right-docked `AcpPanel` has no reserved resize border**, unlike
      `ProjectSidebar`'s divider column.

## Janet Widget Surface

The tracker panels, buffer list and DAP thread panel now share `UI/TableView.h`. That
table is the evidence this stage was waiting for.
- [ ] **A declarative Janet panel API.** Janet returns a model and C++ paints it, the
      same split `ListPopupModel` and `JanetVcsProvider` already use. Never hand Janet a
      `Canvas`, because per-cell granularity is the wrong place for the scripting
      boundary. The deliverable is a model vocabulary: rows, columns, actions, key
      bindings, and a refresh callback. It falls under the 1.0 freeze the moment it ships
      as `ned/*`.

## Remote Development

Goal: edit files on a remote host over SSH without the editor running remotely,
comfortably enough that it doesn't feel like a degraded mode.

**Approach.** A thin, self-contained agent first, speaking `Docs/ClientServerProtocol.md`
over `ssh host ned-agent --stdio`. It does search, file I/O, change notification, and
hosts LSP/DAP/task/VCS processes against the remote toolchain. Locally, the same
dispatcher runs in-process. A real target host (Rocky 9, glibc 2.34, no compiler, no `rg`)
rules out both a dynamically linked agent and a client-only `rg` fallback. Edit locally
and sync on save; don't build a remote-authoritative buffer.

**Library split** — the agent links only what it needs. Measured by include closure,
2026-10-05.
- [ ] `Text/DisplayWidth.cpp` includes Notcurses, which blocks a clean text library. It
      doesn't block the agent.

**Agent**
- [ ] A CI job that builds both static agents (`agent-*` presets) and runs `[HostAgent]`
      against them through `NED_TEST_AGENT`: x86_64 natively, aarch64 under
      `qemu-aarch64`.
- [ ] Prune `<build>-<arch>` agent directories no editor has used in a while.
- [ ] The rest of the `proc` seam, per the design doc's staging step 4: terminal tabs
      over a pty, then LSP, DAP and ACP. Tasks, the test runner, VCS and the format
      command already run on a remote buffer's host.

**Client/server protocol**, ordered by the design doc's staging section.
- [ ] `ned --protocol-dump`: decode a captured byte stream to readable JSON.
- [ ] Verify a remote debug session end to end (PHP/Xdebug, with the adapter running on
      the host through `proc`).

**File I/O and editing**
- [ ] Remote saves over 48 MiB: `fs/write` carries the content in one request, so a
      bigger file needs a streamed upload. Decoding a read and encoding a save run on the
      main thread, which only matters at that size too.
- [ ] Project-registry roots accept `user@host:/path`.
- [ ] Restore point and scroll position for a remote buffer (save-place keys files by
      local path only).
- [ ] Confirm local and remote buffers mix in one window layout.

**Connect**
- [ ] Parse `~/.ssh/config` directly (`Host`/`HostName`/`User`/`Port`/`IdentityFile`/
      `ProxyJump`, wildcards) to fill in defaults.
- [ ] The remote prompts complete hosts (`Remote/KnownHosts.h`, `Remote/HostLookup.h`)
      and the path after them. Still to come: the project name ahead of the host.
- [ ] A destination with a port. `host:/path` can't carry one, so known_hosts entries
      on a port other than 22 aren't offered.

**Project tree and search**
- [ ] A file moved on its host isn't followed by the buffer holding it, as
      `WindowManager::HandleExternalMoves` does locally. The host reports the move as a
      delete and a create; `watch` would need to pair them (`FileWatch.h`'s `FileMove`).
- [ ] A watch burst the host lost events from (`overflow`) checks every remote buffer
      on that host but doesn't relist the Remote panel; `g` does.
- [ ] The Remote panel doesn't colour rows by their repository's status, as the project
      sidebar does locally.
- [ ] A remote test run's `*test results*` lines resolve their sources against the local
      tree (`TestRun/TestSourceResolver.h`); they should resolve on the host and open
      remote buffers.
- [ ] Send remote buffers' unsaved edits with a remote project search, as `LiveContent`.
      Unsaved text can exceed the 64 MiB frame cap in total, so it needs to go up as a
      stream rather than inside the request. Decide how project-replace, find-references
      and the agenda scan run against a remote root.

## Documentation & Companion Tooling

- [ ] **Doxygen developer docs**, themed with Doxygen Awesome, as an opt-in `docs` target
      through `doxygen_add_docs()`. It is not part of `ALL`.
- [ ] **`ned-setup`**: first-run detection of shell integration, language servers and
      debug adapters, generating an editable Janet file loaded from `init.janet`.
      Deliberately a generator rather than silent runtime detection.
- [ ] **Cookbook entries for tools that already work with no new code**: Valgrind
      (`--vgdb=yes --vgdb-error=0` + DAP attach), and Docker/embedded/OpenOCD targets
      through `ned/set-dap-adapter`/`ned/set-dap-attach`.

## Notcurses Patches to Upstream

`Patches/notcurses/` carries each patch as a `git am`-able file (regenerated by
`regenerate.sh`, verified against pristine v3.0.17).
- [ ] `PatchNotcursesNulKey` (Ctrl+Space swallowed as NUL) and `PatchNotcursesMouseWheel`
      (SGR wheel-right misdecoded). Both reproduce on upstream `master` as of 2026-09-06
      and have no existing issue. They are ready to PR.
- [ ] `PatchNotcursesBracketedPaste`, for upstream
      [#2704](https://github.com/dankamongmen/notcurses/issues/2704). Our shape buffers in
      `EventLoop::Run()`, not in Notcurses, so it doesn't match tstack's sketch there.

## Known Issues / Test Flakiness (Watch List)

Real, reproduced, non-urgent. Fix opportunistically; delete when fixed.

- **F# grammar**: application and infix share `prec-left 16`, so `1 + f a` parses as
  `(1 + f) a`. Also, `let f x y = ()` without a trailing newline doesn't parse as a
  function.
- **Markdown emphasis on a pathological paragraph.** Each unclosed opener reads ahead up
  to 4096 codepoints: 15,000 of them in 45 KB take 0.65 s. A closer-free memo in the
  scanner state would fix it if a real document hits this.
- **`BufferView's highlight cache updates after an edit…` flakes under `--order rand`**
  (about 1 in 10–15 runs). The test's *first* assertion fails: the first `Paint()`
  renders a JSON string as `Default`. It smells like a parse-readiness race
  (`ModePrewarmTest.cpp` precedent) and hasn't been root-caused.
- **Notcurses and the terminal can disagree on glyph width**: VS16 emoji, and Nerd Font
  private-use glyphs under tmux. Nothing has been seen in Konsole. A startup
  cursor-position probe could calibrate an exception table.
- **`search-everywhere debounces a background text search…` flakes under `ctest -j8`**.
  It's a timing margin under contention; pin the debounce to a fake clock if it
  resurfaces.

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
