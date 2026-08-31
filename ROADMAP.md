# Ned Roadmap

What's still open. Completed work is deliberately not tracked here — the detailed
per-feature design/decision records this file used to carry were pruned 2026-08-20 and
again 2026-08-25 (re-accumulated in between); full history lives in git
(`git log --follow ROADMAP.md`, `git show <rev>:ROADMAP.md`, or just `git log` for the
feature's own commit). Current architecture is documented in `CLAUDE.md`. When an item
here ships, replace its entry with a one-line pointer to the shipping commit (or delete
it outright) rather than writing up what was done — the writeup belongs in the commit
message, this file is a todo list, not an archive.

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

- [ ] **Jank replaces Janet** — once possible, replace the internal scripting
      representation with [jank](https://github.com/jank-lang/jank).

### Language Intelligence

- [ ] **Go-to-file-at-point resolver gaps** (`Mode::importTarget`, the hand-rolled
      import/include resolver): LSP should be tried first where a server can answer it
      (clangd's `textDocument/documentLink` for `#include`); Python's leading-dot
      relative imports (`from . import x`) aren't handled; PHP namespace `use` needs a
      PSR-4 autoloader parse; JS dynamic `import(...)` isn't resolved; node_modules
      `package.json` main/exports resolution goes no further than `index.*` inference;
      Rust has no bundled mode yet to resolve at all.
- [ ] Whether Markdown fenced code blocks / Org `#+BEGIN_SRC` blocks should get the same
      real-LSP-sync treatment HTML `<script>`/`<style>` embedded documents already have
      is an open question — spawning a live language server per code fence in an
      ordinary notes file could be noisy for illustrative/incomplete snippets.
- [ ] **LSP request coverage is narrow** (2026-08-25 audit; `declaration`/`typeDefinition`/
      `implementation`/`signatureHelp` closed 2026-08-26; `references`/`documentSymbol`/
      `workspace/symbol` plus a capabilities-object/completion-context hygiene pass closed
      2026-08-26; `documentHighlight`, `signatureHelp` auto-trigger, and
      `documentFormatting`/`rangeFormatting` closed 2026-08-26 — see below). Only
      hover/completion/codeAction(+resolve)/definition/declaration/typeDefinition/
      implementation/references/documentSymbol/workspace-symbol/rename/switchSourceHeader/
      executeCommand/signatureHelp/documentHighlight/formatting/rangeFormatting are ever
      sent. Still missing: `semanticTokens` full/delta/range (highlighting stays
      tree-sitter-only, never server-informed — matters where tree-sitter can't
      disambiguate, e.g. C++ template vs. less-than); `inlayHint`; `codeLens`;
      `callHierarchy`/`typeHierarchy`; and `onTypeFormatting`. Pull diagnostics
      (`textDocument/diagnostic`) also unsupported — harmless while every configured server
      pushes, a real gap only if one doesn't.
      2026-08-26 (closed same day it was flagged): `documentHighlight` — live-on-
      cursor-move (debounced via the same `LspCompletionDebounceMs()` completion already
      uses) plus a manual `lsp-document-highlight` command (M-x only), both driving
      `BufferView`'s own ephemeral `documentHighlight_` state (not `Buffer::Diagnostics()`'
      persistent shape — closer to ghost-text completion's lifecycle) rendered via a new
      `Theme::documentHighlightBackground` overlay, below selection/isearch/snippet-field
      in `Paint()`'s brush-priority chain but above the execution-line/multibuffer/
      trailing-whitespace washes. No on/off toggle — read-only decoration with no editing-
      flow risk unlike auto-trigger completion/signature-help. `signatureHelp` auto-trigger
      — fires on typing `(`/`,` (same debounce, gated by a new
      `LspSignatureHelpAutoTriggerEnabled()` toggle, default true) via
      `MaybeScheduleSignatureHelp`/`RequestSignatureHelpAtPoint`, writing into the shared
      `statusMessage_` the same way `lsp-hover`/manual `lsp-signature-help` already do —
      coexists with ghost-text completion (disjoint UI surfaces, inline vs. echo area), no
      mutual suppression. `documentFormatting`/`rangeFormatting` — `LspManager::
      RequestFormatting`/`RequestRangeFormatting` (bare `TextEdit[] | null` response,
      `ExtractFormattingEdits`; `rangeFormatting` added for API symmetry but not wired into
      any command yet — `save-buffer`/`format-buffer` are whole-buffer operations already).
      `save-buffer`/`save-buffer-force` defer to a new async `BufferView::
      RequestLspFormatThenSaveBuffer` (via `CommandContext::deferSaveForLspFormat`) only
      when a new opt-in toggle (`ned/set-lsp-format-on-save`, default **false** — silently
      changing existing installations' save behavior would be a real surprise) is on, no
      external `FormatCommand()` is configured (that always wins unconditionally — the
      more specific, deliberately hand-configured choice), and a server is actually running
      for the buffer's language; `FormattingOptions` is fixed (`tabSize: TabWidth()`,
      `insertSpaces: true` — this codebase has no per-buffer tabs-vs-spaces concept to
      source them from yet). Found and fixed in the same pass: `BufferView.cpp`'s
      anonymous-namespace `ApplyWorkspaceTextEdits` (shared by `ApplyCodeAction`/
      `ApplyRename`, now also this) never wrapped its apply loop in
      `Buffer::BeginUndoGroup`/`EndUndoGroup` — invisible for rename/code-actions' usual
      handful of edits, but would have made undoing a whole-document format take one
      keypress per edit; now one `Buffer::Undo()` reverts a full format. All three
      tmux+clangd-verified live (documentHighlight's occurrence tracking across edits/
      motion; signature-help appearing after `(` with no manual invocation; a real
      badly-formatted file reformatted on save both on disk and in the buffer, one `C-_`
      restoring it, and the external-formatter-precedence case confirmed by configuring
      both and observing the external one win).
      2026-08-26 (closed same day it was flagged): the direct-subprocess
      `LspClient` path (as opposed to a broker-owned connection) previously had no
      graceful `shutdown`/`exit` handshake at editor exit at all — it just let the child
      get killed by `ChildProcess`'s destructor. `LspManager::Shutdown()` (called once from
      `main.cpp`'s post-`Run()` sequence, before local teardown) now sends real
      `"shutdown"`+`"exit"` frames to every *directly-spawned* client, mirroring
      `LspBroker::Shutdown()`'s own fire-both-frames-without-waiting-for-the-response
      pattern exactly (there is no live `EventLoop::Run()` left at that point to wait
      with — `Transport::WriteFrame`'s own bounded stall timeout is what keeps this from
      hanging, and `ChildProcess::~ChildProcess()`'s existing close-stdin/poll/SIGKILL
      escalation is still what actually bounds the wait for the process to exit, unchanged
      by this addition). A broker-backed client (`LspManager::brokerBackedLanguages_`,
      stamped in `ClientForLanguage` at the one call site that already knows the
      distinction) is correctly *never* sent shutdown/exit — that server is shared with
      other `ned` processes and the broker daemon itself, and must outlive this one.
      tmux-verified live both ways: a direct-spawned clangd was confirmed as a real child
      process (`pstree`) that exits promptly on `C-x C-c` with no hang; a broker-backed
      clangd (parented to the broker daemon) was confirmed to keep running, untouched,
      after `ned` exited normally.
      2026-08-26: `find-references`/`lsp-find-references` — `project-find-references`
      (`M-?`) now tries a real `textDocument/references` first when a language server is
      running for the buffer (`LspManager::RequestReferences`), falling back to the
      original RE2 text scan only when none is (mirrors `SwitchHeaderSource`'s own
      "LSP is a nice-to-have accelerant, not the only path" precedent — unlike
      `lsp-goto-definition`/etc., which refuse outright with no server). `lsp-goto-symbol`
      (`M-g i`, real Emacs' own `imenu` binding) sends `textDocument/documentSymbol` and
      opens a `project-find-file`-style fuzzy picker over the results.
      `lsp-workspace-symbol` (`C-c l w`) sends `workspace/symbol`, live-re-querying
      (debounced via the same `LspCompletionDebounceMs()` ghost-text completion already
      uses) as the query is typed, since the server does its own matching rather than
      handing back a full list to filter locally. Both share `LspContent.h`'s
      `ExtractSymbols`, which parses all three response shapes the spec allows
      (hierarchical `DocumentSymbol[]`, flat `SymbolInformation[]`, and 3.17's
      range-optional `WorkspaceSymbol[]`) uniformly.
      Also 2026-08-26: `lsp-goto-declaration`/`lsp-goto-type-definition`/
      `lsp-goto-implementation` gained default bindings (`C-c l d`/`C-c l t`/`C-c l i`,
      previously M-x-only); `BuildInitializeParams`' advertised capabilities object
      previously declared only `completion`/`codeAction`/`window.workDoneProgress` despite
      this client sending/handling hover/definition/declaration/typeDefinition/
      implementation/references/rename/signatureHelp/publishDiagnostics and
      `workspace/configuration`/`workspace/executeCommand` — all now declared too (harmless
      against clangd's own permissive handling either way, confirmed before and after, but
      a capability-strict server is entitled to assume otherwise); `textDocument/completion`
      requests now carry `context: {triggerKind: 1}` (every call site here is a manual/
      explicit trigger, never a tracked trigger character).
- [ ] **LSP edit-application gaps** (2026-08-25 audit) — `ApplyCodeAction`
      (`BufferView.cpp`) outright refuses any code action whose edit touches more than
      one file, unlike rename, which does apply multi-file edits correctly; rename
      itself only applies the `changes`-map response form and silently drops a
      `documentChanges`-only response (`LspContent.cpp`'s `touchesUnsupportedForm`); a
      code action that triggers a server-side `workspace/applyEdit` push (rather than
      `workspace/executeCommand`) has no client handler at all — nothing ned wires up
      needs this yet, but it'll break silently the day something does.
- [ ] `rename-project-path` never tells an open LSP server about the rename (no
      `prepareRename`, `linkedEditingRange`, or `workspace/willRenameFiles`/
      `didRenameFiles`) — import paths elsewhere go stale until the server notices on
      its own.

### Navigation & Search

- [ ] **Multibuffer gaps**: no full-commit diff view (browsing one commit's whole diff
      from `*vcs log*`, not just the working tree); no result cap/warning on
      `project-find-references` for a very common identifier (a huge match set builds a
      proportionally huge composite buffer) — still true for its RE2 text-scan fallback
      path (no LSP server running for the buffer); the real `textDocument/references` path
      added 2026-08-26 has the same gap in principle but is bounded by whatever the server
      itself returns, not a raw project-wide regex sweep. `VisitResultUnderPoint`'s jump-to-source
      stays line-granularity even though `Buffer::ExcerptRange` already carries the
      exact source byte range that would let it preserve the intra-line column.
- [ ] A real visual side-by-side 3-way merge/diff view. `AutoMerge` auto-resolves the
      common case and drops real `<<<<<<<`/`=======`/`>>>>>>>` conflict markers into the
      buffer for a genuine divergence, but a real conflict is still hand-edited text,
      not a visual diff.

### Large Files

- [x] **Genuinely huge (multi-GB) file editing via a piece table — v1 floor shipped.**
      `Rope` (and everything built on it) assumes fully-resident content, which doesn't
      scale past a full in-memory load; a read-only-until-fully-loaded viewer was
      considered and rejected — real editing without a blocking full-load step needs a
      different storage engine, not just a different loading strategy. `Text/PieceTable.h/
      .cpp` (spans over an mmap'd original file, `Text/MappedFile.h/.cpp`, plus a small
      append-only insert buffer) is the engine; `Text/ITextStorage.h` (with `RopeStorage`/
      `PieceTableStorage` implementations) is what lets `Buffer` hold either storage
      behind one interface, `Buffer::Content()` included — every one of the ~340 existing
      external call sites across the codebase kept compiling against the interface
      unchanged, no throw/refusal needed there (a piece-table-backed buffer answers the
      same calls, just possibly at a different cost — `ITextStorage::IsHuge()` is the
      opt-in a size-sensitive caller checks first). `Buffer::FromHugeFile` is the
      construction path (wired into `BufferList::OpenFile` above a configurable
      `HugeFileThreshold()`, default 1 GiB); open/scroll/seek/point-motion/insert/delete-
      anywhere/undo/redo/multi-cursor/save all work, streamed and memory-bounded end to
      end (measured, not assumed: opening a 200 MiB file grows RSS by ~200 KiB, saving a
      220 MiB file with an edit grows it by ~128 KiB — `[memory]`-tagged tests in
      `PieceTableTest.cpp`/`BufferHugeFileTest.cpp`). A free-disk-space safety check
      (`Text/DiskSpace.h`) forces a huge buffer read-only at open time if there isn't
      comfortably enough free space to save it (COW filesystems like Btrfs/ZFS can need
      close to double a file's size to safely rewrite it), with `toggle-read-only` as the
      override and a hard, non-overridable re-check at actual save time.

      Still open: `Minimap` bails out entirely for a huge buffer rather than the real bounded
      line-sampling redesign (a stopgap fix for a real hang, see this file's own history
      for the underlying cause — `PieceTable`'s 256 KiB original-file leaves make any
      *unbounded* per-line iteration ~500x costlier than `Rope`'s); `BufferView`'s own
      line-count/scrollbar reads haven't been audited for that same unbounded-iteration
      risk class; the CLI-arg deferred-open path (`main.cpp`) only checks
      `AsyncLoadThreshold()`, never `HugeFileThreshold()`, so a file under the async
      threshold's default skips huge-file handling entirely when opened via CLI argument
      (doesn't bite the real multi-GB target case, but is a real gap for a
      lowered-threshold config) — same shape of fix as the session-restore bullet right
      below. Every feature built on full-document access rather than the storage
      engine's own bounded reads — Vim motions/search, Org, snippets, multibuffer,
      bookmarks, session save-place, VCS blame/diff gutters — simply doesn't operate
      *well* on a huge buffer yet (nothing crashes; `IsHuge()`-unaware code just pays
      whatever cost full materialization costs). See the staged follow-up plan below for
      bringing those online incrementally rather than all at once.
- [x] **Progressive, editable-while-loading huge-file open — v1 shipped.** The v1 floor
      above opened a huge file fully synchronously on the main thread (one blocking
      whole-file scan, one whole-file `MappedFile::ReleasePages` at the very end) — live-
      tested against a real 14.7 GB file, this froze the UI for minutes with RSS climbing
      toward the full file size. `Source/UI/HugeFileLoader.h/.cpp` (mirrors
      `AsyncFileLoader`'s threading contract) now streams a huge file in on a background
      `jthread`, `~8 MiB` chunk-groups at a time, via two new `PieceTable` primitives
      (`FromFileRange` — builds a fragment off-thread from a given byte range of a shared,
      loader-owned `MappedFile`; `Concatenated` — O(log n) splice onto the live tree, safe
      to run inline on the main thread) — releasing each chunk-group's mmap pages as it
      goes rather than holding the whole file resident. The buffer is genuinely editable
      throughout (not just viewable) — `Buffer::MarkLoading(bool forceReadOnly)` decouples
      `IsLoading()` from `ReadOnly()` (the existing async tier still passes `true`; this
      tier passes `false`) — so `Buffer::AppendHugeLoadChunk` always splices at the
      buffer's *current* end, safe regardless of concurrent edits since nothing can exist
      past "however much has loaded so far." Background appends coalesce into their own
      undo lineage (`CanAmendLoadAppend_`, separate from ordinary typing's `CanAmend_`) so
      a multi-minute load with no user interaction is one undo step, not thousands, and
      never merges with a real edit's own step either direction. If a user undoes past a
      landed chunk, the loader detects its own next-append-size expectation no longer
      matches and stops cleanly (logs a warning, leaves the buffer as-is) rather than
      silently splicing past the gap.

      Making Undo/Redo safe on a buffer this size surfaced a real, independent,
      already-shipped bug: `Buffer::Undo()`/`Redo()` materialized the *whole* buffer via
      `ToString()` (twice) just to diff old vs. new content — replaced with a
      storage-native, exponentially-growing-block comparison built on the already-existing
      `ITextStorage::Substring`, bounded to O(the actual differing region) rather than
      O(document size) for the common case (one edit, or one load-append), for every
      buffer, not just huge ones. Live full-scale testing (real 14.7 GB file) then
      surfaced a second, more serious, also-pre-existing bug entirely unrelated to this
      feature's own new code: `LspManager::SyncToServer` called `buffer.Text()`
      unconditionally, *before* checking whether any LSP client was even configured for
      the buffer's language — `SyncBackgroundBuffers`' periodic tick paid a full multi-GB
      copy every 5 seconds for a buffer with no server configured at all, and once that
      cost exceeded the tick interval the `EventLoop::Post` queue backed up forever,
      hanging the whole editor (confirmed via a standalone timing harness against a live
      4 GB reproduction, `[TICK] SyncBackgroundBuffers: 3687.77ms` against a 5 s interval —
      not a hypothetical). Fixed by moving the `ClientForLanguage` check ahead of the
      `buffer.Text()` argument. Also added: `SetLikelyBinary`/`BinarySafeguardsActive` —
      a buffer opened via a confirmed "open anyway?" binary override now skips
      format-on-save (external command and LSP formatter alike), ensure-final-newline,
      and forced/explicit line-ending conversion by default (`toggle-binary-safeguards` to
      override per-buffer) — a real file can look binary by the sniff heuristic and still
      be something the user genuinely wants to edit as text, so this is a default, not a
      hard block.

      **Guardrails identified but deliberately not chased down in this pass** (each is a
      real cost/risk at multi-GB scale, none is corruption-on-write the way the two fixed
      bugs above were, and none blocks ordinary use):
      - ~~`save-buffer`'s own `HasConflictMarkers(context.buffer.Text())` check ...
        fully materializes a huge buffer on every save~~ — fixed, see
        huge-file-search-and-save below. (`TrimTrailingWhitespaceOnSave()`'s own trim
        pass never actually had this problem — `SaveToFile`'s huge branch already ran it
        through `StreamingSaveWriter`, not a whole-string copy, from this same commit;
        this bullet mis-stated that half.)
      - DAP full-document-shaped operations for a huge buffer aren't guarded the way LSP
        sync now is (see the huge-file-lsp-gate entry below) — not a live-reproduced issue
        yet, since debugging a multi-GB buffer is a much rarer combination than editing one
        with a language server attached, but the same shape of fix would apply if it comes up.
      - `Backup::AutoSaveFileBuffers`/`PersistentUndo::SaveUndoHistory` silently skip a
        buffer entirely once it clears `MaxBackupBytes()`/`MaxUndoBytes()` (16 MiB
        default) — correct/safe (skip, don't materialize), but means an actively-edited
        huge buffer today has *no* crash-recovery autosave and *no* persistent undo
        history across restarts. Was a low-stakes gap while editing a huge buffer was
        rare/awkward; matters more now that this feature makes it ordinary.
      - `RegexPattern`/`QueryReplace`/`ProjectSearch` treat a `LikelyBinary()` buffer's
        content as ordinary text for search/replace purposes — not a corruption risk the
        way write-time formatting was, but semantically questionable in the same family;
        no guard added yet.
      - `AutoMerge`/`MergeExternalChanges`'s `ThreeWayMerge` still fully materializes both
        sides for external-change reconciliation on a huge buffer — explicitly kept out of
        scope this pass (diffing for *rarer* sync-to-disk-shaped operations was judged an
        acceptable cost, unlike `Undo`/`Redo`'s hot path) but worth revisiting if
        huge-buffer-plus-externally-changed-file turns out to be a real workflow.
- [x] **huge-file-lsp-gate: LSP/prose-checker sync no longer touches a huge buffer at
      all — real, live-reproduced fix, not the "guardrail, not chased down yet" class
      above.** Live-tested opening a real 3.1 GB file with `harper-ls` on `$PATH` (the
      prose checker auto-wires against any text file, not an unusual setup): RSS
      oscillated 6-9 GB continuously, never settling, well after load finished, because
      `BufferView::Paint()` calls `LspManager::SyncBuffer` every frame for the focused
      buffer, and `SyncToServer` built `buffer.Text()` (a full `ITextStorage::ToString()`)
      as an eager function argument *before* the "nothing changed since the last sync"
      check that would otherwise make it a no-op — so once any server was configured
      (the whole point of having one), every repaint paid a full multi-GB copy on the
      main thread, forever, not just at load. Two fixes: (1) that check is now hoisted
      above the `buffer.Text()` call in `SyncToServer` itself, so an already-synced,
      unchanged buffer never re-materializes on repaint, for every buffer, not just huge
      ones; (2) `SyncBuffer` now checks `buffer.Content().IsHuge()` first and skips both
      the primary language server and the prose checker entirely for a huge buffer — no
      server has a sane way to consume a multi-GB `didOpen` anyway, and harper-ls
      spell-checking gigabytes of text was never a meaningful feature. Logged once per
      buffer via the existing `LogError`/`*lsp log*`/echo-area path (not silent) so
      missing diagnostics/completion on a huge file has a visible explanation. Re-tested
      live post-fix against the same 3.1 GB file: RSS flat at ~42 MiB for the buffer's
      entire lifetime, no LSP error spam. `[Lsp][memory]`-tagged tests in
      `LspManagerTest.cpp` cover both the huge-buffer skip (with a real server
      configured, not just the pre-existing no-server-configured case) and the repeated-
      call no-op for an ordinary buffer.
- [x] **huge-file-search-and-save: literal isearch and the conflict-marker save guard no
      longer materialize a huge buffer.** `IncrementalSearch` used to build two full
      copies of the buffer (`content_` plus an ASCII-lowercased `contentLower_`) at
      session construction regardless of size; `save-buffer`'s
      `HasConflictMarkers(context.buffer.Text())` guard did the same on every single
      save. Both now branch on `ITextStorage::IsHuge()`: `IncrementalSearch::SearchHuge`
      (and `AppendWordAtPoint`'s huge branch) scan in bounded windows via
      `ITextStorage::Substring`, overlapping by `needle.size() - 1` bytes so a match
      straddling a window boundary is never missed, with the same smart-case/wrap-around
      semantics as the in-memory path; `Buffer::HasConflictMarkers()` does the equivalent
      windowed line-start scan and replaces the old free-function call site entirely (an
      ordinary buffer's cost is unchanged — one window covers the whole thing, same as
      before). Deliberately literal-only: `query-replace-regexp`'s own full
      materialization (`QueryReplace.cpp`'s `content_ = buffer_.Text()`) is a known,
      *not* fixed, gap — windowing a real regex engine (PCRE2, via lookaround/variable-
      length quantifiers) needs the partial-match streaming API described in this
      section's own "Streaming/parallel search" bullet below, not just an overlap window,
      and was scoped out on purpose: multi-MiB-plus source files/functions are treated as
      the pathological case here, not a real one worth the extra design. Both scans are
      still an O(document size) worst case when a query isn't found anywhere (unavoidable
      for a genuine whole-document search) but never hold more than one window's worth of
      the buffer in memory. `[IncrementalSearch][HugeFile]`/`[Buffer][HugeFile]`-tagged
      tests cover forward/backward/case-insensitive/wraparound/not-found on a huge buffer
      plus a dedicated window-boundary-straddling case for each of the two new scans.
- [x] **huge-file-session-restore: a restored session's own large/huge files no longer
      load synchronously at startup.** Same gap `deferredLargeOpenPath` already fixed for
      the CLI-arg path: the session-restore loop (`Source/main.cpp`, right after
      `LoadActiveProjectSession`) ran well before `EnableAsyncFileLoading`/
      `EnableAsyncHugeFileLoading` wire `BufferList`'s async/huge opener hooks (those need
      a real `EventLoop&`, constructed much later), so every restored file called
      `BufferList::OpenOrCreateFile` on the fully synchronous fallback path regardless of
      size — a multi-GB file left open at last quit re-blocked the splash on every
      subsequent launch. Fixed the same way: a restored file over `AsyncLoadThreshold()`
      is collected into `deferredSessionOpenPaths` instead of opened inline, then actually
      opened right after the async/huge hooks are wired (same insertion point
      `deferredLargeOpenPath` already uses) — it becomes an ordinary streamed-in background
      buffer, ✕ no per-file interactive confirmation, matching the rest of session
      restore's existing "best-effort" contract.

      Live-testing this surfaced a second, more serious bug in the process:
      `WindowManager::RestoreWindowLayout` used to run immediately after session buffers
      opened (long before the deferral above existed, when that assumption always held).
      `BuildNodeFromLayout` resolves every leaf by path with no open of its own — a single
      leaf naming a still-unopened deferred path fails to resolve, and per the function's
      own existing corruption-defense comment, an unresolvable leaf discards the *entire*
      restored tree, not just that one pane, silently falling back to the plain
      single-default-pane startup. Concretely: a session with a huge file as the *focused*
      buffer (the common case — it was being edited when the editor last quit) would
      restore focused on some other, arbitrary already-open buffer instead once the huge
      file was deferred, with no error and no indication anything was wrong — confirmed
      live (`small.txt` staying focused while a 20 MB `big.txt` streamed in behind it,
      reachable only via the tab bar). Fixed by moving the `RestoreWindowLayout` call
      itself to after `deferredSessionOpenPaths` finishes opening, ahead of
      `deferredLargeOpenPath`'s own CLI-focus override so a CLI-named file still wins
      outright, matching the pre-existing precedent. A small additional fallback (gated on
      `pathArg == nullptr`, since RestoreWindowLayout's own focus resolution already covers
      the common windowLayout-present case) re-derives the intended active buffer from
      `activeFile`/first-of-`openFiles` for the rare case RestoreWindowLayout still
      couldn't rebuild the tree at all (a pre-windowLayout-era session, or a file that
      failed to open even on retry).
- [ ] **Staged path to full feature parity on a huge buffer**, once the piece-table v1
      above lands, each stage independently shippable and not blocking the next:
      1. A bounded-range `Buffer` API (`LineCount()`, `TextInRange(start,end)`,
         `LineRange(startLine,endLine)` or similar) that never leaks `Rope`/`PieceTable`
         and is answerable by either storage without materializing the whole document —
         the actual unlock every later stage depends on; each subsequent stage is
         "migrate one subsystem off `Content()` onto this."
      2. Vim mode's bounded motions (character/word/line/paragraph, basic operators) —
         highest-value feature for actually editing a huge file well, and answerable
         with bounded reads near point.
      3. Streaming search/replace (see this section's own search follow-up below) — once
         it lands, Vim's `:s`/`:g`, isearch, and query-replace work on a huge buffer for
         free, since they'd be built on the streaming engine rather than `Content()`.
      4. Structural/index-driven features (Org outline, code folding, symbol gutter, VCS
         blame/diff) — these need a real index over the whole file, not just a bounded
         read, so this is the biggest remaining lift. Two real techniques apply, neither
         designed in detail yet: (a) generalize `PieceTable::Node`'s existing aggregate
         (`byteLength`/`codepointLength`/`lineCount`, combined left+right at every
         internal node) from a fixed triple into a pluggable, monoid-combined summary
         type — the same pattern as Zed's `SumTree` and xi-editor's metric-parameterized
         rope (formally: a finger-tree/monoid-augmented B-tree, Hinze & Paterson), so a
         structural index (headline offsets, fold regions, symbol positions) rides the
         same tree/traversal machinery instead of needing a bespoke index per feature;
         (b) tree-sitter's own `included_ranges` API (parse only specific byte ranges,
         stitch partial trees) is what would let that index stay lazy/viewport-driven
         instead of requiring one big upfront parse of a multi-GB file.
      5. Multibuffer/bookmarks/session save-place — mostly just track byte
         ranges/points through `Buffer`'s own relocation logic, which already lives in
         `Buffer.cpp` and is storage-agnostic — likely cheaper than stage 4 despite
         listed after it here.
- [x] **huge-file-regex-replace: `query-replace-regexp` no longer materializes a huge
      buffer — real windowed regex search/replace, not just literal isearch.** The
      partial-match approach this section used to sketch (`PCRE2_PARTIAL_SOFT`/`HARD`,
      chunk-fed via `PieceTable::ForEachChunk`) turned out to be the wrong tool: PCRE2's
      DFA matcher supports multi-segment restart but has no capture groups at all (needed
      for `$1`/named-group replacement templates), and the non-DFA matcher's own partial-
      match support is documented as unreliable for genuine multi-segment matching. Shipped
      instead: `QueryReplace::FindNextMatchHuge` (`Editor/QueryReplace.cpp`), a synchronous
      overlapping-window scan in the same spirit as `IncrementalSearch::SearchHuge` but
      generalized past a fixed-length literal needle. Two cursors drive it — `searchFrom`
      (where the next match may start, monotonic) and `reach` (how far the current window
      extends past it, grows on retry) — so a candidate match landing within
      `kOverlapMargin` (64 KiB) of the window's own tail is never trusted unless the window
      has reached the real document end; instead `reach` grows and the *same* `searchFrom`
      is retried with a wider window (advancing `searchFrom` past an unconfirmed candidate
      would skip a match starting in what's now the new window's lead-in). Each window
      additionally starts `kOverlapMargin` bytes *before* `searchFrom`, so genuine
      lookbehind gets real leading context. This bounds correctness to "a match, or the
      lookaround/multi-line span it depends on, is at most `kOverlapMargin` bytes wide" —
      the same class of accepted limit `ProjectSearch`'s line-bounded RE2 path already
      lives with. `RegexPattern` itself needed no change (searched an initial design
      through PCRE2 `NOTBOL`/`NOTEOL` options before realizing the two-cursor margin design
      already makes a window's own synthetic start/end unreachable as a match position, so
      the anchor flags would have been dead code — reverted, kept the change minimal).
      `[QueryReplace][HugeFile]`-tagged tests cover parity with the in-memory path, a match
      past the first window, one straddling a window boundary, lookbehind resolved in a
      later window, multi-window `ReplaceAll` offset drift bookkeeping, and a no-match
      multi-window termination case. Parallelizing the scan (the old sketch's
      `ProjectSearch`-style work-stealing pool, one byte range per worker) is still
      possible if single-threaded windowed scanning proves too slow in practice, but wasn't
      needed to make the feature correct or usable — not chased down in this pass.
      `ProjectReplace`/`ProjectSearch` (many normal-sized files on disk, not one open huge
      buffer) are a different problem and stay out of scope here.

### Editor Ergonomics

- [ ] **Terminal panel gaps**: no drag-resize of the drawer height (needs overlay
      mouse-capture semantics; height is Janet-configurable instead via
      `ned/set-terminal-height-percent`), no scrollback search/selection/copy, no
      multiple terminals/tabs, no terminal-side mouse forwarding to the shell (clicks
      focus the panel, wheel scrolls the ring — TUI apps inside don't receive mouse
      events), no OSC 52/title integration.
- [ ] **Vim-mode gaps**: no jumplist/changelist ring beyond the single `` ` ``/`''`
      toggle (no `C-o`/`C-i` ring); mark letters limited to `a`-`z`/`A`-`Z`/`'<`/`'>`
      (`A`-`Z` are buffer-local here, not vim's cross-file global marks); macros record
      raw keystrokes rather than vim's editable-register-text form; `.` replays
      verbatim (a count typed before it doesn't override the recorded one);
      search/`:s`/`:g` patterns pass straight to PCRE2 rather than translating vim's
      default "magic" escaping convention; Insert-mode `C-o`'s one-shot dot-repeat/
      register bookkeeping isn't unified with the interrupted session's own.
- [ ] **DAP gaps**: attach mode; remapping a breakpoint to the adapter's snapped line
      (only verified/dimming is tracked today); hit-count/`hitCondition` breakpoints;
      cross-restart persistence of conditions/logMessage/watches/thread focus (session
      storage deliberately stayed the old line-only shape); debug console has no
      scrollback/search/history-recall; no exception breakpoints
      (`setExceptionBreakpoints`/`exceptionInfo` — can't break on throw/uncaught); no
      function or data breakpoints; no `restartFrame`; no disassembly/memory view; every
      stop sends `disconnect`+`terminateDebuggee:true` rather than distinguishing a real
      `terminate` from a detach, which some adapters expect (2026-08-25 audit).
- [ ] **No buffer-list/ibuffer-style management** — `switch-to-buffer` is a
      name-completion prompt only; no dedicated buffer-list buffer with mark/save/kill
      batch operations.
- [ ] **No server/daemon mode** — no `emacsclient`-equivalent; one process per terminal,
      no way to keep a warm process (buffers, LSP connections, undo history) alive and
      attach a new terminal client to it.
- [ ] **Project root is fixed at startup** — `ProjectRoot.h` exposes `SetProjectRoot` as
      a primitive but nothing wires an `M-x`-reachable command to it; switching projects
      means restarting the process.
- [ ] **Test-runner gaps**: no gutter-click run-this-test; no Go/Rust test discovery (no
      bundled modes — their output still parses); pytest needs `-v` or junit-xml for
      per-test pass marks; Go's basename-only `file:line` can miss jump-to-source in
      multi-directory modules.
- [ ] **Snippet expansion gaps**: `$TM_*` variables, choices, transforms, nested
      placeholders' inner stops, bundled default snippets, macro-replay continuation
      through a session.
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

### Remote Development (SSH Remote Editing)

The goal: edit files on a remote host over SSH without ned itself running remotely —
comfortable enough that it doesn't feel like a degraded mode. Two shapes are on the
table and genuinely undecided: **(a) client-only**, everything shells out to `ssh`/
`sftp` per operation, no code footprint on the remote host at all; **(b) client + thin
remote agent**, a small companion binary deployed to the remote host (the JetBrains
Gateway/VS Code Remote-SSH precedent) that does what a bare SSH session is genuinely bad
at — fast recursive search, live file-change notification, and, the big one, running
the actual language server/debugger/task/VCS subprocesses *on* the remote host against
its real toolchain rather than against a locally-synced copy missing the remote
system's headers/deps/`compile_commands.json`. Likely path: build (a) first since it's
simpler and gets editing working at all; add (b) only once search latency or
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

**Connect UX**
- [ ] A connect dialog/command (`ned-connect` or similar): host, user, port, key/agent
      selection, jump-host/bastion support — ideally sourcing defaults from the user's
      own `~/.ssh/config` rather than reinventing host aliasing.
- [ ] A remembered recent-connections list — same shape as the recentf gap already on
      this roadmap, worth building on the same primitive rather than a separate one.
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

- [ ] **AI-assisted editing (ACP) gaps** (2026-08-26: round 1 validated live against
      Claude Code's own ACP adapter, `@agentclientprotocol/claude-agent-acp` — first
      real agent exercised end to end, not just crafted-JSON tests). Fixed this round:
      `HandleSessionUpdate`/`PushOrUpdateToolCall` no longer clobber a tool call's
      title/status back to a generic fallback when a later `tool_call_update` omits
      them (confirmed live — Claude's adapter routinely does); a tool call's `"diff"`
      content item (`{path, oldText, newText}`) is now captured
      (`TranscriptEntry::diffOldText`/`diffNewText`) and rendered as a line-count
      summary in the panel (a real unified-diff view is still a follow-up — see below);
      permission-prompt *resolution* now happens directly in `AcpPanel` when it has
      focus (`WindowManager::SetAcpPanelFocusChecker`), not only `BufferView`'s
      echo-area flow; a pending permission prompt (or any live agent chatter) no longer
      trips `ExpireStaleRequests`' generic 30s timeout out from under a human still
      deciding — confirmed live, this previously hard-failed any permission decision
      that took a bit over 30 seconds; the "ACP agent:" prompt now Tab-completes
      against `AcpConfig::AcpAgentNames()` instead of being pure free text (useful for
      registering several accounts, e.g. `claude-personal`/`claude-work`/
      `claude-consulting` — see below). Still open: no scrollback in the panel; a real
      diff view (actual +/- lines, not just a line-count delta) has no reusable
      line-diff utility to build on yet (`ThreeWayMerge.h`'s LCS diff is a private
      implementation detail); a completion-popover replacement for ghost text, an M-x
      dropdown, and code-action lists are all still squeezed into the one-row
      `EchoArea`; `terminal/*` tool-call support and `elicitation/create` structured
      forms are undeclared as client capabilities; no multiple concurrent agents/
      sessions (still one at a time, `Dap/`'s own precedent — revisit once a second
      agent, e.g. OpenCode, is wired the same way); no `session/load` history replay;
      `session/set_config_option`/`session/set_mode` aren't surfaced to the user; no
      MCP server passthrough (`session/new`'s `mcpServers` is always `[]`); no
      per-agent environment-variable override — `ChildProcess`'s `posix_spawn` always
      forwards the parent's global `environ` (`ChildProcess.cpp:140`), so multiple
      registered agents needing different credentials (e.g. separate Claude accounts)
      need a shell-wrapper argv (`sh -c '<set the right env> exec ...'`) rather than
      anything first-class; no per-agent "character" (a per-agent display-name/accent
      color, distinguishing `agent_thought_chunk` from `agent_message_chunk`) — v1 only
      widened `AcpPanel`'s own `DisplayStyle` (agent text/plan steps no longer share
      the exact same style as a plain user-message echo), deliberately not a full
      theming pass. Composer/echo-area input field's append-only editing (no cursor
      position, no forward-delete, no arrow-key movement) closed 2026-08-26 — see
      `git show da0a7c9`. Separately: `Keymap::AmbiguousBindings()` is diagnostic-only (a
      `CommandsTest.cpp` regression test), not enforcement — `Keymap::Bind` still lets a
      caller construct an unreachable-by-typing binding; a real structural fix (Emacs'
      own `define-key` semantics: reject/restructure a bind that would shadow an
      existing command) would change `Bind`'s signature across every call site
      including `ned/define-key`.
- [ ] **ACP chat-feel UX backlog** (2026-08-26 research pass — surveyed what Claude Code's
      own CLI and OpenCode's TUI do that users specifically call out as good, cross-checked
      against what `AcpPanel`/`AcpManager` actually do today; none of this is implemented
      yet, just scoped). Roughly in order of how much a single session of chatting with an
      agent would actually feel it:
      - Interrupt an in-flight prompt, and a visible "agent is working" spinner between
        sending a prompt and the first token, both closed 2026-08-26 — see
        `AcpManager::CancelPrompt`/`PromptInFlight` and `AcpPanel::OnEvent`'s Escape
        handling. Also fixed same day: a redundant "session ready" line no longer
        duplicates the panel's own `[Active]` title-bar state in the transcript (the raw
        `*acp: <agent>*` log buffer still gets it verbatim).
      - **Checkpoint/rewind per turn.** Claude Code's double-Esc `/rewind` (checkpoint
        auto-created per prompt, restore code/conversation/both) is the single most-cited
        "saved me" feature in the research above — and this codebase already has the two
        primitives it needs: `Text::UndoTree` (real tree, cheap `Rope` snapshots) and
        `Editor/Backup.h`. A per-turn checkpoint could be "snapshot every buffer touched by
        `fs/write_text_file` since the last user prompt" rather than a new storage format;
        the open design question is what "restore conversation" even means against ACP's
        `session/load` (still itself unimplemented — see the gaps entry above) rather than
        a from-scratch mechanism.
      - **Word-wrap in the transcript.** `AcpPanel::FormatTranscript`'s `Kind::AgentText`
        case is explicitly "No word-wrap in v1 -- split only on literal newlines the agent
        itself sent" (its own header comment) — a long unwrapped reply line just runs off
        the panel's fixed width with no way to read the rest, unlike `BufferView`'s own
        `Mode::wrapLines`.
      - **Lightweight Markdown rendering** (bold, inline code, bullet markers) in
        `AgentText`/`Kind::Plan` lines instead of showing `**`/backtick markup literally —
        agents write Markdown by default and every popular chat surface (Claude Code
        included) renders it rather than showing raw asterisks.
      - **`@`-style file-mention autocomplete in the composer** — reuse
        `Editor/FuzzyMatch.h` (already backs find-file/M-x/theme-picker) plus
        `Editor/ProjectTree.h` to fuzzy-complete a project-relative path inline while
        typing a prompt, the same affordance Claude Code/OpenCode both use so a prompt
        references an exact file instead of a name the agent has to go search for.
      - Explicitly *not* pulled from the research: OpenCode's session-sharing (`/share`,
        needs a hosted backend — out of scope for a local-first editor) and a unified
        command palette (already a stated non-goal below, for the same
        keep-purpose-built-commands-separate reasoning).
- [ ] **Real-time collaborative editing** (CRDT-based) — the biggest lift in this file;
      last.
- [ ] VCS: "generalize the two-callback plugin shape past version control" (cloud CLIs,
      Terraform, Docker) remains an open idea, not a plan.

### Documentation & Companion Tooling

- [ ] **Documentation framework** — man page(s), PDF, and a web page generated from one
      shared source (pandoc is the direct fit), mining the `ned/*` binding doc strings
      already passed to `Register<Fn>`. A build/release-time step, nothing ned does at
      runtime.
- [ ] **Environment setup tool** (`ned-setup` or similar) — first-run detection: shell
      integration, plus scanning the system for installed tree-sitter grammars and
      *generating an editable Janet file* loaded from `init.janet`. Deliberately a
      standalone, inspectable generator — silent runtime auto-detection was considered
      and rejected (system grammar layouts aren't portable).
- [ ] **Tree-sitter-assisted formatter** with JetBrains-level per-rule configurability
      ("a dprint clone that is actually awesome") — a substantial project per language,
      not a utility. Scope it once concrete gaps left by external formatters are known.

### Known Test Flakiness / Non-Critical Issues (Watch List)

Real, reproduced, non-urgent — each is safe to leave as-is for now, but worth fixing
opportunistically rather than re-discovering from scratch. Add to this list instead of
just fixing-and-forgetting or letting it fade from memory between sessions.

The LSP-broker-hermeticity flake, 2026-08-26 (`LspManagerTest.cpp`'s
two spawn-failure tests, plus the same shape in `ModeLineTest.cpp`'s spawn-failure-glyph
test — the latter caught by re-running the full suite against a real broker daemon left
warm from a live tmux session, exactly the scenario this flake needed to reproduce), was
fixed 2026-08-26 by adding `LspManager::SetBrokerSocketPathOverrideForTesting` and
threading it into `ClientForLanguage`'s `TryConnectToBroker` call; all three affected
tests now point at a guaranteed-nonexistent socket path instead of the real broker
socket.

`EchoArea::Paint`/`ModeLine::Paint` being byte-by-byte, not UTF-8-aware (found 2026-08-26,
live tmux+clangd testing `lsp-signature-help` — a multi-byte guillemet marker rendered as
blank padding, one blank cell per byte, silently wrong despite every unit test passing
since those compared plain byte strings) was fixed 2026-08-26: both now walk by codepoint
span (`Text/Utf8.h`'s `NextCodepointBoundary`, matching `BufferView`'s own
one-codepoint-per-cell content rendering) instead of by raw byte — `EchoArea`'s six
sentinel bytes (always exactly one byte) are still recognized via a one-byte-span check
before the general case; `ModeLine` gained a shared `AppendUtf8Columns` helper replacing
six duplicated byte-loop call sites. Double-width CJK/emoji columns are still one cell
each, matching `BufferView`'s own accepted limitation there — no `wcwidth`-equivalent
exists anywhere in this codebase, and fixing that only in these two widgets would be
inconsistent. `lsp-signature-help`'s active-parameter marker still uses ASCII `**...**`
rather than switching back to guillemets — a cosmetic choice either way now that the
underlying bug is gone.

Normal-mode self-insert silently dropping every non-ASCII keystroke (found 2026-08-26,
reported live as "pasting an emoji does nothing" — any terminal paste/keystroke of a
character outside 0x20-0x7E, not just emoji: accented Latin, CJK, ...) was fixed
2026-08-26. `BuildDefaultGlobalKeymap` only ever gave printable ASCII its own real
self-insert-command keymap entry (deliberately, per its own comment — enumerating a
literal entry per Unicode codepoint isn't feasible), and `Dispatcher::Feed` had no
fallback at all for a `NoMatch` on anything else, so it reported "<char> is undefined"
and dropped the keystroke instead of inserting it. `Dispatcher::Feed`'s `NoMatch` case now
falls through to `self-insert-command` for a *bare* (not mid-prefix-sequence) plain chord
(no Control/Meta/Special) whose codepoint isn't a C0/DEL/C1 control character — a real
rebind of any individual character, ASCII or not, still wins via `Match` before ever
reaching this fallback. `C-y`/`PasteFromSystemClipboard` was never affected (it inserts
the whole pasted string directly via `Buffer::InsertAtPoint`, bypassing per-character
dispatch) — only a terminal-native paste or direct keystroke of a non-ASCII character hit
this.

**Open, not yet root-caused:** `VimEngineTest.cpp:903`'s "Dot-repeat replays an Insert
session that used C-o" — passes cleanly every time run standalone
(`./ned_tests "Dot-repeat replays an Insert session that used C-o"`), but has failed
intermittently when the full suite runs (`ctest --test-dir build`), seen at least
2026-08-30. No obvious static/global mutable state in `VimEngine.h/.cpp` itself on a
first pass (grepped for `static`/`thread_local`, nothing suspicious) — order-dependence
likely comes from somewhere else the test touches (a shared `Buffer`/`Dispatcher`
fixture, register/clipboard global state, or similar to the `DiagnosticsLog`
global-state leak already fixed once in this codebase, `ChildProcess hang-protection
round 2`). Not yet worth a deep dive on its own; next time it reproduces, capture which
other test(s) ran immediately before it in that run — that's the fastest path to an
actual repro.

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

Unscoped beyond this sketch — process spawning is the obvious dependency root; nothing
else works without it.

## Maybelist (Speculative — Neither Committed nor Rejected)

Ideas worth remembering but not worth scoping yet — too undecided for "Open Items",
not disliked enough for "Won't do". Promote or delete on revisit rather than letting
these accumulate detail in place.

- [ ] **Merge-aware cross-session undo** — persistent undo (`Editor/PersistentUndo.h`,
      shipped 2026-08-25) content-gates: on reopen, restores the full tree only if the
      file's current on-disk content exactly matches some node already in the persisted
      tree (any node, not just the tip -- covers "quit without saving" for free); no
      match at all just discards the persisted history outright and the buffer starts
      fresh. The fancier version would three-way-merge a genuinely novel external change
      into the persisted history instead of discarding it (base = last-persisted content,
      ours = tree tip, theirs = fresh disk content), reusing `Text/ThreeWayMerge.h`/
      `Editor/AutoMerge.h`'s existing machinery to splice one merge node onto the old tip.
      Deferred because it means synthesizing an undo node for content the user never
      actually typed — a real risk of `undo` doing something surprising later — worth it
      only if the content-gate default proves too lossy in practice (i.e. people
      habitually edit files outside ned in ways that touch none of a session's own undo
      states and lose history often enough to complain).

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
- When you finish an item above, delete it (or replace it with a one-line pointer) in
  the same commit — don't leave a `[x]` writeup behind. Keeping this file short is the
  point.
