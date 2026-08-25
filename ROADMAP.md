# Ned Roadmap

What's still open. Completed work is deliberately not tracked here — the detailed
per-feature design/decision records this file used to carry were pruned 2026-08-20 and
live in git history (`git log --follow ROADMAP.md`, `git show <rev>:ROADMAP.md`);
current architecture is documented in `CLAUDE.md` (note: some of its "see ROADMAP.md's
X entry" pointers refer to those pruned records).

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
- **Keep `Source/UI/` loosely coupled from the TUI library where it's cheap.**.

## Open Items

### Embedded Language

- [ ] **Jank replaces Janet** - Once I'm able to do so, I'd love to replace our
      internal scripting representation to move to [jank](https://github.com/jank-lang/jank)

### Language Intelligence

- [ ] **Embedded-language documents** (HTML with inline `<script>`/`<style>`,
      Vue/Svelte-style SFCs) — segment the buffer into per-language virtual documents
      (tree-sitter injection queries find the boundaries), sync each to its own
      server, remap positions back. `LspManager` gained real multi-server-per-buffer
      diagnostics merging with the prose-checking feature (harper-ls as a second,
      independent diagnostics channel alongside the primary language server) — that
      two-server shape is a fixed pair keyed by a reserved language key, not yet a
      general N-server-per-buffer mechanism this would need.
- [ ] Per-capture highlighting round 2 (v1 shipped, exhaustive-highlighting
      follow-up: enumeration of all 17 bundled queries' 87 capture names, defaults
      closing every gap found, `HighlightSpan` carrying an interned capture id, a
      dotted-name-inheritance override store — capture chain beats `SyntaxClass`
      override beats built-in theme, field by field — capture→class remapping, and
      the `ned/set-capture-*`/`ned/capture-names` Janet surface). Deliberate cuts:
      **language-scoped rules** (an explicit decision — one rule set for all
      languages; the resolution walk in `SyntaxTheme.h` is the seam a
      `<lang>/<name>` tier would prepend to later, and markdown's hardcoded
      `punctuation.special` special case in `Mode.cpp` is the first candidate to
      migrate onto it), per-capture styling in the Minimap (class-level only
      there), and theme-file serialization of capture overrides.
- [ ] Go-to-file-at-point for import/include directives (v1 shipped:
      `Mode::importTarget`, a tree-sitter-query-driven capability mirroring
      `Mode::fold`/`expandSelection`'s "one function pointer per capability"
      shape — every bundled language with a real import/include construct has
      its own small `*-imports.scm` query, `RegisterDynamicMode` takes the
      same query file for a runtime-loaded grammar). Still open: LSP as a
      first-choice resolver ahead of the hand-rolled one where a server can
      answer it (clangd supports `textDocument/documentLink` for `#include`);
      Python's leading-dot relative imports (`from . import x`); PHP's
      namespace `use` (needs a PSR-4 autoloader parse, out of scope for the
      hand-rolled resolver); JS's dynamic `import(...)`; node_modules
      `package.json` main/exports resolution beyond `index.*` inference;
      Rust when it gets a bundled mode at all.

### Navigation & search

- [ ] **Multibuffers** — `Editor/Multibuffer.h`'s stitching primitive shipped. Three
      consumers so far: `vcs-full-diff-buffer` (every changed file's real diff hunks,
      stays read-only -- see below), `lsp-diagnostics-buffer` (every open buffer's
      Code-origin LSP diagnostics, one excerpt per diagnostic, reusing the ordinary
      diagnostic gutter/underline/severity-color pipeline via real composite-space
      `Buffer::Diagnostic` entries rather than a new tint), and `project-find-references`
      (`M-?`/`ESC ?` -- one excerpt per whole-word RE2 match for the identifier at point,
      across the project; a fast textual approximation, not real semantic LSP
      references, which still don't exist as a client capability -- see the
      `.gitignore` item below for where the RE2 engine it's built on came from). Every
      consumer shares the same jump-to-source path: `vcs-visit-result` (`C-c v v`) and
      `project-search-visit-result` (`C-c C-v`) stayed as separate commands/bindings
      (existing keybinding/Janet-name compatibility) but now both delegate to one shared
      `BufferView::VisitResultUnderPoint` -- the "confirmed confusing in practice" gap
      (either chord silently doing nothing depending on which results buffer you were in)
      is closed: `MultibufferIndexFor` is tried first, falling back to the `path:line:`
      regex, so both chords -- and Enter/click on any read-only results buffer, which
      already funneled through `VisitSearchResult` -- now behave identically in every
      results-style buffer (plain search, diff, references, diagnostics).
      **Editable excerpts** (shipped 2026-08-24, wgrep-style writeback, closing the
      "making a multibuffer genuinely editable" line this entry used to carry) --
      `lsp-diagnostics-buffer`/`project-find-references` excerpts are now genuinely
      typable and commit back to their real source buffer via `C-c C-c`
      (`multibuffer-commit-changes`); `vcs-full-diff-buffer` and the agenda/clock-report
      multibuffers stay read-only, a deliberate cut (diff hunks mix +/-/context lines --
      real patch-apply semantics, a differently-shaped problem; agenda/clock-report
      excerpts are synthetic summary text with no 1:1 source-byte mapping). Built on a
      new `Buffer`-owned relocated tracked field, `Buffer::ExcerptRange` (the sixth
      sibling to `SnippetRange`, same "Buffer has no idea what this means" split as
      `FoldMarker`/`SnippetRange` have with their own owning subsystems) -- point-level
      editability enforcement lives inside `Buffer`'s five content-mutation entry points
      themselves (a silent no-op outside an editable range, not a thrown error --
      `ForEachCursor` has no per-cursor failure protocol, so a multi-cursor edit with
      some cursors in chrome just produces no change for those), and `Undo()`/`Redo()`
      deliberately do NOT clear `ExcerptRanges_` the way `SnippetRanges_`/
      `SecondaryCursors_` do -- an emptied set would silently drop chrome protection on a
      buffer whose text is still sitting there mid-edit, so it's relocated instead, the
      same `ChangedByteRange`-diff-composed-onto-relocation shape
      `UpdateUnsavedRangesForRestore` already uses for `UnsavedChangeRanges_`.
      `CommitExcerptChanges` (`Editor/Multibuffer.h`, a free function, not a `Buffer`
      method -- keeps `Text/Buffer` unaware of multibuffer semantics) diffs each
      editable excerpt's current text against a snapshot captured at build time,
      batches changed excerpts per source file in descending-source-byte order
      (`BufferView.cpp`'s existing `ApplyWorkspaceTextEdits`, LSP code-actions/rename's
      own precedent, for the same "don't invalidate a not-yet-applied edit's stored
      offset" reason), wraps each source file's writes in one undo group, and skips
      (with a per-file reason, never aborting the rest of the commit) a source whose
      `ExternallyModified()` trips or whose live bytes at the excerpt's own source range
      no longer match the snapshot -- deliberately a direct byte-range comparison rather
      than a whole-buffer `ContentGeneration()` snapshot, so an unrelated edit elsewhere
      in the same source buffer never false-positives. Never writes to disk -- leaves
      that to the user's own `save-buffer` on whichever source buffers came out
      modified, matching wgrep's own scope. Still open: fuller VCS history views (e.g. a
      full commit's diff from `*vcs log*`, not just the working tree), a result
      cap/warning for `project-find-references` on a very common short identifier (no
      limit today -- thousands of matches would build a proportionally huge composite
      buffer), and `VisitResultUnderPoint`'s jump-to-source staying line-granularity
      (column 0 only) even though `ExcerptRange` now carries the byte-exact source range
      that would let it preserve the intra-line column.
- [x] **`.gitignore` correctness gap left by dropping `rg`** (shipped 2026-08-24) --
      `GitIgnore.h`'s `GitIgnoreMatcher` (the shared walk filter behind
      `ProjectSearch`/`ProjectReplace`/`ProjectTree` and the find-all-references
      multibuffer consumer) is no longer root-`.gitignore`-only: it now consults the
      same sources real git does, in git's precedence order -- the global ignore file
      (`core.excludesFile` resolved via a minimal INI scan of `~/.gitconfig`/
      `$XDG_CONFIG_HOME/git/config`/`$GIT_CONFIG_GLOBAL`, defaulting to
      `$XDG_CONFIG_HOME/git/ignore`), `<gitdir>/info/exclude` (a `.git` *file*'s
      worktree `gitdir:` pointer followed), and nested `.gitignore` files (lazily
      loaded per queried directory -- since both walkers prune ignored directories,
      a `.gitignore` inside `node_modules/` is never even read; each file's patterns
      match relative to its own directory, deeper overriding shallower). Global/
      info-exclude only apply when the root has a `.git` entry, matching git itself.
      The glob translator also gained real `**` semantics and character classes
      (`[a-z]`, `[!abc]`) while it was open. `CachedGitIgnoreMatcher`'s staleness
      check widened from "root `.gitignore` mtime" to `AnySourceChanged()` -- one
      stat per consulted file, absence recorded too, so a nested `.gitignore`
      appearing later still invalidates. Deliberate cuts: backslash escapes
      (`\#foo`) and POSIX named classes (`[[:alnum:]]`) in patterns (both rare);
      `IsIgnored` answers for the queried path only, relying on the walkers'
      directory pruning rather than re-checking every ancestor per file.
- [x] **PCRE2 for in-file regex matching/replacing** (shipped 2026-08-24) --
      `Editor/RegexPattern.h/.cpp`, a pimpl'd RAII wrapper (pcre2.h stays off the
      public include surface, RE2's own privacy treatment) now backing both
      `Editor/QueryReplace.cpp` and `Editor/ProjectReplace.cpp`'s `ReplaceMatches`,
      the last two user-facing "type your own regex" surfaces that were on
      `std::regex` (everywhere else `std::regex` remains -- `GitIgnore.cpp`,
      `Link.cpp`/`Org.cpp`, `TreeSitter/Query.cpp`, `BufferView.cpp`'s result-line
      parsing -- is internal fixed-pattern plumbing, deliberately untouched).
      Compiled `UTF | UCP | MULTILINE | MATCH_INVALID_UTF` with an LF newline
      convention: `^`/`$` anchor at line boundaries (what an editor user expects,
      and what makes project-replace's per-line search preview agree with its
      whole-file rewrite), `\w`/`\b`/`\p{...}` are Unicode-aware, and stray invalid
      bytes in a "text-looking" file are tolerated rather than an error. JIT'd via
      `pcre2_jit_compile` (silent interpreter fallback), with
      `pcre2_set_match_limit` (1M steps, tighter than PCRE2's 10M default) as the
      catastrophic-backtracking safety net -- a trip throws `RegexPatternError`,
      which `BufferView`'s handlers surface as a status message ending the session
      rather than a crash or a frozen UI. Two structural bug fixes came free:
      `QueryReplace`'s documented ^-anchoring limitation (it searched trimmed
      subranges; `RegexPattern::Search` takes a start offset over the whole
      subject, so `^`/`\b`/lookbehind see preceding context), and replacement
      expansion is now hand-rolled (`FormatReplacement`) preserving the
      ECMAScript `$1`/`$&`/`` $` ``/`$'`/`$$` set `std::smatch::format` provided
      while adding `${name}` named groups. Zero-width-match stepping advances a
      whole codepoint (new `Text/Utf8.h` `NextCodepointBoundary`), never
      mid-UTF-8-sequence. The old "RE2-validated pattern can still throw at
      rewrite time" split-engine gap is practically closed (PCRE2 accepts
      essentially everything RE2 does); the reverse constraint is documented and
      stands: lookaround/backreferences work in single-buffer
      `query-replace-regexp` but not project-replace, whose preview is RE2's
      linear-time engine by design. Deliberate cuts: no `\1`-style Emacs
      backreference syntax in replacements (kept `$`-style from the std::regex
      era), no user-configurable match limit, and `IncrementalSearch` stays
      literal-only (regex isearch was never in scope).

### Large files

- [ ] **Windowed/paged editing for genuinely huge files** (multi-GB). Not a tweak to
      the async loader — `Rope` and everything on it assume fully-resident content.
      Recommended v1 shape: a **read-only mmap-backed viewer** past a size threshold,
      lazily building a line-offset index only near the viewport, with an explicit
      "load fully to edit" step reusing the async loader. In-place windowed *editing*
      needs a disk-backed piece table — most of a new engine, explicitly not v1.
- [ ] Loose end: buffers restored by a project session open before the async-loader
      hook is wired, so a huge file inside a restored session still loads
      synchronously at startup.

### Editor ergonomics

- [ ] Terminal panel round 2 (v1 shipped: libvterm-backed drawer over the
      `OverlayHost` floating-widget layer, `toggle-terminal` on `` C-` ``/`C-c t`,
      title-row `[▼]` minimize/`[▲]` maximize/`[×]` close mouse buttons alongside
      the keyboard toggle, 2000-line scrollback via Shift+PageUp/Down,
      respawn-on-Enter after exit; `C-c t` on a visible panel hides rather than
      focuses — the keyboard-complete cycle on legacy-encoding terminals where
      `` C-` `` never arrives, a real live-use report). Deliberate v1 cuts: no
      drag-resize of the drawer height (needs overlay mouse-capture semantics;
      height is Janet-configurable via `ned/set-terminal-height-percent`
      instead), no scrollback search/selection/copy, no multiple terminals/tabs,
      no terminal-side mouse forwarding to the shell (clicks focus the panel,
      wheel scrolls the ring — TUI apps inside the terminal don't receive mouse
      events), and no OSC 52/title integration.
- [ ] **Remote development** (SSH remote editing).

### Collaboration & AI

- [ ] **AI-assisted editing** (inline completion, chat with codebase context) — a
      Janet-scriptable Agent Client Protocol (ACP, Zed's open agent/editor standard)
      integration, `Editor/Acp/` (v1 shipped): `Transport` (newline-delimited JSON-RPC,
      distinct from `Lsp`/`Dap`'s shared `Content-Length` framing — the one real wire-
      level difference from those siblings) + `AcpClient` (bidirectional, with an
      async-capable agent→client `RequestHandler` LSP's synchronous-only one couldn't
      model) + `AcpManager` (single-session handshake, `fs/read_text_file`/
      `fs/write_text_file` bridged into real buffers reusing `AutoRevert`/`AutoMerge`'s
      own Revert/MergeExternalChanges gating, `session/request_permission` as a
      numbered-choice prompt mirroring `LspCodeActionSelect`) — same client/manager
      split as `LspManager`/`LspClient`, built directly on `Process/ChildProcess`
      exactly as anticipated. `acp-start-session`/`acp-send-prompt`/`acp-stop-session`
      (`C-c a s`/`C-c a p`/`C-c a k` — unreachable by typing, see below;
      `M-x`/Janet only for now) stream a session into a plain read-only
      `"*acp: <agent>*"` buffer — `TaskRunner`'s own output-buffer convention, kept
      alongside the real chat panel below rather than replaced. A real dockable chat
      panel shipped as a follow-up (`UI/AcpPanel.h`, an `OverlayHost` overlay, bottom by
      default/right via `ned/set-acp-panel-dock`, `acp-toggle-panel` on `C-c c`), built
      on a new structured transcript (`AcpManager::Transcript()`, `Buffer::Diagnostic`-
      style wholesale-replace + generation counter) rather than the flat output buffer.
      Deliberate cuts, still open: permission-prompt *resolution* stays in `BufferView`'s
      existing echo-area flow, the panel only displays the pending prompt; no scrollback
      in the panel (same v1 cut `TerminalPanel` itself has); a completion-popover
      replacement for ghost text, an M-x dropdown, and code-action lists currently
      squeezed into the one-row EchoArea are still unbuilt (the panel didn't turn into a
      general popover host); `terminal/*` tool-call support (spawning real
      `ChildProcess`/pty-backed terminals on the agent's behalf) and `elicitation/create`
      structured forms — both left undeclared as client capabilities, so a
      spec-compliant agent won't invoke them, rather than answered badly; multiple
      concurrent agents/sessions; `session/load` history replay;
      `session/set_config_option`/`session/set_mode` surfaced to the user; MCP server
      passthrough (`session/new`'s `mcpServers` is always sent as `[]`). The exact
      `session/update` sub-schema (message-chunk/tool-call/plan discriminated union)
      still isn't pinned down here against the authoritative ACP JSON schema, only
      informally documented at the time this was written — `AcpManager::
      HandleSessionUpdate`'s defensive, best-effort parsing (now including a `"plan"`
      branch, previously silently dropped) is expected to need widening once exercised
      against a real agent. Also newly found, keymap-collision follow-up: `Keymap::Resolve`
      returns a `Match` the instant a node's own command is set, before consulting its
      children, so any `C-c a <x>` binding was unreachable by typing once `C-c a` itself
      was bound to `org-agenda` — confirmed live, affected `acp-start-session`/
      `acp-send-prompt`/`acp-stop-session`; `acp-toggle-panel` avoided the same trap by
      binding `C-c c` instead. Worked around by moving the three ACP session commands to
      `C-c A s/p/k` (shifted "A", distinct from plain "C-c a"), and
      `Keymap::AmbiguousBindings()` (walks the trie for any node holding both a `command`
      and non-empty `children`) now guards the shipped default/Org/Markdown keymaps via
      `CommandsTest.cpp`'s "shipped keymaps have no unreachable-by-typing bindings" test
      — a future collision like this fails `ctest` instead of needing another live tmux
      session to find. Still open: `AmbiguousBindings()` is diagnostic only, not
      enforcement — `Keymap::Bind` still lets a caller construct this shape, it's just
      caught by the regression test for the keymaps that ship today. A real structural
      fix (Emacs' own `define-key` semantics: reject/restructure a bind that would shadow
      an existing command with a longer sequence, or vice versa) would change `Bind`'s
      signature across every call site including `ned/define-key` (would need to surface
      as a catchable Janet error for user init.janet scripts) — worth doing eventually,
      deferred as its own change since it's a multi-file API change, not a one-off
      collision fix.
- [ ] **Real-time collaborative editing** (CRDT-based) — the biggest lift in this
      file; last.

### Documentation & companion tooling

- [ ] **Documentation framework** — man page(s), PDF, and a web page generated from
      one shared source (pandoc is the direct fit), mining the `ned/*` binding doc
      strings already passed to `Register<Fn>`. A build/release-time step, nothing
      ned does at runtime.
- [ ] **Environment setup tool** (`ned-setup` or similar) — first-run detection: shell
      integration, plus scanning the system for installed tree-sitter grammars and
      *generating an editable Janet file* loaded from `init.janet`. Deliberately a
      standalone, inspectable generator — silent runtime auto-detection was considered
      and rejected (system grammar layouts aren't portable).
- [ ] **Tree-sitter-assisted formatter** with JetBrains-level per-rule configurability
      ("a dprint clone that is actually awesome") — a substantial project per
      language, not a utility. Its highlighting groundwork (exhaustive capture
      enumeration + per-capture identity) shipped with the exhaustive-highlighting
      follow-up; scope it once concrete gaps left by external formatters are known.

### Small loose ends

- [ ] **Vim-mode follow-ups** (shipped 2026-08-25, `Source/Editor/Vim/`, default off via
      `ned/set-vim-mode`) — Normal/Insert/Replace/Visual(char/line/block)/command-line
      modes, operator+motion+text-object composition, registers (unnamed/named/numbered
      ring/blackhole/system-clipboard), `.` dot-repeat, `q`/`@` macros, marks, PCRE2-
      backed `/`/`?`/`:s`, and ex commands (`:w`/`:q`/`:wq`/`:d`/`:normal`/range syntax)
      all work end-to-end (tmux-verified live). Deliberate v1 cuts, not oversights: no
      jumplist/changelist (`C-o`/`C-i`), no `:g` global command, no `ge`/`gE` (backward
      word-end), no mark letters beyond `a`-`z`/`'<`/`'>`, no `gv`, macros record raw
      keystrokes rather than vim's own editable-register-text form, `.` replays verbatim
      (a count typed before it doesn't override the recorded one), `>`/`<`/case operators
      don't apply to a Visual Block selection (only `d`/`y`/`c`/`x`/`I`/`A` do), and
      search/`:s` patterns are passed straight to PCRE2 rather than translating vim's own
      default "magic" escaping convention (closer to vim's `\v` very-magic mode than its
      default) — revisit any of these if they turn out to matter in practice.
- [ ] **`libned` as a real shared library** — `ned_lib` (static today) exists solely so
      `ned_tests` can link real editor code without pulling in `main()`; a static lib
      already does that job. Worth revisiting only if a second real consumer shows up
      (an embedding use case, a separate CLI tool, ...) — would need symbol-visibility
      curation (everything's exported by default today) and SONAME/ABI-versioning
      discipline, neither of which pays for itself with zero external consumers.
- [ ] Hunk unstage matches point against the *cached* staged diff, which drifts when
      unstaged edits exist earlier in the file — exact in the common
      stage-then-undo flow; revisit only if it bites.
- [x] **Subprocess hang/timeout protection** (2026-08-24 audit, shipped 2026-08-24;
      Janet-configurable timeouts + user-facing hang affordance shipped 2026-08-25) —
      `Editor/Process/ChildProcess.h` gained `WaitReadable`/`ReadSome(timeout)`
      (poll()-based), the shared primitive three independent fixes build on, each
      matched to what it actually protects against rather than one bolted-on read
      timeout: (1) `Clipboard.cpp::PasteFromSystemClipboard`/
      `ToolchainIncludePaths.cpp::QueryToolchainIncludePaths` — the two call sites that
      run synchronously on the *main thread* (a paste keystroke, first LSP-config
      resolve for a language) and could freeze the whole editor on a hung `wl-paste`/
      `clang++ -E -v` — now kill and fail gracefully past a 5s read timeout; (2)
      `Lsp/Transport.cpp` (shared by DAP) and `Acp/Transport.cpp` bound every read
      *after* a frame/message's first byte to a 30s stall timeout — idle *between*
      messages stays unbounded (normal), silence *mid*-message throws, reusing
      `LspClient`/`DapClient`/`AcpClient`'s existing malformed-frame disconnect path
      with no new plumbing (their disconnect reason is now the real exception message,
      not a fixed string, so a stall reads differently from a genuinely malformed
      frame in `*Messages*`); (3) `LspClient`/`DapClient`/`AcpClient`'s `pending_` maps
      gained a `sentAt` timestamp and `ExpireStaleRequests(maxAge)`, swept every 5s
      from `WindowManager::StartAutoSaveTimer`'s existing background tick via
      `LspManager`/`DapManager`/`AcpManager::ExpireStaleRequests()` — a request a
      server simply never answers (previously a permanently-spinning hover/completion
      with no error) now resolves with a synthetic timeout failure after 30s, through
      each protocol's existing error-callback shape (no new handling at any call
      site). `TaskProcess`'s own read loop deliberately got no timeout: silence isn't
      a hang signal for a legitimately slow build/test, and `Cancel()` (already wired
      to `cancel-task`, `SIGKILL`-based) already provides a working user-triggered
      recovery path that unblocks the blocking read via EOF — audited, not an
      oversight. **Both "still open" follow-ups shipped 2026-08-25**:
      `Editor/ProcessTimeouts.h` is a new, mutex-guarded (`TabWidth.h`-shaped)
      settings module holding the three now-configurable timeouts —
      `SubprocessReadTimeoutMs` (default 5000ms, `Clipboard`/
      `ToolchainIncludePaths`), `ProtocolStallTimeoutMs` (default 30000ms,
      `Lsp`/`Acp` `Transport::ReadFrame`/`ReadMessage`'s mid-message stall),
      `ProtocolRequestTimeoutMs` (default 30000ms, every `*Client`/`*Manager`'s
      `ExpireStaleRequests`) — each a real getter call now sitting where a
      `constexpr`/hardcoded-literal default argument used to be (evaluated
      fresh per call, including from a protocol client's own background read
      thread — the mutex is what makes that safe), and each exposed to Janet
      (`ned/set-subprocess-read-timeout-ms`/`ned/set-protocol-stall-timeout-ms`/
      `ned/set-protocol-request-timeout-ms`). The user-facing affordance:
      `DiagnosticsLog` gained a process-wide `HasUnseenDiagnosticsLogEntry`/
      `AcknowledgeDiagnosticsLogEntry` pair (set on any Warning-or-Error entry
      in a currently-visible category, Info-severity and hidden-category
      entries excluded) that `BufferView::Paint` polls once per frame — the
      same idiom `LspManager::HasUnseenLogEntry` already established for its
      own, older, LSP-only `"*lsp log*"` buffer, generalized to the shared
      `*Messages*` log every hang/timeout-recovery path (and everything else
      routed through `LogMessage`) now reaches. Deliberately gated behind a
      new opt-in `BufferView::SetSurfaceUnseenLogEntries(bool)` (default
      false, only `WindowManager::Pane`'s constructor turns it on) rather than
      reading that flag unconditionally: `HasUnseenDiagnosticsLogEntry` is
      genuinely process-wide state, unlike `LspManager::HasUnseenLogEntry`
      (an ordinary instance member, naturally test-scoped per fresh
      `LspManager`) — an unconditional read let one test's own `LogMessage`
      call intermittently clobber an unrelated `BufferView` test's
      `statusMessage_` under `ned_tests --order rand`, confirmed live before
      this gate existed, not a hypothetical.
- [ ] LSP deliberate cuts, revisit on demand: syncing every open buffer (not just the
      active one), incremental sync, idle server teardown, multi-root workspaces (raw
      subprocess stderr capture shipped 2026-08-25 -- see the diagnostics-log entry
      below).
- [ ] **Diagnostics/error log round 2** (v1 shipped 2026-08-24: `Editor/DiagnosticsLog.h`,
      a `"*Messages*"` buffer — plain find-or-create like `TaskRunner`'s own output
      buffers, not a real `Editor/Multibuffer.h` composite — rebuilt from an in-memory,
      cap-configurable (`ned/set-log-max-entries`, default 5000) ring on every category
      filter change; category visibility (`ned/set-log-category-visible`, `lsp` hidden
      by default) and best-effort daily-file disk persistence under
      `$XDG_STATE_HOME/ned/logs/`, retention mirroring `Backup.h`'s age-cap shape.
      Severity glyph/color comes free from the *existing* diagnostics gutter pipeline —
      each visible line gets a synthetic `Buffer::Diagnostic` rather than a hand-rolled
      `Mode::HighlightFunction` — and click/Enter-to-visit a `path:line:`-suffixed entry
      rides `BufferView::VisitResultUnderPoint`'s existing regex fallback, no new visit
      command needed. Wired into `Environment::DoString`/`DoFile` and
      `EditorBindings.cpp`'s command-invocation path (both now surface Janet's *real*
      error text via `janet_dostring`'s `*out`, replacing the old generic "see stderr
      for details" throw) and into `LspClient`/`DapClient`/`AcpClient`'s previously
      fully-silent malformed-JSON `DispatchFrame` catch plus their `onDisconnected_`
      sites. **LSP stderr capture** (shipped 2026-08-25): `ChildProcess` gained a third
      `StderrMode`, `Capture` — a dedicated pipe (never merged into stdout, which carries
      a framed protocol a stray stderr byte would corrupt), exposed via `StderrFd()`.
      `Lsp/Transport`'s spawning constructor took a new `captureStderr` parameter
      (default `false`, so `DapClient`'s own reuse of this exact constructor — see its own
      "reuses `Lsp/Transport.h` unmodified" note above — is untouched, since nothing there
      drains a captured pipe yet); `LspClient`'s real-subprocess constructor is the one
      caller that passes `true`, adding a second background thread
      (`stderrThread_`, declared alongside `readThread_` before `transport_` for the same
      destruction-order-unblocks-the-blocking-read reason) that line-buffers raw stderr
      bytes and posts each line to `LogMessage(LogCategory::Lsp, ...)`, tagged with
      `Transport::ProcessLabel()` (argv[0]'s basename, e.g. `"clangd: ..."`) — resolved at
      the `Transport`/`LspClient` layer rather than threading a language name down from
      `LspManager`, so no cross-file signature change was needed. **Diagnostics-log
      rollup** (shipped alongside, generic — not LSP-specific): `LogMessage` now coalesces
      a call that exactly repeats the ring's current *last* entry (same
      category/severity/message/path/line) into it instead of appending a new one —
      `LogEntry::count` increments and the entry's timestamp refreshes, so a source that
      logs the same thing every tick (a reconnect-and-fail loop, a repeated server
      warning) collapses to one updating `"... (xN)"` line in `*Messages*` instead of
      flooding it. On-disk persistence still writes the first occurrence but throttles a
      long identical streak to exponential-count milestones (1, 2, 4, 8, ...) rather than
      one line per repeat or none at all past the first. **DAP/ACP stderr capture**
      (shipped 2026-08-25, same day as LSP's): `DapClient` (which already reuses
      `Lsp/Transport.h` unmodified) now passes `captureStderr=true` on its real-subprocess
      constructor and runs its own `stderrThread_`, an exact copy of `LspClient`'s, logging
      to `LogCategory::Dap`. `AcpClient`'s own, separate `Acp/Transport.h` (newline-framed,
      not `Lsp/Transport.h`'s `Content-Length` framing) gained the identical
      `captureStderr`/`StderrFd()`/`ProcessLabel()` trio, and `AcpClient` gained the same
      `stderrThread_`, logging to `LogCategory::Acp` — so all three protocol clients now
      capture and roll up their subprocess's stderr the same way. Still open: the
      `ChildProcess` hang/timeout gap noted above; the ~20 other `BufferView.cpp`
      `statusMessage_`-only catches, `VcsRunner.cpp`'s one orphaned catch, and folding
      `TaskRunner` exit-code failures in, are all still un-wired (the
      umbrella `RunCommandAndHandleOutcome` catch already benefits from the Janet fix
      above with no changes of its own needed).
- [x] **Janet's raw stacktrace/compile-error print corrupts the live TUI** (shipped
      2026-08-24, raw-stderr-fd-redirect follow-up) — `Janet/Environment.cpp`'s
      `StderrCapture` (RAII) redirects the process's own fd 2 into a pipe for the
      duration of every `janet_dostring` call, restoring the real fd and draining the
      pipe right after, so Janet's unbuffered raw stacktrace print (confirmed via a
      standalone probe: it happens synchronously inside `janet_dostring` itself, not the
      CLI's own wrapper) never reaches the live Notcurses-rendered terminal. Also closed
      the "no path/line" gap noted above as a byproduct: `DoStringCapturingStacktrace`
      now prefers the captured stderr text (which does carry Janet's real
      `"path:line:col:"` location, for both a compile error and a runtime panic) over
      `*out`'s bare, location-stripped message, tmux/unit-verified against both shapes.
      `JanetVcsProvider.cpp`'s two direct `janet_dostring` call sites (VCS plugin
      callbacks) were routed through the same shared function, replacing their old "see
      stderr for details" placeholder with the real captured error text too. Degrades to
      a no-op (stderr wired to the real fd, pre-fix behavior) if the pipe/dup setup
      itself fails. Known limitation, not hit in practice: output written during the call
      beyond a pipe's default 64KB buffer would block on the full pipe since nothing
      drains it until the call returns — fine for Janet's own stacktrace prints, which
      are always far smaller.
- [x] **DAP round 2 / slice 4** (shipped 2026-08-24) — closed all of slice
      1-3's deliberate cuts except attach mode (a different launch-config
      shape, not a debugging-workflow gap — left open, its own slice).
      Conditional breakpoints and logpoints: `DapManager::Breakpoint` (line +
      `condition`/`logMessage`/`verified`) replaces the old bare-line store;
      `SetBreakpointCondition`/`SetBreakpointLogMessage` find-or-create a
      breakpoint at point's own line (`dap-set-breakpoint-condition`/
      `dap-set-breakpoint-log-message`, M-x only) and push it immediately via
      the existing `SendBreakpointsForFile`, which now also matches the
      `setBreakpoints` response's `verified` array back onto each breakpoint
      by index (still doesn't remap to the adapter's *snapped* line, only
      tracks verified/unverified — that remains open, see below). The gutter
      (`BufferView.cpp`'s debug-marker column) reads the richer
      `BreakpointsForKey` now: glyph by kind (`●`/`◆`/`○` for plain/
      conditional/logpoint — shape) crossed with color by verified state
      (`theme_.breakpointMarker`/new `theme_.unverifiedBreakpointMarker` —
      color), so all four combinations need only one new theme field, added
      through the same four-variant + `ThemePalette.cpp` + `ThemeFile.cpp`
      mechanical path `breakpointMarker`/`executionMarker` themselves went
      through. A small `Capabilities` struct parsed from the `initialize`
      response (`supportsConditionalBreakpoints`/`supportsLogPoints`/
      `supportsSetVariable`) appends a soft, informational warning to the
      status string when the active adapter said it lacks a feature —
      the field is still sent regardless (adapters routinely under-advertise
      but honor it anyway). Watch expressions: `DapManager::watches_` (plain
      ordered `vector<string>`, session-lifetime only) plus `AddWatch`/
      `RemoveWatchAt`/`Watches()`; `Evaluate` gained a `context` parameter
      (default `"repl"`, watches pass DAP's own `"watch"` tag) so the debug
      console below and watch re-evaluation are distinguishable to an
      adapter that treats them differently. `BufferView::ShowDebugInfo`
      fans out one `Evaluate(..., "watch")` per watch alongside its existing
      per-scope `variables` fan-out (one shared `remaining` counter now
      covers both), assembling a `"== Watches =="` section with each line
      carrying its own `[watch:N]` marker (`ExpandVariableAtPoint`'s
      `[ref:N]` convention, reused) that `dap-remove-watch` parses off
      point's own `*debug*` buffer line before re-running `ShowDebugInfo()`
      to refresh; `dap-add-watch` is a plain prompt. Thread picker:
      `RequestThreads` (the `threads` request) plus `SelectThread(id,
      callback)`, which sets a new `focusedThreadId_` and refreshes
      `stoppedFrameId_` via a 1-level `stackTrace` for it — a private
      `CurrentThreadId()` (`focusedThreadId_.value_or(stoppedThreadId_)`)
      is now what every inspection/step/continue request actually targets,
      so picking a thread steers stepping too, not just display.
      `dap-select-thread` copies `HandleAcpPermissionPromptKey`'s numbered-
      choice shape verbatim (new `InputMode::DapThreadSelect`,
      `pendingDapThreads_`/`dapThreadSelection_`). Editable `*debug*` buffer
      variables: `FormatDebugVariableLine` now always appends a second,
      independent `[owner:M]` marker (`M` = the *container's* own
      variablesReference the line's `variables` request was made against —
      distinct from the existing `[ref:N]`, present only when the variable
      itself is composite) so `dap-set-variable` can send DAP's own
      `setVariable` request without restructuring how `*debug*` buffer state
      is tracked (still nothing but the buffer's own line text — no side
      table); `ExpandVariableAtPoint` preserves a parent's `[owner:M]`
      across its own splice so an expanded composite variable stays
      editable too. Debug console (REPL): new `UI/DebugConsolePanel.h/.cpp`,
      structurally identical to `AcpPanel` (title/state row + content rows +
      one input row, an `OverlayHost` overlay hardcoded bottom-dock like
      `TerminalPanel` — no dock-side config was asked for), its own small
      input-echo/result/error transcript (`DapManager` has no transcript
      concept of its own the way `AcpManager` does), Enter calling
      `Evaluate` with the default `"repl"` context — the actual manual-
      evaluation console `dap-evaluate`'s one-shot echo-area prompt was
      always a smaller stand-in for. `dap-toggle-console` on `C-c D`
      (shifted "d", distinct chord from `duplicate-line`'s own plain
      `C-c d` — the `C-c A`/`C-c T` precedent) is the only round-2 command
      besides the existing F-keys to get a real binding; every other new
      command (`dap-set-breakpoint-condition`/`-log-message`,
      `dap-add-watch`/`dap-remove-watch`, `dap-select-thread`,
      `dap-set-variable`) stays M-x only, matching the documented F-key-
      quartet-only policy. Deliberate cuts, still open: attach mode;
      remapping a breakpoint to the adapter's snapped line (only
      `verified`/dimming is tracked); hit-count/`hitCondition` breakpoints;
      cross-restart persistence of conditions/logMessage/watches/thread
      focus — `ProjectSessionData::breakpoints` deliberately stays the old
      line-only `map<string, vector<size_t>>` on-disk shape,
      `DapManager::AllBreakpoints()` now a fresh line-only projection off
      the richer internal store rather than a direct reference, so this was
      a zero-touch cut rather than an on-disk format migration; the debug
      console has no scrollback/search/history-recall, same v1 cut
      `TerminalPanel`/`AcpPanel` both carry.
- [ ] VCS: "Generalize the two-callback plugin shape past version control" (cloud
      CLIs, Terraform, Docker) remains a framing, not a plan.
- [ ] A friendlier, possibly visual surface for browsing/editing ned's own
      settings beyond hand-writing `init.janet` — real live-editing already
      exists for themes specifically (`save-theme`/`ned/theme-set`, see
      `UI/ThemeFile.h`); a general settings surface would generalize that.
      Vague, unscoped — from `Stuff.md`.

### Gaps found comparing against mainstream editors (2026-08-23 survey, +2026-08-24)

Named so they're a conscious decision, not an oversight — being in a mainstream editor
isn't by itself a reason ned needs it too; several below are listed specifically to be
argued against, not just added.

Real, fairly uncontroversial gaps:

- [x] **Structured test-runner integration** (shipped 2026-08-24) —
      `Editor/TestRun/`: seven built-in output parsers (`TestOutputParser.h` —
      ctest, Catch2 console, pytest, `go test -json`, cargo test, hand-rolled
      tolerant JUnit XML, PHPUnit console; every format's line shapes pinned
      against real captured tool output except PHPUnit's, which follows the
      documented 9–11 console format), a Janet-pluggable parser registry
      (`ned/register-test-parser` — fn gets the raw output string, returns
      result tables or `{:results [...] :failures-only true :passed n}`; a
      registered name deliberately shadows a built-in, the user's escape hatch
      for a mis-parsing format), and `TestRunner` (TaskRunner's shape for the
      one project-wide `ned/set-test-command` argv+format pair: streams raw
      output into read-only `"*test output*"` while accumulating, parses on
      the main thread at exit — optionally from `ned/set-test-results-file`
      for file-writing formats like JUnit XML — and stores a generation-counted
      `TestRunOutcome`; filtered runs merge by name instead of replacing).
      UI: `run-tests`/`cancel-tests`/`show-test-results`/`run-test-at-point`/
      `rerun-failed-tests` on a new `C-c T` prefix (shifted "T" — plain
      `C-c t` is toggle-terminal's leaf, the `C-c A` collision precedent);
      `"*test results*"` is a `path:line:`-prefixed failures worklist carrying
      one synthetic `Buffer::Diagnostic` per failure/skip line
      (`RebuildTestResultsBuffer`, the `*Messages*` precedent — severity
      glyphs, underlines, and Enter/click jump-to-failing-test all ride
      existing pipelines with zero new plumbing), refreshed in place on every
      parse while open; a per-test ✓/✗/− gutter column (bare Palette16
      constants, the diff-gutter precedent) lights discovered test definitions
      via `Mode::testDiscovery` — a seventh tree-sitter Mode capability with a
      ned-local `@test.definition`/`@test.name` capture convention, new
      `*-tests.scm` queries for C++ (Catch2 `TEST_CASE`/`SCENARIO`/
      `TEST_CASE_METHOD` + gtest `TEST`/`TEST_F`/`TEST_P` — written against
      the grammar's real error-recovery parse of unexpanded macros, whose
      sibling `compound_statement` body the closure re-attaches), Python,
      JS/TS(X), and PHP (`test*`/`#[Test]`/`*Test` classes) — matched against
      results by a tolerant pure rule (`MatchesTestName`: exact, `[param]`
      stripping, `parent/subtest`, trailing `Class::method`-style segment;
      Failed beats Passed beats Skipped on aggregation, basename-level file
      filter). `run-test-at-point` resolves the innermost definition
      containing point through the `{test}`/`{file}` placeholder template
      (`ned/set-test-filter-command`, per-element substitution, never a
      shell); `rerun-failed-tests` chains one sequential filtered run per
      failed test from each exit (framework-agnostic and correct; `pytest
      --lf`-style native rerun stays the faster per-framework config). A
      pre-existing symbol-gutter staleness bug came out in live verification
      and is fixed for both columns: a `GutterWidth()` call during a buffer
      switch's own event handling could stamp the mode-derived gutter caches
      current-and-empty under the *old* mode before `Paint()`'s resync
      replaced it (confirmed via instrumented trace, marks stayed blank until
      the next edit) — the resync now discards both stamps. Worked plug-in
      example (any unsupported framework):
      `(ned/set-test-command ["mix" "test"] "mix")` +
      `(ned/register-test-parser "mix" (fn [output] ...))` parsing lines into
      `@[{:name ... :status :failed :file ... :line n :message ...}]`.
      Deliberate cuts: Catch2/PHPUnit pass marks are *inferred* from a clean
      full run (failures-only formats never name passing tests; junit-xml via
      results-file is the precise alternative); pytest needs `-v` (or
      junit-xml) for per-test pass marks; go's basename-only `file:line` can
      miss jump-to-source in multi-directory modules; no Go/Rust *discovery*
      (no bundled modes — their output still parses); no gutter-click
      run-this-test; timeouts/parallelism stay the framework's own business.
- [x] **Snippet expansion** (shipped 2026-08-24) — TextMate-style tabstops:
      `Editor/Snippet.h` (pure `ParseSnippet` + `SnippetSession`),
      `Editor/SnippetRegistry.h` (`ned/register-snippet`, per-language-key +
      `""`-global tiers), `Buffer::SnippetRange` (a sixth relocated tracked field
      with active-aware gravity — built on the existing relocation primitives as
      anticipated, though *not* literally on secondary cursors, which dedupe by
      point and clear on undo), smart-TAB triggering via `indent-for-tab-command`
      (+ `expand-snippet` for TAB-shadowed modes), live mirrors, an
      `InputMode::Snippet` session with per-keystroke undo grouping/mirror sync in
      `RunCommandAndHandleOutcome` hooks, active-field theme wash, and full LSP
      `insertTextFormat: 2` support (`snippetSupport` now advertised; raw
      `${1:...}` insertion bug fixed). Deliberate cuts: `$TM_*` variables,
      choices, transforms, nested placeholders' inner stops, bundled default
      snippets, macro-replay continuation through a session.
- [ ] A real visual side-by-side 3-way merge/diff view. `AutoMerge` already
      auto-resolves the common case and drops real `<<<<<<<`/`=======`/`>>>>>>>`
      conflict markers into the buffer for a genuine divergence (deliberately, so the
      auto-resolved case needs no bespoke UI at all — see `Text/ThreeWayMerge.h`) but a
      real conflict today is still hand-edited text markers, not a visual diff.
- [ ] A plugin marketplace/package registry (VSCode extensions, MELPA/straight.el).
      Ned's whole model is one Janet-scriptable environment plus opt-in project-local
      plugins (`Editor/ProjectPlugins.h`) gated by `ProjectTrust`'s hash-based,
      disuse-expiring trust registry — a marketplace implies a supply-chain-trust
      problem this project has so far deliberately stayed out of. Leaning "won't do"
      rather than "open," named here so that's a conscious call, not silence.
- [ ] A single fuzzy command palette unifying M-x/find-file/switch-buffer into one
      popup (VSCode/Sublime's Cmd+Shift+P). Real Emacs itself keeps these as separate,
      purpose-built commands with their own bindings — consistent with this project's
      stated Emacs-class-parity vision, so this reads as a different, already-chosen
      philosophy rather than an obvious gap.
- [x] **Auto-revert/auto-merge are pure polling, not file-watcher-driven** (shipped
      2026-08-24) — `Editor/FileWatch.h`'s `FileWatcher`, an inotify-backed trigger:
      watches each open buffer's *parent directory* (write-sibling-then-rename saves —
      including ned's own `ProjectReplace` — invalidate a file-level watch), filters
      events to watched basenames, debounces a burst (~100ms quiet, ~500ms cap) into
      one callback, and fires `WindowManager::SweepExternalChanges` (the tick's
      AutoRevert/AutoMerge/diff-gutter portion, factored out so both triggers run
      identical code) via `EventLoop::Post`. The 5s poll tick keeps running unchanged
      as the safety net (NFS, watch-budget exhaustion, a dropped event) — the watcher
      only lowers latency and both paths are idempotent (each re-checks
      `ExternallyModified()` per buffer). Watch set re-derived from
      `BufferList::Buffers()` per tick and per watcher-fired sweep — deliberately no
      buffer-open hook (`BufferList::SetOnFileOpened` is single-slot and already
      claimed twice over in `main.cpp`; a newly opened buffer is watched within ≤5s
      with the poll sweep covering the gap). `ned/set-file-watch` (default true)
      empties/restores the watch set at the next resync. Deliberate cuts: Linux-only
      inotify with a fully-inert fallback (no kqueue/FSEvents — no non-Linux port
      exists to serve); no per-event granularity (any relevant event triggers the
      existing whole-list sweep rather than a targeted single-buffer check);
      `IN_IGNORED` heals a deleted-and-recreated directory only at the next resync,
      not instantly.

### Native Windows port (idea, unstarted — design sketch only)

Raised alongside the system-clipboard work (`Editor/Clipboard.h`'s WSL detection covers
running ned as a Linux binary under WSL today, shelling out to `clip.exe`/
`powershell.exe` over WSL's own interop — real, already shipped). A *native* Windows
build (running directly under Windows Terminal or a raw console host, no WSL layer) is a
distinct, much larger effort: this codebase is POSIX throughout, not just at one or two
call sites, so "port" means replacing the platform layer wholesale, not adding `#ifdef`s
at the margins:

- **Process spawning** (`Editor/Process/ChildProcess.h`, `posix_spawn` + `pipe`) needs a
  `CreateProcess`/anonymous-pipe implementation behind the same `WriteAll`/`ReadSome`/
  `WaitForExit`/`Kill` surface — every LSP/DAP/ACP/task/VCS subprocess integration in
  `Editor/` is built on this one class, so a faithful reimplementation behind the
  existing interface is what keeps all of them working unmodified.
- **The embedded terminal panel** (`Editor/Terminal/PtyProcess.h`, `forkpty`) needs
  ConPTY (`CreatePseudoConsole`) instead — a real API, but a different threading/
  handle-lifetime shape than a POSIX pty fd pair.
- **Raw terminal I/O** (`UI/TerminalColorProbe.h`'s `termios`/`poll` raw-mode probe, and
  this feature's own OSC 52 write) needs the Win32 console API or, on a recent-enough
  Windows Terminal, VT passthrough — Windows Terminal itself already speaks ANSI/VT and
  OSC 52 natively, which is what makes this plausible at all rather than a dead end.
- **Notcurses itself** would need to build and run against the Win32 console/Windows
  Terminal target — worth checking Notcurses' own upstream platform support before
  committing to this, since ned's entire `UI/` layer sits directly on it with no
  abstraction gap to absorb a gap there.
- A PowerShell-flavored bundled theme (matching PowerShell/Windows Terminal's own
  default palette) would be a small, self-contained addition once the port itself
  exists — `UI/ThemeRegistry.h`'s fixed name→factory table is exactly the extension
  point, no new mechanism needed.

Unscoped beyond this sketch — no estimate of effort or of which piece would need to land
first (process spawning is the obvious dependency root: nothing else works without it).

## Won't do (at least not soon)

- **Org Babel** — subsystem-sized, and arbitrary code execution triggered by opening a
  text file: a security surface to design around deliberately, not bolt on.
- **Org table formula/spreadsheet engine** — a small programming language of its own.
- **Org export backends / publishing pipeline** — each a standalone tool-sized effort.
- **Alt+Click cursor creation** — mouse-dependent in a terminal app where SSH/tmux
  mouse passthrough is unreliable; keyboard multi-cursor commands cover it.
- **Bundling or requiring a JVM** for anything (heavy checkers like `ltex-ls` stay
  opt-in user configuration).

## Notes for whoever builds next

- Build/test: `cmake -S . -B build && cmake --build build`, then
  `ctest --test-dir build`. Sanitizer opt-in: `-DNED_ENABLE_SANITIZERS=ON` with
  `-DCMAKE_BUILD_TYPE=Debug` — the suite is expected clean; a finding is a real bug.
