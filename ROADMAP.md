# Ned Roadmap

What's still open. Completed work is deliberately not tracked here — the detailed
per-feature design/decision records this file used to carry were pruned 2026-08-20,
2026-08-25, 2026-09-06, and again 2026-09-07; full history lives in git
(`it log --follow ROADMAP.md`, `git show <rev>:ROADMAP.md`, or `git log --grep=<slug>`
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

### Releases

**1.0, for context, since branching starts near it.** For a scriptable editor 1.0 is a
promise about the *Janet surface*, not about features: 160 `Register<>` bindings and 275
command names, and declaring 1.0 makes breaking any of them a major-version event. That
surface is still moving (`GrammarQuerySources` became designated-initializers mid-flight;
the parsing engine would reshape `Mode` outright), which is the real reason 1.0 is not close
regardless of how usable ned feels. Candidate criteria, all already present below: Janet API
frozen · parsing-engine decision made either way · no known data-loss paths · release
artifacts · the mdBook docs site.

### Rendering & Keystroke Performance
What is left of the translucency/theme-v2 work is performance, not appearance: the
compositor and theme engine shipped, and these are the costs that surfaced while building
them. Compositing design and the measurements behind it: `Docs/Translucency.md`,
`Tools/NotcursesGradientProbe.cpp`, `Tools/TerminalImageAlphaProbe.cpp`.
`Tests/KeystrokeBench.cpp` is the instrument for all three — it exists because every CPU
measurement said "fine" while typing felt bad.

The entry that stood here until 2026-09-22 said the incremental re-parse is what
dominates a keystroke on a large C++ file. It does not, and the correction is the only
part worth keeping: timed separately (the C++ attribution case in `Tests/KeystrokeBench.cpp`),
the re-parse is ~0.36 ms of a ~13 ms keystroke on a 107 KiB file and the per-frame queries
are all of the rest. The costs that turned out to be real are fixed -- slug for
`git log --grep=`: `query-walk-ancestry`; windowed highlight 6.9 ms -> 2.8 ms,
whole-document highlight 50.7 ms -> 14.8 ms, keystroke+repaint on a 9 KiB C++ file
6.1 ms -> 2.7 ms. Three items left behind.

- [ ] **Fold, locals and indent still walk the whole document per keystroke.** Only
      highlight and symbolKind are viewport-windowed, so a 107 KiB C++ file pays ~3.0 ms
      for the fold scan and ~3.4 ms for the locals query on every edit, and indent adds
      ~5 ms on the Enter path. Windowing them is not the obvious fix it looks like:
      `GutterModel`'s huge-file path already shows what a truncated fold window costs (a
      block whose closer falls outside the window folds to the window edge, which is why
      that path drops any range abutting its own tail). The real question is whether
      these become incremental -- MatchCache-style reuse keyed on the edit -- rather than
      windowed. Nothing pulled yet; typing no longer feels bad, which is what would drive
      it.
- [ ] **A cheap single `Parent()` call is still unaddressed.** The chain shape is fixed
      (`parse::NodeAncestorChain`/`grammar::Node::AncestorChain`, collecting a whole
      ancestor climb in one root-to-self descent instead of one re-descent per
      `.Parent()` link -- `Editor/Indent.cpp`'s two walks, Mode.cpp's expand-selection/
      pairwise-binding/sexp-motion walks, and the CLike/Org/Markdown indent ancestor
      walks all migrated), but a single isolated `Parent()` call is still a full descent,
      and `NodePrevSiblingImpl`/`NodeNextSiblingImpl` still call `NodeParent` once
      internally on every invocation -- so a loop that climbs by sibling rather than by
      parent (Mode.cpp's `sexpMotion`) still re-descends per level for that half. A cheap
      *parent* (not just chain) needs a memo per tree, which is shared mutable state the
      parse layer does not have today.
- [ ] One measured non-fix worth not re-trying: rewriting `ImprintBracket`'s
      `DelimitersOf` `Child(i)` loops as `ForEachChild` cursor passes made a fold scan
      ~25% *slower* (2952 -> 3714 us). A closer is nearly always a node's last child and
      its opener the first, so the reverse `Child(i)` scan stops after two lookups where
      a cursor pass visits every child. `Node::Child`'s "prefer ForEachChild" rule is
      about loops that walk ALL children; this is not one.

### Language Intelligence

- [ ] **Android device tooling** (the one part of the Java/Kotlin work below that
      didn't fall out of it). Editing, building and testing an Android project works
      today via Java/Kotlin modes + the generic task runner (`ned/set-task-command`
      pointed at `./gradlew ...`) + the bundled XML mode for layout files. What has no
      natural home in anything that exists: `adb logcat` streaming, and a one-click
      "install + run on device/emulator" flow. Deliberately left unscoped — worth
      building only if plain shelled-out `adb`/`gradlew` tasks prove too manual in
      practice, not speculatively.
- [ ] Whether Markdown fenced code blocks / Org `#+BEGIN_SRC` blocks should get the same
      real-LSP-sync treatment HTML `<script>`/`<style>` embedded documents already have
      is an open question — spawning a live language server per code fence in an
      ordinary notes file could be noisy for illustrative/incomplete snippets.

**Quick-fix gutter marker**

Shipped -- slug for `git log --grep=`: `code-action-hints`. The feasibility question
the Maybelist entry held this behind was answered by measuring rather than guessing:
a viewport-ranged `textDocument/codeAction` carrying `context.only = ["quickfix"]`
costs 0.4 ms median on clangd 23, 0.8 ms on gopls and 16-40 ms on
typescript-language-server, because the server answers from the fixes it already
computed when it published the diagnostics. The filter is load-bearing in the other
direction too: unfiltered, a wide range draws whole-file `source.*`/`refactor.*`
actions that attach to no diagnostic and so name no line (53 of them from gopls
alone). Four conscious cuts left behind.

- [ ] The marker only ever names a **diagnostic-attached** quick fix, because an
      attached diagnostic is the only thing that maps an action back to a line. A
      selection-scoped refactor (extract function/variable) and a whole-file source
      action (organize imports) light nothing, and asking per line to find them would
      be a request per visible line rather than one per viewport. `lsp-code-action`
      (`C-c C-a`) still reaches all of them at point.
- [ ] Prose-origin diagnostics are excluded from the request outright: the viewport
      request goes to the buffer's primary language server, and only the prose
      checker's own connection knows what a harper-ls-flagged word is. A prose
      diagnostic's "add to dictionary"/"ignore" fix is reachable from
      `lsp-code-action` (which does route to `kProseLanguageKey`) and never from the
      marker. Fixing it properly means a second viewport request on that connection.
- [ ] A marker retires when the range it sits in is re-answered, which needs a
      diagnostics publish or an edit to re-arm. A server that silently stops offering
      a fix without republishing its diagnostics leaves the marker up until the next
      publish -- not observed on any of the three servers measured, since a fix going
      away is a diagnostic changing.
- [ ] The column is reserved from the frame after the server opens the document, so
      opening a file in a server-backed language still shifts the gutter one column
      once -- the same one-time shift the diff, blame and symbol columns already make.
      Reserving it before the server attaches would mean paying a column in every
      buffer, LSP or not. (The breakpoint column made the opposite call as of
      `debug-panel`: it is reserved for any language with a configured DAP adapter,
      because that column is where the first breakpoint gets clicked, so gating it on
      one already existing was a chicken-and-egg.)

- [ ] `Lsp/Manager.cpp`'s `PathToUri` doesn't percent-encode, while its `UriToPath` now
      decodes (`lsp-document-link`, after clangd's own encoded targets proved every
      URI-carrying response was missing paths outside the unreserved set). Nothing has
      needed the outgoing direction yet — it would change the URI every `didOpen` sends,
      so it was left alone deliberately rather than overlooked. Revisit if a project path
      with a space/`#`/`?` in it ever misbehaves.

**LSP results as buffer-anchored data, not byte snapshots**

- [ ] The throttle is per buffer and per window, not per request kind: a viewport-scoped
      `semanticTokens/range` and a whole-document `codeLens` share one window even though
      only the first has any reason to move with the viewport. Harmless today (codeLens
      dedups on generation, so a viewport-only change costs it nothing), and splitting it
      would mean a timer per kind per buffer -- worth it only if a third viewport-scoped
      request kind ever shows up.
- [ ] `documentHighlight` is deliberately *not* carried forward (`BufferView.h`'s
      `DocumentHighlightState` says why): it describes the symbol under point rather
      than a span of text, so typing inside that symbol makes it a different symbol,
      which no relocation fixes -- and any edit moves point, which re-arms its own
      debounce. Suppress-and-re-request is the right model there. Revisit only if the
      one-debounce-window gap turns out to be visible in practice.
- [ ] Open policy question: a server's semantic tokens currently override any syntax
      class the grammar produced, including `comment`. No live case motivates it now --
      the phpantom_lsp one this entry used to cite was a misdiagnosis (checked against
      0.9.0 and 0.10.0: both classify `//x`, `#x` and `/*x*/` as `comment` and flag only
      the uncommented call), and 0.10.0's `[semantic_tokens] mode = "contextual"` default
      stops emitting comment tokens at all. Decide it when a server actually recolours a
      grammar `comment` cell.

**Unimplemented LSP surface** — audited 2026-09-21 by grepping every method string in
`Source/` against the 3.17 method list. Split by whether something is known to be broken
today, merely absent, or deliberately skipped, so a later pass doesn't re-litigate the
third group.

The file-operation family is complete as of 2026-09-21 -- slug
`file-operation-create-delete`: `didCreateFiles` (sent when a save first brings a
buffer's file into existence -- `Manager::ReportFileCreation`), and
`willDeleteFiles`/`didDeleteFiles` around `delete-file` (C-c C-k), which now asks a
server for the edits that keep the rest of the project compiling before the file goes,
exactly as a rename already did. `workspace/willCreateFiles` stays unsent and
undeclared: ned has no explicit create-a-file action, so there is no moment *before* a
creation to ask for edits at.

Everything in the "known to cost something" group shipped 2026-09-21 -- slug for
`git log --grep=`: `server-side-protocol-half` (`client/registerCapability`/
`unregisterCapability`, `workspace/didChangeWatchedFiles`, `textDocument/didSave`, and
the `window/log|show*` family). Live-verified against clangd and harper-ls: harper-ls no
longer logs "Unable to register watch file capability", its registration is honored, and
a deletion in a watched directory reaches it as a real `didChangeWatchedFiles`. What that
left behind:

- [ ] **Watched-file coverage is the directories an open buffer lives in, not the project
      tree.** `Editor/FileWatch.h` watches the parent directory of each open buffer, so a
      server's `**/*.php` registration is answered for siblings of what's open and for
      nothing else -- a `composer install` or a generator writing somewhere no buffer is
      open is still invisible to it. Widening this means watching the project tree
      (ignore-rules and inotify watch budget included), which is a different piece of work
      from the protocol half that shipped.
- [ ] Per-entry reporting flips on at the next background tick after a server registers
      its watchers (`WindowManager::ResyncFileWatcher`, ~5s), so file events in that
      window are missed. Inherent to refreshing the toggle from a poll rather than a hook,
      and harmless in practice: a server that has just registered has just finished
      indexing the tree it would be told about.
- [ ] `window/showMessageRequest` always answers null ("the user chose none of these").
      `Client::RequestHandler` is synchronous, so a real modal choice can't be driven from
      it -- the same constraint `workspace/applyEdit` works under. The message and the
      actions it offered do reach the user; only the answer is fixed.
- [ ] `window/showDocument` honors the path and the selection's start *line*. `takeFocus:
      false` is ignored (every pane ned could show a document in is one the user is
      looking at) and the selection's column is dropped with it, since the jump goes
      through `JumpToPathLine`.
- [ ] `window/showDocument` with `external: true` (or any non-`file:` scheme) goes
      straight to `Editor/Link.h`'s `OpenUrl` -- the configured opener, `xdg-open` by
      default -- with no confirmation. Deliberate: the server is a process the user
      already configured and could launch a browser itself, and the opener is
      exec-based (no shell) and disable-able with `(ned/set-url-open-command "")`.
      Revisit if a "server asked to open <url>, allow?" prompt ever earns its keep.
- [ ] A dynamic registration for anything other than `workspace/didChangeWatchedFiles` and
      `textDocument/didSave` is recorded and logged but changes no behavior. Mostly
      moot -- ned sends feature requests without consulting a capability and latches off
      a real MethodNotFound response -- but a server that registers, say, on-type
      formatting dynamically still has its trigger characters ignored, because those are
      read from the `initialize` response only.
- [ ] Observed consequence of `didSave`, not a ned bug: harper-ls advertises
      `textDocumentSync.save: true` and then logs "got a `textDocument/didSave`
      notification, but it is not implemented" on every save, which ned's stderr capture
      dutifully files as a WARN. Anyone editing prose sees one such line per save in
      `*Messages*`. Suppressing it would mean either second-guessing a server's own
      advertised capability or filtering a server's log text by content; revisit only if
      a second server does the same.
- [ ] `textDocument/willSave` / `willSaveWaitUntil` stay unsent on purpose: the latter is
      the spec's format-on-save hook and ned runs its own pipeline there
      (`format-buffer-lsp-tier`), which a server offering competing edits would fight.

*Absent, no equivalent elsewhere in ned:*

Triaged 2026-09-21 while deciding what belonged in 0.10, so the next pass starts from
the judgement rather than redoing it: what is left here is *absent*, not broken. The
file-operation and colour halves of that triage have since shipped; of the pair
standing here, the half that was pure latency plumbing has too.

`workspaceSymbol/resolve` shipped 2026-09-23 -- slug for `git log --grep=`:
`workspace-symbol-resolve`. A WorkspaceSymbol whose location omits a range
(`SymbolEntry::hasRange=false`) now resolves its real range from
search-everywhere's own accept path, the one place a stand-in position (top of
file) actually mattered -- not eagerly per row, which would have defeated the
"lazy" half of the point. Gated on `workspaceSymbolProviderFor(...)
->resolveProvider`, `ResolveCompletionItem`'s exact shape and capability-at-
the-call-site stance.

*Deliberately skipped -- reasons recorded so these don't get re-opened:*

- [ ] `inlayHint/resolve` -- **closed 2026-09-23 on a measurement.**
      `Tools/lsp-capability-probe.py --require inlayHintProvider` against every
      installed server: clangd and typescript-language-server advertise a bare
      `true` (no `resolveProvider`), gopls advertises `{}` (same), and
      lua-language-server is the only one that sets `resolveProvider: true`. A
      live probe against clangd (a real parameter-hint request on a two-argument
      call) confirms the bare-`true` case isn't hiding an inline tooltip either
      -- the response carries `label`/`kind`/padding and nothing else. Building
      this would mean parsing `InlayHint::tooltip`/`raw`, a capability extractor,
      a `ResolveInlayHint` request, and -- the actually expensive part -- teaching
      the mouse-hover popup to hit-test `RenderedVirtualText` spans instead of
      just buffer byte offsets (`RequestHoverAtOffset` only does the latter
      today), all to light up for one installed server with an unverified
      payload even there. Reopen only on evidence a server actually installed
      here returns a real tooltip through it.
- [ ] `workspace/diagnostic` -- **closed 2026-09-22 on a measurement, and the gap it
      described was closed by another route.** No installed server advertises it:
      rust-analyzer, the only one here implementing pull diagnostics at all, sets
      `diagnosticProvider.workspaceDiagnostics: false`, and clangd/gopls/pylsp/
      typescript-language-server/lua-language-server/phpactor/jdtls/harper-ls declare no
      `diagnosticProvider` whatsoever (`Tools/lsp-capability-probe.py`). Worth knowing
      for the next pass: ned's existing `textDocument/diagnostic` path is therefore
      also unexercised against everything installed except rust-analyzer.
      What the entry actually wanted -- a problem list that is not limited to open
      buffers -- shipped instead off the *push* side, slug for `git log --grep=`:
      `keep diagnostics for files with no open buffer`. Servers already volunteer
      findings about files nobody opened (measured: rust-analyzer via cargo check,
      gopls per package) and ned was discarding them for want of a `text::Buffer` to
      hang them on. Reopen only if a server turns up that implements the request AND
      reports something its own publishes do not.

- [ ] `textDocument/foldingRange` -- ned folds from its own grammar (`ImprintFold.h`), for
      every language, with no server required. Worth revisiting only for a language that
      has a server but no ned grammar.
- [ ] `textDocument/selectionRange` -- ned already has a native equivalent, for the same
      reason folding does: `expand-selection`/`shrink-selection` (`M-=`/`M--`, slug for
      `git log --grep=`: `structural-selection-expansion`) walk the enclosing named-node
      chain of ned's own tree, so they work in every parsed language, offline, with no
      server -- where the request would hand the feature only to whoever happens to run a
      server that implements it. `Mode::expandSelection` adds a step no grammar has a node
      for (the interior of a delimited body, expand-region's "inside pairs") off the
      imprint table. The `selectionRange` hits in `Lsp/Content.h` are `DocumentSymbol`'s
      unrelated field of that name. Revisit only if a server proves it knows something the
      tree doesn't.
- [ ] `textDocument/moniker` -- cross-repository symbol identity, useful only with an
      index ned has no consumer for.
- [ ] `textDocument/inlineValue` -- **closed 2026-09-22 on a measurement, after being
      built up to and then measured out of.** ned already owns both ends (a DAP session
      knows the values, the inlay-hint path draws inline text), and the half that needed
      no server shipped the same day -- slug for `git log --grep=`:
      `inline-debug-values-scoped`. What killed the LSP half is what the request
      actually returns. Of its three result variants, only
      `InlineValueEvaluatableExpression` is beyond a parse tree (it needs a DAP
      `evaluate` per expression per stop, plus the async cache that implies);
      `InlineValueText` is a string the server composed; and
      `InlineValueVariableLookup` is "look this name up in the debugger", which is
      exactly `Dap::Manager::FrameLocals` plus the scope resolution ned now does
      natively. Asked against a real PHP file, phpactor answered with seven results, all
      seven `InlineValueVariableLookup` -- nothing ned does not already compute for
      itself, for one language, at the cost of a round trip per viewport. Adopting it
      would be a strict downgrade.
      The capability hunt behind that, so nobody repeats it: probe with
      `Tools/lsp-capability-probe.py --require inlineValueProvider`. Ruled out --
      clangd 23, gopls, pylsp, typescript-language-server, lua-language-server,
      harper-ls, and (the obvious guess, wrong) jdtls, checked including dynamic
      registration, the form jdtls uses for eight other capabilities and which would
      make a static-only probe report a false negative. The five real implementations,
      found by searching for who SETS the capability rather than who declares the type
      (ocaml-lsp, nim langserver and elixir-ls only declare it): phpactor,
      AdaCore's `ada_language_server`, the Dart analysis server, `FsAutoComplete`, and
      R's `languageserver`.
      Reopen only on evidence that some server returns
      `InlineValueEvaluatableExpression` in practice -- that is the one variant that
      would buy something, and the four implementations other than phpactor were not
      measured. `workspace/inlineValue/refresh` would be a fifth `RefreshKind`, and the
      request needs `context.frameId`/`stoppedLocation` from DAP, a fact `Lsp::Manager`
      cannot reach today.
- [ ] `notebookDocument/*` -- no notebook editing surface exists to sync.
- [ ] `textDocument/inlineCompletion` -- ACP is ned's answer to this shape.
- [ ] Minor interop note, not a ned defect: ned answers `workspace/configuration` with
      JSON `null` per unmatched section, which the spec allows; harper-ls rejects it with
      "Settings must be an object" on every request. Only worth revisiting if a second
      server objects.

**Colour swatches**

Shipped -- slug for `git log --grep=`: `color-swatches`. A cell painted in the colour
every visible literal names, plus `color-at-point` (`C-c #`) to rewrite one in another
notation. Built native-first and LSP-second, which is the opposite of how the roadmap
entry it closes was written, on a measurement: of every server installed here, exactly
one (lua-language-server) advertises `colorProvider`, and no CSS-family server is
installed at all (`Tools/lsp-capability-probe.py`). An LSP-only build would therefore
have shipped swatches in Lua and nowhere else. `Editor/ColorLiteral.h` is the recogniser
(pure, unit-tested), `Lsp::Manager::DocumentColorSpans` the additive server tier, and
`Editor/ColorSwatchSettings.h` the `block`/`underlay` switch. A swatch rides the same
per-line span list an inlay hint does (`bufferview::RenderedVirtualText`) rather than a
second virtual-text mechanism, so the four column walks that have to count it already
do. Two recorded decisions worth not re-litigating: a presentation's own `textEdit`
range is ignored (it can only ever be the range ned already found, and honouring it
would let a server move an edit made in place), and a server-reported colour is dropped
outright once anything is typed inside it rather than clamped like a code lens -- a
swatch claims *these bytes* spell that colour, and ned's own scan has the right answer
for the edited text anyway. Three conscious cuts left behind; the fourth (no
interactive picker) closed 2026-09-22 -- see below.

- [ ] `lab()` / `lch()` / `oklab()` / `oklch()` are not recognised. Parsing them is
      trivial; the round trip is not. Those spaces are wider than sRGB, so offering
      "the same colour as `#rrggbb`" for an out-of-gamut `oklch()` would silently
      rewrite a P3 colour as its clipped sRGB twin -- data loss wearing a conversion's
      clothes. Admitting them needs a gamut-mapping policy first, which is its own
      decision, not a chore.
- [ ] The scan reads at most 8 KiB of each visible line, the same bounded-walk tradeoff
      `kMaxTabAwareColumnScan` makes: a literal past that column earns no swatch. The
      bound exists because the scan is a real copy out of the rope and
      `PerformanceTest.cpp`'s pathologically-long-line case caught the uncapped version
      redoing five million bytes per frame. Minified CSS is the one real file this
      costs. Widening it means windowing on the horizontal scroll, and both producers
      of the span list would have to agree on that window within a frame or the cursor
      drifts -- which is the whole reason the cap is a function of the line alone.
- [ ] Wrap segmentation still does not count virtual text against a row's width
      (`ComputeWrapSegments` sees links and codepoints, not `RenderedVirtualText`), so a
      soft-wrapped line carrying swatches or inlay hints breaks a little later than it
      should. Pre-existing -- inlay hints have always had it -- and now visible in one
      more place.

**Colour picker**

Shipped -- slug for `git log --grep=`: `color-picker`. `pick-color` (`C-c #`, taken from
`color-at-point`, which keeps its name on `M-x`) opens `UI/ColorPicker.h`, a focus-taking
overlay on ThemeGallery's shape: seven channel rows (R/G/B, H/S/L, alpha), a before/after
preview, and a WCAG readout of the colour against the theme's text and background. Each
slider track is painted as *that channel's own ramp* rather than a filled bar -- "what
would this row's colour be at each position" is the question a picker exists to answer,
and the cost is one `HslToRgb` per cell. Point on no literal opens on a neutral grey and
inserts rather than replaces, which makes it a way to write a colour you do not have yet.
Four decisions worth not re-litigating:

- The picker holds *both* representations, RGB authoritative for the value and HSL
  authoritative for the H/S/L rows, and retains hue across any edit that lands on a grey.
  `RgbToHsl` is lossy at the achromatic extremes (documented on the function, now public
  alongside `HslToRgb` in `Editor/ColorLiteral.h`), so a colour driven to black through
  the L slider would otherwise come back with hue 0 and never return. Saturation is *not*
  retained -- a grey genuinely has none, and a row claiming otherwise would lie.
- The notation is inside the picker (`Tab`), not a second menu: the value row always
  shows the exact text an accept writes, and it follows a colour across the
  opaque/alpha boundary via `AlphaSibling` so nudging the alpha row does not silently
  reset a chosen `hsl()` to hex.
- The buffer is never edited while adjusting -- one edit on accept, so the undo tree
  gets one entry rather than one per arrow key and the swatch scan is not re-run per
  keystroke. No live preview in the buffer for the same reason.
- The contrast readout answers `-`, not a number, when the theme's background is the
  terminal's own with nothing detected behind it. `ContrastRatio` returns its *maximum*
  (21.0) for an unmeasurable pair, which would have painted a perfect score where there
  is no score at all -- caught in the first live run, not by a test.

- [ ] No LSP tier: the picker offers ned's own notations only. A colour a server *names*
      (`rebeccapurple`) is a conversion, which is what `color-at-point` is for, and
      `colorPresentation` has nothing to say about a colour the user has not chosen yet.

**LSP completion fidelity**

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

**Merged completion sources**

Shipped -- slug for `git log --grep=`: `completion-source-merge`. `Editor/Completion.h`
is the source-neutral candidate type, `Editor/CompletionSources.h` the four producers
and the merge. Two conscious cuts left behind.

- [ ] Buffer words are collected from `Buffer::Text()` -- a whole-document copy -- on
      every completion request, which is why a huge buffer (`ITextStorage::IsHuge()`)
      is skipped outright rather than scanned incrementally. A windowed scan (the
      viewport plus some margin, the shape the huge-file search work already uses)
      would give a huge file back its buffer words; nothing has needed it, since a
      huge file is usually one being read rather than one being typed into.
- [ ] Snippet and buffer-word candidates are collected once, at request time, and
      re-ranked from there for the rest of the session -- correct for narrowing, and
      for widening back to the prefix the session started at, which is the only
      widening `CompletionSession` permits. A *server* answering `isIncomplete` re-asks
      and re-collects; a local source has no equivalent, and needs none as long as the
      collection is a superset of every prefix the session can reach.

### Parsing Engine: Trait Vocabulary over Per-Language Queries

Full design in `Docs/ParsingEngine.md`. Phases 0 through 4b are complete; design history
is pruned here per this file's own convention — see `git log --grep="Phase [0-9]"` and
`Docs/ParsingEngine.md` for the full record. Chain, briefest form: Tier 0 infers delimited
bodies/spans/nesting/delimiters straight from `grammar.json`; Tier 1 is composable
declared traits, authored as Janet data (`.scm` left the repo 2026-09-12); Tier 2 is Janet
escapes for arithmetic/host-state predicates a query can't express (Org's TODO keywords).
`QueryMatcher` replaced `ts_query` as the production matcher (Phase 4a, 2026-09-12, later
given a 60x cursor-walk fix). Phase 4b (2026-09-13) replaced the tree-sitter runtime
itself with `Source/Editor/Parse/` (`ned::editor::parse`), a from-scratch green/red tree +
GLR stack + incremental reuse + error recovery engine interpreting each grammar's own
ABI-13/14/15 tables directly — gated on upstream conformance (~2,800 cases across all 24
grammars), incremental-vs-scratch and red-layer differentials, and a `[Performance]` win
over the old runtime it replaced. `MatchCache` (2026-09-13) followed as the per-subtree
fact-memoization payoff Phase 4b's stable node identity made possible, wired through
`symbolKind`, `localScopes`, fold, `testDiscovery`, and indent's query path (highlight is
deliberately excluded — already windowed via `MatchesInRange`, see the keystroke-
performance section above).

Two items remain open, both conscious calls rather than defaults:

- [ ] **Keeping tree-sitter grammar ingestion is a hard constraint** -- at import time
      now, not runtime: `ned --import-language` converts a repository's `grammar.json`
      to `grammar.janet` once, and ned's own generator (`Editor/Grammar/Compile/`) and
      engine (`Editor/Parse/`) do the rest (2026-09-17/18: no tree-sitter code, headers or
      runtime anywhere in the tree; 74 languages bundled through that path). It still
      forecloses the resilient-LL path (matklad's) that would give better error recovery,
      because LL means hand-written grammars and therefore losing every language nobody
      here personally writes a grammar for.
- [ ] **Not extracting this as a standalone library.** The engine has legitimate pull into
      ned specifics (Janet host callouts, `Mode`'s capability surface, `SyntaxClass`);
      designing library-first would make it worse at the job it exists for. Extract later
      if it earns it.
- [ ] **Table-generator outliers** (measured 2026-09-18 at the Tier B systems batch,
      single-core CPU, `ned --compile-language`): ada 149s for only 2,207 parse states
      (its case-insensitive keyword tokens -- `[pP][aA][cC][kK][aA][gG][eE]` and every
      other reserved word -- multiply lex-state construction and token-conflict work),
      nim 70s / 20,305 states, odin 60s / 9,611, kotlin 40s, crystal 30s. Absorbed by the
      parallel build-time compile (`CMake/LanguageTables.cmake`), so nothing is checked
      in; profile with `perf record --call-graph dwarf` before touching it -- the known
      remaining cost is item-set construction (`ParseItem::Order` / `TokenSet::Compare`
      under `std::map`), and ada suggests the lex side has its own hot spot.
- [ ] **QueryMatcher supertype-scoped names are membership-only.** `(expression/variable)`
      (2026-09-18, for haskell's upstream highlights) checks that `variable` is one of
      `expression`'s declared subtypes at compile time and then matches by the subtype's
      symbol; whether the node actually sits under a hidden `expression` in the tree is
      not consulted, so `(pattern/variable)` and `(expression/variable)` match the same
      nodes. The cursor stack carries the hidden ancestors (tree_cursor.c's field walk
      already climbs them), so a positional check is a small addition if a query ever
      needs the distinction.

**Language coverage** — full catalogue in `Docs/LanguageCoverage.md`: the depth ladder
(D0 structural / D1 navigational / D2 integrated / D3 bespoke), Tier A flagship through
Tier D parked, a graveyard with revisit triggers, and an 8-point grammar admission policy.
The reframe that makes it affordable: **a tier is a commitment to a depth, not a decision
about whether a language works at all** — D0 falls out of Tier 0 inference for free, so
"basically every known language" becomes a real target rather than a boast, and the honest
answer to a request for an obscure DSL becomes "yes, next release".

- [ ] Admission policy worth knowing before adding any grammar: **prefer
      `tree-sitter-grammars/*` over the original personal repo, and never use star count as
      a health signal.** Measured 2026-09-11 — `alemuller/tree-sitter-make` is 51★ and stale
      since 2024-01 while `tree-sitter-grammars/tree-sitter-make` is 17★ and current;
      `MunifTanjim/tree-sitter-lua` is a 1★ fork against the org's 104★. Use `pushed_at` and
      `archived`, pin by tag, and record ABI version + external-scanner LOC + corpus presence
      at admission.

### Refactoring

**Sidecar metadata: association without binding**

- [ ] The eight existing tenants are **not** migrated onto anchors, and that is a cost
      finding rather than a leftover: each accessor (`Diagnostics()`, `SnippetRanges()`,
      `ExcerptRanges()`, `SecondaryCursors()`, `FoldMarkerAt()`) has 58-108 call sites and
      returns a reference to stored state, so a migration means either touching all of
      them or materializing a vector per read -- and `Diagnostics()`/`SecondaryCursors()`
      are read inside `Paint()` loops. They already relocate correctly through the one
      feed, so the churn buys nothing visible. Snippet ranges were the candidate
      exception — "migrate them and nested placeholders fall out" — and building nesting
      for real showed that to be wrong (`git log --grep=nested-snippet-stops`): `Buffer`
      already relocated each endpoint independently under its own gravity, so anchors
      offered nothing it lacked. What nesting actually needed was *carried ancestry*
      (which field encloses which — a placeholder that is exactly one nested stop shares
      its parent's span, so geometry can't say) plus a gravity that changes on every TAB,
      and `AnchorSet::Create` fixes a policy for the anchor's lifetime. A tenant whose
      gravity is dynamic is the shape anchors do **not** fit today; that is the finding,
      and it applies to any future migration candidate too.
- [ ] **`IncrementalParseCache` re-derives an edit the buffer already recorded exactly.**
      `Editor/Grammar/IncrementalParse.h` keeps `lastText_` -- a second full copy of the
      document -- and reconstructs one changed region per call by common-prefix/suffix
      diffing it, which is what `Buffer::Edits()` has held exactly since
      `sidecar-anchors`. Cost, not wrongness: the reconstruction is conservative (two
      distant edits widen to one span covering both, so the parser reuses fewer subtrees
      but never a wrong one). The reason it diffs is a real constraint, not an oversight
      -- every `Mode` capability is a pure function of "the buffer's full current text",
      with no `Buffer&` and no edit-delta parameter, which is what keeps `Mode` a
      copyable value type a test can drive with a bare string. So this is a change to
      that seam, not to this file. Worth doing only once the doubled resident text
      actually shows up in a measurement (`Tests/KeystrokeBench.cpp`, a large file, RSS
      per open buffer) rather than on principle.
- [ ] **The background-loop-plus-`EventLoop::Post` machine is hand-rolled in five more
      places.** `Editor/Protocol/FramedConnection.h` exists because three protocol
      clients each rebuilt the same thing and a member-declaration-order invariant was
      defended only by a comment repeated in three files. `Editor/Mcp/BridgeServer` is a
      fourth copy of exactly that -- jthread read loop, `shared_ptr<bool> alive_` captured
      by value into every `Post`, and the same "declared last so its destructor runs after
      the body has unblocked it" comment -- and `Terminal/PtyProcess`, `Tasks/TaskProcess`,
      `FileWatch` and `ModePrewarm` each carry a thinner version. `FramedConnection`
      cannot be reused as-is: it owns one spawned `TransportT` by value (which is what
      enforces the destruction order), while the bridge accepts N socket connections and
      the other four aren't framed protocols at all. So the honest options are extracting
      the per-connection half into something the bridge can hold one of per accepted
      connection, or leaving it -- not a migration. Nothing here is known-broken; the
      cost is that the next bug of the shape `lsp-use-after-free` was has five places to
      hide in instead of one.
- [ ] Anchors have no Janet surface. Deliberate for now: the C++ seam has one consumer
      shape so far, and a scripted holder that leaks handles leaks them forever (there is
      no RAII handle -- `Buffer` is move-only and an anchor outliving its owner would
      dangle across a `Clone()`). Revisit when a plugin actually wants to mark a position.
- [ ] Explicitly *not* a fix for stale server data: an anchor placed at a wrong offset
      stays faithfully wrong, which is exactly what the 2026-09-18 phpantom_lsp session
      turned out to be (the server's own `character` values disagreed with its own
      document; ned's conversion and relocation were both correct).
- [ ] A Lisp binding vector's names are captured by unrolled per-pair-index patterns
      (`clojure-locals.scm`, `janet-locals.scm`), because the whole-vector form a query
      would naturally express captures a bare-symbol *value* as a definition and turns a
      rename of the outer binding it names into a partial one. The unrolling stops at
      eight pairs, and destructuring (`[{:keys [x y]} m]`) is not captured at all; both
      degrade to "declines" rather than to a wrong rename. Lifting either needs something
      the query language cannot say, so it would mean a capture kind `LocalScopes.h`
      understands positionally -- not worth it until a real file hits the cap
      (`lisp-shell-locals`).
- [ ] Fish's `set -l -x count 0` (a scope flag not adjacent to its own target) and
      `read -l line` are not captured as definitions -- the first because the pattern
      anchors the name to the flag before it, the second because there is no `set`
      command node to hang off. Both decline rather than mis-resolve
      (`lisp-shell-locals`).
- [ ] A use that textually precedes its own binding in a whole-scope-binding language
      (Python's function scope, JavaScript `var` hoisting) is detected and *declined*
      rather than resolved -- `LocalBinding::usedBeforeDefinition`, the one case where
      the resolver's position rule knowingly gives up. Resolving it properly means
      knowing per language whether binding is declaration-point or whole-scope, which is
      a real per-language fact this deliberately did not invent a place to record
      (`scope-aware-rename`).
- [ ] A huge buffer (`ITextStorage::IsHuge()`) never gets the scope-aware tier at all.
      Unlike the fold/symbol/test gutters beside it, this one cannot window: a binding's
      occurrence set is only complete if the whole file was parsed, so a windowed answer
      would be a partial *rename*, not a partial display (`scope-aware-rename`).
- [ ] A rename carrying filesystem resource operations (a `documentChanges` edit that also
      creates, deletes or renames a file) is applied directly rather than reviewed -- a
      multibuffer has no way to represent a resource op, and reviewing half an edit is
      worse than reviewing none of it. Same for a huge source among the touched files, and
      for a server whose `newText` isn't the new name verbatim (a qualified or re-cased
      replacement, which the review has no way to propose). All three say so in the echo
      area rather than behaving differently in silence (`rename-review`).
- [ ] An excerpt mixing a real reference with a comment/string hit on the same line is one
      row with three states, not two independently togglable hits -- `M-a` steps it from
      original to references-only to every occurrence. Per-hit granularity would need the
      review to carry sub-excerpt ranges, which nothing else in the multibuffer does
      (`rename-review`).
- [ ] The comment/string candidate scan covers only the files the rename already touches.
      A name that appears in a comment in a file with no code reference at all is never
      offered -- that would be a project-wide search, which `project-replace` already is
      (`rename-review`).
- [ ] An import fixup only ever rewrites a specifier in the style it was already written
      in, and declines anything that style cannot express: an angle-form system include, a
      PHP `use` namespace, a Rust `mod` declaration, a bare package specifier, and a target
      that moved out of the root its specifier counts from (a project-root-relative
      `"Editor/Widget.h"` moved outside the project). Declines are counted and named in the
      review's status line rather than guessed at. Lifting any of them means teaching the
      rewriter a second style to write in, which is what the "never restyle" rule exists to
      refuse (`file-rename-propagation`).
- [ ] An externally detected move is only seen inside a directory ned already watches --
      the parent of an open buffer -- and only paired when BOTH ends are such a directory.
      A rename inside one always pairs (the moved file itself need not be open); a move
      into a directory nothing is open in delivers inotify's `IN_MOVED_FROM` half alone,
      which still triggers the existing sweep but has no destination to name, and none is
      guessed at. Watching the whole project tree would fix both and cost a watch per
      directory, which is the trade that was declined (`file-rename-propagation`).
- [ ] An externally detected move opens its review unprompted, switching the focused pane
      to it. That is the point -- the imports are broken either way and the review is
      non-destructive -- but a branch switch that moves many files will interrupt whatever
      was on screen. A "N imports need fixing (C-c i to review)" status line instead, with
      the review built on demand, is the obvious alternative if it turns out to grate
      (`file-rename-propagation`).
- [ ] The import scan runs synchronously on the main thread, inside the rename itself:
      one `SearchDirectory` pass for the moved file's own name, then a tree-sitter parse of
      each file that matched. Measured at ~1.2s for the worst case in ned's own tree
      (renaming `Mode.h`, whose stem half the codebase mentions) and far less for an
      ordinary file. Bounded by `ned/set-import-fixup-max-files` and skipped outright for a
      move with no end inside the project root. Worth moving off-thread only if a real
      repository makes the pause visible (`file-rename-propagation`).
- [ ] The automatic offers ride on a SERVER rename landing, or on a `*rename*` review being
      committed -- never on the no-server in-buffer rewrite, because renaming a top-level
      type is always cross-file and `rename-symbol`'s scope-aware tier declines it by
      construction (`LocalBinding::scopeIsFile`). With no server configured for a language,
      `rename-file-to-match-type`/`rename-type-to-match-file` are the whole feature; they
      need none (`class-file-sync`).
- [ ] `rename-type-to-match-file` with no server renames the declaration and its whole-word
      occurrences **in that file only**, and says so. The "top-scoped name pair" is the
      reliable half -- this file's one top-level type is named after this file -- and the
      dependency tree is the half it cannot follow. The obvious lift is a project-wide
      whole-word scan into the same review (`Project/Search.h` plus `ClassifyHit`, which is
      what `project-replace` already is), deliberately not built into this: a rename that
      silently rewrote matches across a repository on a name match alone is a different,
      riskier feature than the one asked for (`class-file-sync`).
- [ ] Java/C#/PHP/TypeScript tags coverage is now upstream's file plus a repo-local delta
      (`ned_embed_treesitter_query_concat`). The deltas are small and current; the thing to
      watch is a grammar bump making one redundant, which shows up as a duplicate marker
      rather than a wrong one (`class-file-sync`).
- [ ] **Change signature (the hard one, scoped honestly).** LSP has no request for this --
      JetBrains does it from its own index, and no server offers an equivalent -- so it is
      ned's own transform or nothing. Renaming a parameter falls out of the `locals.scm`
      item above and needs nothing else. Adding, removing or reordering parameters means
      rewriting call sites, which needs a per-language structural query (a `calls.scm`
      alongside `tests.scm`, mapping a call expression to its argument list) plus an edit
      planner that maps old positions to new ones and drops or defaults the rest. First cut
      should be one language family and the review buffer above making the blast radius
      visible before anything lands; a signature change that silently rewrote fifty call
      sites would be the least trustworthy feature in the editor.

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

### Navigation & Search

- [ ] Excerpt-scoped search covers isearch and query-replace only. A multibuffer's
      chrome is also visible to `next-error`, dabbrev completion and Vim-mode `/`
      search, none of which consult `multibuffer::ExcerptBodyRanges`
      (`multibuffer-scoped-search`) — none has been annoying enough in practice to
      chase, and each would need its own scope plumbing rather than sharing one.
- [ ] A real visual side-by-side 3-way merge/diff view. `AutoMerge` auto-resolves the
      common case and drops real `<<<<<<<`/`=======`/`>>>>>>>` conflict markers into the
      buffer for a genuine divergence, but a real conflict is still hand-edited text,
      not a visual diff — see "Merge Conflict Resolution Mode" below, which scopes a
      chord/mouse-driven *resolution* workflow over these same markers without needing
      this visual diff first.

### Editor Ergonomics

- [ ] `C-x` itself is still vim's own "decrement number under point" in Normal mode
      (`Engine::HandleAction`'s Control-chord block), so ned's Emacs-style `C-x 2`/
      `C-x 0`/`C-x o` window-split/-close/-cycle prefix still has no way to reach
      `Dispatcher` under Vim mode — confirmed live (`vim-keymap-fallthrough`'s own
      fallthrough deliberately leaves every Control chord vim already recognizes, `C-x`
      included, alone). Left alone deliberately rather than fixed: `vim-window-commands`
      gives window management a real vim-native path (`C-w` prefix, `:sp`/`:vs`/`:on`)
      that needs no resolution of this conflict at all, and the only way to actually
      reach `Dispatcher` through `C-x` would mean either breaking real vim's own
      decrement-number binding or a two-key lookahead hack — not worth it now that the
      practical gap (no way to split/close/cycle windows under Vim mode) is closed.
**Status gutter**

Both halves shipped -- slug for `git log --grep=`: `status-gutter-unseen-content`. The
swatch is suppressed on a read-only buffer, and the freed cell carries an unseen-content
marker there instead (`Buffer::UnseenContentTracked`/`SeenByteOffset`,
`GutterModel::FirstUnseenLine`, `ned/set-unseen-content-marker` and its `band`/`boundary`
styles). `Buffer::AppendWhileReadOnly` turned out to be the whole answer to "which
buffers does this mean anything for": it is the one feed that can make content unseen, so
the unseen region is always one contiguous tail and any edit that is not a pure tail
append disarms the frontier rather than leaving it pointing into rewritten text. Three
conscious cuts left behind.

- [ ] Only a streaming buffer marks. A read-only buffer rebuilt wholesale (`*debug*`,
      a results buffer regenerated from scratch rather than appended to) disarms
      tracking on the rebuild and never shows the marker -- correct, since the frontier
      was measured against text that no longer exists, but it means the feature covers
      `*lsp log*`/task output/test results and nothing else. A rebuild that wanted a
      marker would need a content diff, not a byte offset.
- [ ] The frontier is committed in `ActiveBuffer::Set`, which is the last point the
      outgoing buffer is guaranteed alive. Two consequences: closing a pane or window
      without switching buffers never commits, and two panes showing the same log share
      one frontier -- whichever leaves first marks as seen whatever the other one was
      displaying. Both need a per-pane frontier to fix, which is a map keyed by buffer
      with the lifetime problem that implies.
- [ ] `Manager::HasUnseenLogEntry`/`AcknowledgeLogEntry` still track the same
      unseen/seen edge for `*lsp log*` at whole-buffer granularity, for the echo-area
      surfacing, and are not derived from the new frontier. Two mechanisms for one
      fact; worth merging only if they drift.
- [ ] The search-everywhere preview footer shipped (slug for `git log --grep=`:
      `search-everywhere-preview`) -- one conscious cut left behind. The excerpt is
      painted in one brush, unhighlighted: a per-file `Mode::highlight` on every
      selection change is a parse the popup cannot afford, and `ListPopupRow` carries
      one foreground per column rather than spans, so a highlighted footer needs the
      widget to grow a span-aware row first.
- [ ] `describe-bindings` (`C-c ?`) and `Docs/Commands.md`'s key column both shipped --
      slug for `git log --grep=`: `describe-bindings`. Two conscious cuts left behind.
      The report's "Major mode" section is whatever mode was active when it ran, and
      there is no way to ask for another language's layer without opening a file in it;
      a mode argument would mean instantiating a `Mode` the pane isn't showing.
      `Docs/Commands.md`'s key column is the default global keymap alone, for the same
      reason from the other side: the page is generated from a static registry, so a
      per-mode column would be a different, per-language page.
- [ ] **Transient mode covers the five VCS tools whose editor filenames are fixed, and
      nothing else.** Shipped, slug for `git log --grep=`: `transient-mode`
      (`Editor/TransientSession.h` -- `--transient`/`--no-transient` plus automatic
      detection, verified live against git/hg/svn/jj/fossil by running each tool with a
      probe editor. Composition of switches that already existed rather than new gating:
      the quit-time saves are the load-bearing half, since `SaveFilePlaces(force)`/
      `SaveRecentFiles(force)` write the in-memory store without consulting their own
      enabled flag). A tool whose editor file is randomly named -- `crontab -e`,
      `sudoedit`, `cvs`, `gh pr create` -- cannot be detected and needs
      `EDITOR="ned --transient"`; that is inherent, not a todo. Three conscious calls
      worth not re-litigating: the mode suppresses what ned records on the user's behalf
      (save-place, recent files, session, undo, backups) but **not** what the user
      deliberately asked for, which is why bookmarks still save and `bookmark-set` instead
      grew a guard refusing the message file itself; `DiagnosticsLog` still writes (it is
      a log about the process, not state about the file); and there is deliberately no
      `ned/set-transient` Janet setting -- the mode is chosen at invocation, and the Janet
      surface is a 1.0 freeze commitment.
- [ ] Terminal-side mouse forwarding and the OSC 52/title relay both shipped -- slug for
      `git log --grep=`: `terminal-mouse-and-osc-relay`. Two conscious calls left behind,
      each its own item below.
- [ ] An OSC 52 *query* from a program inside `TerminalPanel` is never answered
      (`Emulator`'s selection-query callback returns libvterm's "not handled"), so such a
      program can write ned's clipboard but never read it — xterm's own default, and the
      alternative hands the user's clipboard to every subprocess. Revisit only against a
      real need.
- [ ] An application-set title (OSC 0/2) replaces the terminal tab's static label
      unconditionally, truncated to 24 columns, with no setting to turn it off. A noisy
      PS1-driven title is the case that would want one; the Janet surface is a 1.0 freeze
      commitment, so it waits for a real complaint rather than landing speculatively.
- [ ] **DAP gaps, remainder**: data breakpoints shipped -- slug for `git log --grep=`:
      `dap-data-breakpoints` (`dataBreakpointInfo`/`setDataBreakpoints`, armed from a
      `*debug*` buffer variable row's own `[owner:M]` container reference, listed and
      removed via `[data:N]` rows in the same buffer). Thread-focus reattachment across a
      session restart is deliberately excluded even if revisited — a fresh session has
      entirely new thread IDs, nothing meaningful to reattach it to.
- [ ] A data breakpoint is session-scoped and deliberately not persisted: the `dataId` is
      minted by one adapter for one run, so the store is cleared in `EndSession` alongside
      the exception filters and for the same reason. DAP's own `canPersist` flag is read
      but not acted on — a persistable id still needs the next session to be the same
      build of the same program, which nothing here can check. Revisit only if an adapter
      that sets it turns out to make re-arming after a restart genuinely tedious.
**Debug panel**

Shipped -- slug for `git log --grep=`: `debug-panel`. `Source/UI/DebugPanel.h`, the third
`LeftDock` rail panel beside Files and VCS, over `TreeView` rather than `ListPopup`
because it is sections containing files containing breakpoints. One panel rather than one
per store, for the reason VS Code's own Run-and-Debug sidebar is one: a rail glyph per DAP
concern would be four glyphs that say nothing whenever no session is live, and the
sections share a single refresh path. Sections: call stack (threads, their frames), the
focused frame's scopes and variables, watches, all four breakpoint stores, and the two
inventory listings. The breakpoint half works with no session at all -- line and function
breakpoints are process-wide and persisted, so this is where they get armed before
anything is launched. Cuts left behind, each its own item below.

- [ ] The panel's Watches section and the `*debug*` buffer's own now show the same
      expressions through two entirely separate fetch paths (`DebugPanel::FetchWatchValues`
      and `BuildDebugInfoLines`'s chunked fan-out). Deliberate: the `*debug*` buffer is
      also what `dap-ask-agent` sends to an agent, so it stays a text rendering rather
      than becoming a view of the panel's model. Worth merging only if they drift.
- [ ] `DebugPanel` re-fetches every scope and variable of the focused frame on each stop,
      and `Manager::RefreshFrameLocals` independently fetches the same non-expensive
      scopes for the inline values. Two overlapping fan-outs, kept apart because the
      panel needs the tree (per-`variablesReference` children, lazily expanded) and the
      painter needs a flat name→value map it can read synchronously. Measured at nothing
      so far -- both are a handful of requests per stop -- but it is duplicated work, and
      a `Manager`-side variables cache keyed by reference would serve both.
- [ ] A variable row's tree depth is recomputed in `PushModel` from a map of
      container-reference → depth rather than stored on the row, which works only because
      a container is always an earlier row than its children. True by construction today
      (`AppendVariableRows` is a pre-order walk); a future row source that isn't would
      silently mis-indent rather than fail.
- [ ] Inline debug values resolve a local to a line through the language's own locals
      query (`Editor/InlineDebugValues.h`'s `ResolveInlineDebugValues`, slug for
      `git log --grep=`: `inline-debug-values-scoped`), which needs one -- so the
      whole-word text search it replaced is still the tier a mode without a locals query
      gets, and so is a huge buffer, which is never handed a whole-document query at
      all. That tier keeps its old failure mode: a value against a line mentioning the
      same word in a comment or a string. Two conscious calls in the scoped tier: a mode
      that HAS a locals query but captured nothing draws nothing rather than falling
      back (precision over recall -- the query has said there is no binding here), and
      only the innermost binding the stop can see is annotated, because the adapter
      reports one value per name and that is the one it means. At most three per line,
      and only in the file the debuggee is actually stopped in.
- [ ] Inline values show only the focused frame's *locals*, never a watch or an
      arbitrary expression, and never a field of a composite. The flat name→value map
      they read is exactly what the non-expensive scopes report.
- [ ] `Manager::SnapBreakpointToValidLine` moves a newly-set breakpoint to the nearest
      line the adapter says can hold one, within an 8-line window. It is the pre-emptive
      sibling of `Breakpoint::actualLine`, which stays the fallback for adapters that
      don't implement `breakpointLocations` -- so two mechanisms now correct the same
      thing from different ends. Kept separate deliberately: the snap makes the *store*
      honest (conditions and removals address a real line), `actualLine` only ever
      corrected the display, and collapsing them would mean making every edit operation
      re-address itself asynchronously.
- [ ] The debug console's Tab completion lists several candidates into the transcript and
      inserts their common prefix, readline-style, rather than showing a popup. The panel
      has no `ListPopup` of its own and adding one for the debug console alone would be a
      second completion UI beside `CompletionSession`'s.
- [ ] `BufferView::BeginDebugPanelTextEntry`'s accept callback must not start a second
      prompt: the session teardown that follows every accept would cancel it immediately.
      Fine for every prompt the panel actually needs (all single-shot); a chained one
      would need the callback deferred past the reset.
- [ ] `TreeView`'s selection brush deliberately wins over a row's own colours, so a
      selected row cannot say anything in colour alone -- which is exactly the row being
      acted on. Worked around where it mattered (a disabled breakpoint says `off` in its
      value column, an exception filter's `▣`/`▢` differ in shape), not fixed: a row that
      wants its colours through selection would need the widget to say so per row.
- [ ] `Manager::FrameLocals` is a flat name→value map of the focused frame's
      non-expensive scopes, so inline values can never show a field of a composite
      (`node->key`) or a watch -- only a top-level local. The adapter reports
      `evaluateName` per variable, which is what a deeper version would key on.
- [ ] Six DAP requests were added with the debug panel and all six are capability-gated,
      which is load-bearing rather than polite: an empty answer and "this adapter does
      not implement the request" are indistinguishable on the wire. `breakpointLocations`
      in particular must read an empty list as "no opinion", never as "no valid lines",
      or it would refuse every breakpoint against a thin adapter. Only unit-tested
      against a fake adapter so far -- none of `modules`, `loadedSources`,
      `breakpointLocations`, `stepInTargets`, `setExpression` or `completions` has been
      exercised against a real lldb-dap/debugpy session.
- [ ] **No server/daemon mode** — no `emacsclient`-equivalent; one process per terminal,
      no way to keep a warm process (buffers, LSP connections, undo history) alive and
      attach a new terminal client to it.
- [ ] `TestSourceResolver`'s basename search picks the shallowest candidate when
      nothing disambiguates (no go package hint, no directory components in the
      reported path, several same-named files) — deterministic, but it can be the
      wrong file. A prompt-to-choose would be the honest answer; not worth it until
      the silent wrong pick is actually seen.
- [ ] A snippet body that spells one tabstop index with two different placeholders
      (`${2:A} ${1:foo ${2:bar}}`) renders the nested occurrence with its own baked text
      until the first edit syncs the mirrors — the substitution for an index is resolved
      before emission, so the nested run is already spliced by the time the top-level
      placeholder wins. Ill-defined in the LSP grammar to begin with, and it degrades to
      a cosmetic first-render disagreement, never a wrong edit (`nested-snippet-stops`).
- [ ] A variable reference nested inside another placeholder's own default text still
      doesn't resolve (only a top-level `$`/`${` reference does) — untouched by
      `nested-snippet-stops`, which taught the placeholder parser about nested *tabstops*
      only.
- [ ] Hunk unstage matches point against the *cached* staged diff, which drifts when
      unstaged edits exist earlier in the file — exact in the common stage-then-undo
      flow; revisit only if it bites.
- [ ] **`libned` as a real shared library** — `ned_lib` (static today) exists solely so
      `ned_tests` can link real editor code without pulling in `main()`; a static lib
      already does that job. Worth revisiting only if a second real consumer shows up
      (an embedding use case, a separate CLI tool) — would need symbol-visibility
      curation and SONAME/ABI-versioning discipline that don't pay for themselves yet.
      Note the likeliest second consumer, a headless remote agent, does *not* force this:
      it links the existing static `ned_lib` fine, scripting runtime and all left behind
      (measured — see "Remote Development"'s toolchain-choice item).
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

The per-hunk mouse action row and the whole-file bulk actions both shipped -- slug for
`git log --grep=`: `merge-conflict-bulk-and-chips`. The action row is *not* a row: the
chips ride on the `<<<<<<<` marker line as end-of-line virtual text, the same trick
`PaintEndOfLineDiagnostics` uses and for the same reason (a resolution deletes the line
the chips sit on, and nothing below it should jump on the way). Both cuts it left behind
-- the chips firing on marker text alone, and bulk resolution ignoring the marked set --
are closed below.

The VCS gate on the chips/tinting and bulk resolution over the marked set both shipped --
slug for `git log --grep=`: `merge-conflict-vcs-gate`. Two conscious calls left behind,
each its own item below.

- [ ] With no VCS answer at all — no provider resolves for the project root, the buffer
      has no path, the `git status` is still in flight or failed — the chrome falls back
      to the marker-text reading it always had. Deliberate, and the direction the
      three-valued verdict exists to get right: a conflict that is briefly invisible is a
      worse failure than a doc file that briefly shows chips. The cost is that a custom
      provider whose status output cannot express "unmerged" never suppresses anything.
- [ ] The explicit commands (`C-c x n/p/o/t/b/d/k`) are deliberately **not** gated on the
      verdict — they parse the buffer themselves (`Editor/ConflictResolution.h`), so they
      still resolve a file the VCS has no opinion about. Only the automatic chrome defers.
      Worth revisiting only if "the chips are gone but C-c x o worked" reads as a bug
      rather than as the split it is.
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
      (`Lsp/Client.h`, `Dap/Client.h`, `Acp/Client.h`): background `jthread`
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

Local-only slice shipped (`Editor/Project/Registry.h`, `switch-project`/`open-project`
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

### Remote Execution & Server Protocol

- [ ] **Design ned's own client/server protocol** (raised 2026-09-08 — unstarted, no
  design committed yet; this entry records the shape of the problem and what's already
  known, not a spec). The motivating idea: rather than a remote ned shipping buffers
  back and forth, send *the operation* to where the files are and return only the
  result — a project-wide search, a refactor, a script evaluation. Round-trip count,
  not bandwidth, is what makes remote editing feel bad, so this is likely faster as
  well as simpler.

  **The load-bearing design decision — local is the degenerate case.** The protocol
  should be the *only* interface, with in-process execution as one transport behind
  it rather than a bypass around it. Two things follow. It can't rot: every local
  keystroke exercises the same path a remote session uses, so remote stops being a
  bolt-on that's broken every time it's picked back up. And it makes "where does
  this script run" a transport question rather than an architectural one — the same
  request answered in-process, by a local subprocess, or by a host across a socket.

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
    filled (`Lsp/BrokerConnect.cpp` now does the non-blocking-connect + `poll` dance);
  - joining a reader thread under a held mutex wedged the daemon for hours;
  - `poll()` on a `-1` fd with a `-1` timeout parks forever — the root cause of the
    `ctest -j8` timeouts;
  - an unbounded `WriteAll` hung the UI, fixed by a per-client writer thread in
    `LspClient`/`DapClient`/`AcpClient`.
  A new protocol gets timeouts on every blocking call, a non-blocking connect, an
  asynchronous write queue, and no lock held across a join — by construction, on day
  one. `EventLoop::Post` is the existing, proven way results come back to the main
  thread; the protocol layer should not invent a second one.

  **Open questions worth settling before any code:** framing (length-prefixed binary
  vs. reusing the `Content-Length` shape already implemented three times); whether
  requests are JSON (nlohmann is already vendored) or something denser; how a long-
  running remote operation streams partial results and gets cancelled (LSP's
  `$/progress` and `$/cancelRequest` are the obvious prior art, already handled in
  `LspManager`); versioning and forward compatibility; and authentication/transport
  (bare `AF_UNIX` locally vs. stdio-over-ssh vs. TCP — the broker's socket-path and
  trust conventions in `BrokerSocketPath.cpp` are the local precedent).

  **Security is not a later concern here.** "Execute this script over a socket" is a
  remote code execution surface by definition. Ned already gates project-local
  `.ned/init.janet` behind `ProjectTrust`'s content-hash registry precisely because
  opening a directory shouldn't run arbitrary code; the same discipline has to extend
  across a transport, where the threat model is strictly worse. Decide the trust model
  alongside the framing, not after it.

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
- [ ] Transport/protocol for talking to the agent — now covered in full by "Remote
      Execution & Server Protocol" directly above, including the local-is-the-degenerate-
      case framing, compression negotiation, and the four protocol bugs a new client must
      inherit rather than re-earn. Note the earlier framing here ("a fourth following the
      same precedent is low-risk") is exactly what that section argues against: fold
      `LspClient`/`DapClient`/`AcpClient`'s triplicated machinery into one connection class
      *first*, so the agent transport is its first consumer rather than a fourth copy.
- [ ] Auto-deployment: detect the agent is missing or stale on the remote host and
      self-install it (`scp`/`sftp` push + `chmod +x`), the VS Code Remote-SSH/JetBrains
      Gateway precedent, with a version handshake so a stale cached copy gets replaced
      rather than silently mismatching.
- [ ] **Language/toolchain choice for the agent is genuinely open.** Reusing `ned_lib`'s
      own C++ `ProjectSearch`/`GitIgnoreMatcher`/RE2/`ChildProcess` code as a headless,
      UI-free build gives identical search/gitignore semantics locally and remotely for
      free and avoids a second implementation to keep in sync. **The "but `ned_lib` doesn't
      separate cleanly from Notcurses/Janet" objection recorded here earlier turns out to
      be false — measured 2026-09-08.** A headless program linking `libned_lib.a` with
      `-ljanet` removed from the link line entirely compiles, links and runs: 8.7 MB,
      opening a real `Buffer` and returning real `ProjectSearch` hits. Static-archive
      member granularity does the work — objects are only pulled in when referenced.
      Only 12 of 316 objects in `ned_lib` reference anything named "janet", three of those
      want the tree-sitter *grammar* rather than the runtime (`Languages`/`Mode`/
      `ModeOverrides`), `WindowManager` only forwards a `const*` through setters, and the
      entire remaining UI-side coupling is one call: `Environment::BindingNamesWithPrefix`,
      for binding-aware completion. No "split `ned_lib` first" prerequisite exists; a
      separate `add_executable` target is enough (note the existing headless modes —
      `--lsp-broker`, `--mcp-stdio-relay` — are *modes of `ned`*, so they do link
      `-ljanet` today despite never using it; a separate target is what lets the linker
      drop it). Rust or Go therefore look much weaker than they did: they would buy easier
      cross-compilation at the cost of reimplementing and hand-syncing gitignore/search
      semantics a second time, against a C++ path whose cost is now known to be near zero.
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
- [ ] Host-key verification / first-connection trust prompt — `Project/Trust.h`'s
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

### Issue Tracker Integration (Jira First) — and the Widget Layer It Forces

Wanted for real, daily use: a company Jira board reachable from inside ned, not a browser
tab beside it. The end state everyone pictures is "write a widget in Janet and dock it",
and that end state is genuinely several subsystems away — ned has no HTTP client, no
secret handling, no record-list widget, and **no UI surface in Janet at all** (220
`Register<>` bindings, of which exactly six touch the UI layer, and all six are settings:
four theme colors and two ACP dock placements — nothing constructs a widget, a row, or a
key). So this is staged deliberately: build one bespoke panel that is actually useful,
let a second consumer show up, extract only what they share, and reach the scripting
surface last. Every generic layer in this codebase arrived that way and none of them
arrived first — `PanelDock` after three panels had each reinvented the tab strip,
`FramedConnection` after three protocol clients had each rebuilt the same read loop,
`CacheStamp` after the same validity check had been written out fourteen times.

**Stage 0 — transport.** Three shapes, genuinely decidable now rather than later:
- [ ] **(a) `curl` argv from Janet, JSON parsed in Janet.** Exactly the VCS provider
      template (`Source/Janet/JanetVcsProvider.h`, `Source/Janet/Plugins/vcs-git.janet`):
      Janet builds an argv and parses stdout, C++ owns spawning (`Tasks/TaskProcess.h`)
      and the main-thread callback discipline. Zero new C++ dependency, and the provider
      registry already proves a Janet table of callbacks can back a C++ interface.
      Leaning this first.
- [ ] **(b) A real HTTP client in C++** (libcurl, or cpr over it) — a Gentoo package per
      the `system-libs-over-fetchcontent` call, buying proper async, connection reuse and
      streaming. Worth it only once (a) demonstrably hurts.
- [ ] **(c) MCP client.** Tempting, because Atlassian ships an MCP server and ned already
      speaks MCP — but it speaks it in the wrong direction. `Editor/Mcp/BridgeServer.h` is
      a *server*, serving ned's own tools to the ACP agent; being a client is a new
      subsystem, and it hands the user's Jira credentials to somebody else's process.
- [ ] JSON is already solved either way — `nlohmann::json` is a system dependency and is
      what LSP/DAP/MCP all use.

**Stage 0b — credentials, which ned has no answer to at all.** Nothing in this codebase
reads or writes a secret today, so this is a genuine design decision and not a detail of
stage 1.
- [ ] **A token must never reach an argv** — `/proc/<pid>/cmdline` is world-readable.
      `curl -K -` from a mkstemp'd config file is the shape that works (`FormatOnSave`'s
      own temp-file precedent, the same one `RequestHunkApply` uses for patches).
- [ ] Strong lean: **store nothing.** Take a *command* that prints the token
      (`ned/set-jira-token-command`, the shape `ned/set-format-command` and
      `ned/set-url-open-command` already establish), so `pass`, `gh auth token`,
      `secret-tool lookup` or a bare env var all work and ned owns no secret. A 0600 file
      under `$XDG_STATE_HOME/ned/` is the fallback if that proves too fiddly — but a
      plaintext token in editor state is a real thing to be reluctant about, and
      `--transient` would have to exclude it.

**Stage 1 — one bespoke panel, hardcoded, actually useful.** Built the way `VcsPanel`
was: a concrete C++ `Widget` with its own rows, keys and context menu, hosted in the
existing `LeftDock`/`PanelDock` (both already take a new panel without new host plumbing).
Read-only to start — my issues, a board's columns, one issue's detail.
- [ ] Scope by what a board actually needs, not by REST endpoint coverage. Jira's API is
      enormous and almost all of it is irrelevant to "what am I working on".
- [ ] A `jira::Provider`/`jira::Runner` split mirroring `Editor/Vcs/` — this is the third
      time that exact async-external-tool shape will have been written, which is the
      evidence Stage 3 needs rather than something to pre-empt here.

**Stage 2 — actions, and the parts that belong in a buffer rather than a panel.**
- [ ] Transition, assign, comment. A comment is *text*, so it gets a real buffer with a
      real mode, the way `COMMIT_EDITMSG` already does (`Vcs/Runner.h`'s
      `kVcsCommitMessageFilename`) — not a one-line prompt.
- [ ] Branch-name-from-issue, and the reverse: an issue key in the mode line when the
      current branch names one. Both ride `Vcs/Runner` and the existing mode-line
      indicator slots; neither needs anything new.

**Stage 3 — extract, once there are two real consumers.** What Stages 1-2 will have made
concrete, and deliberately not before:
- [ ] A generic **record list/table** widget. `TreeView` already covers hierarchies
      (`lsp-call-type-hierarchy`'s own "real widget" call) and `ListPopup` covers
      transient pick-lists; a persistent, sortable, column-aligned list of external
      records is the thing neither is. The `ListPopupRow` span-awareness cut
      (Editor Ergonomics, above) is the same gap seen from the other side.
- [ ] A generic **async external record source** — the `Provider` + `Runner` +
      registry triple, extracted only if the Jira copy and the VCS copy genuinely agree.
      The `FramedConnection` finding elsewhere in this file is the cautionary version:
      three copies of a shape can still be three different shapes.

**Stage 4 — the Janet widget surface. Last, and declarative, not immediate-mode.**
- [ ] `Widget`'s contract is `Paint(Canvas)` + `OnEvent(Event)` — a per-frame, per-cell
      API. Handing Janet a `Canvas` would put the scripting boundary at per-cell
      granularity, which is the wrong place for it for the same reason dynamic dispatch
      per element is (`CLAUDE.md`'s dispatch note). The shape that works is the one this
      codebase already uses everywhere it crosses that boundary: **Janet returns a model,
      C++ paints it** — `ListPopupModel`, `VcsPanelContextMenuTarget` and
      `JanetVcsProvider`'s callback table are all already that split.
- [ ] Which means the real deliverable here is a *model vocabulary* (rows, columns,
      actions, key bindings, a refresh callback), not a rendering API — and that
      vocabulary is only designable once Stage 3 has said what two real panels have in
      common.
- [ ] Jank changes none of this (Maybelist, below): the boundary's shape is the problem,
      not the language on the far side of it.
- [ ] Note the 1.0 freeze commitment applies the moment any of this ships as `ned/*` —
      which is an argument for reaching Stage 4 late and with evidence, not early.

### Documentation & Companion Tooling

- [ ] **Internal/developer docs: Doxygen.** `/** */` doc-tags on the C++ side
      (namespace/class-hierarchy aware, matching this codebase's `ned::text`/`editor`/
      `janet`/`ui` layering), themed with Doxygen Awesome. Wired via CMake's
      `find_package(Doxygen)` + `doxygen_add_docs()` as an opt-in `docs` target, not part
      of `ALL` — a build/release-time step, nothing ned does at runtime. clang-doc was
      considered and rejected as still too early-stage (LLVM's own docs warn of bugs/
      crashes on real codebases).
- [ ] **Environment setup tool** (`ned-setup` or similar) — first-run detection: shell
      integration, installed language servers and debug adapters, *generating an
      editable Janet file* loaded from `init.janet`. Deliberately a standalone,
      inspectable generator — silent runtime auto-detection was considered and rejected.
      (Languages are no longer part of this: every grammar is a package under
      `share/ned/languages`, and a new one comes in through `ned --import-language`.)
- [ ] **Cookbook entries for debugger-adjacent tools that already work with zero new
      code** (audit finding, 2026-09-06 — a documentation gap, not a code gap):
      Valgrind (`valgrind --vgdb=yes --vgdb-error=0` + DAP `Attach`, memcheck errors
      arrive as ordinary `stopped` events) and Docker/embedded/OpenOCD targets
      (`Attach`'s adapter/config is opaque argv + JSON, so `cortex-debug`-style or
      Docker-aware adapters already work via `ned/set-dap-adapter`/`ned/set-dap-attach`)
      both need a worked example in the docs, not new `DapManager` code.

### Notcurses Patches Worth Upstreaming (Watch List)

Not one of these, recorded here because it looked like one and wasn't: every
`S-F<n>` binding in the default keymap (`dap-stop`, `dap-step-out`, and
`toggle-debug-panel` as of `debug-panel`) was silently dead, and Notcurses was
reporting faithfully. A terminal without the kitty keyboard protocol cannot say
"Shift+F9" -- it sends a *different function key* and no modifier bit, which is
what terminfo's `kf13`..`kf24` have always meant. `KeyTranslation.cpp`'s own
`SpecialKeyFor` stopped at F12 and returned `nullopt`, dropping the keystroke
before any keymap saw it. Measured with a throwaway probe against a real
terminal (Shift+F9 -> F21, Ctrl+F9 -> F33, Ctrl+Shift+F9 -> F45, Alt+F9 -> F57,
every one with `modifiers == 0`) and folded back onto F1-F12 plus the modifiers
each range implies -- see `DecodeExtendedFunctionKey`. The one cost is that a
physical F13..F24 key can no longer be bound separately from Shift+F1..F12,
which is the conflation terminfo itself already makes.

Not submitted anywhere yet — a deliberate choice (2026-09-06), not an oversight. Recorded
so the research doesn't have to be redone before actually opening anything.

The mechanical barrier is gone now: `Patches/notcurses/` carries all three as real
`git am`-able files with proper commit messages, generated from the CMake scripts by
`Patches/notcurses/regenerate.sh` and verified to apply against pristine v3.0.17. Opening
a PR is a `git am` and a push rather than a re-derivation.

- [ ] **`CMake/PatchNotcursesNulKey.cmake`** (Ctrl+Space/Ctrl+@ swallowed as a NUL byte)
      and **`CMake/PatchNotcursesMouseWheel.cmake`** (SGR wheel-right, Cb=67, misdecoded
      as a motion+release) are both still-reproducible bugs against upstream
      `dankamongmen/notcurses` `master` as of 2026-09-06 (verified live against the
      current `src/lib/in.c`, not just our pinned `v3.0.17`) — checked GitHub issues for
      both ("Ctrl+Space", "NUL", "0x00", "wheel") and found nothing matching, so these
      look like genuinely novel, unreported bugs. Both patches are already small,
      root-caused, and general (not ned-specific workarounds), so they're close to
      PR-ready as-is whenever we decide to open them.
- [ ] **`CMake/PatchNotcursesBracketedPaste.cmake`** — shipped in ned 2026-09-06
      (`git log --grep=paste-perf-and-drag-drop`); it is the *upstreaming* that is still
      open, same as the two above. Upstream issue
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

- **UBSan: `TypstScanner.cpp` loads invalid `enum container` values (2494, 2501, 2557).**
  Three `runtime error: load of value 8, which is not a valid value for type 'enum
  container'` reports every sanitizer run of `Ned parse engine matches the bundled
  corpora`. The scanner keeps its container stack in a `vec_u32` and casts entries back
  to the enum (`scanner_container_at`), so a value the enum has no name for is loaded as
  one; upstream tree-sitter-typst's scanner has the same shape. Tests pass -- the value
  falls through to `default:` in both switches that read it. Verified pre-existing on
  2026-09-22 (reproduced on a clean tree with the EOF-completion work stashed), so it is
  not fallout from that change. Fix is to store and compare the raw `uint32_t`, or widen
  the enum to cover what is pushed.

- **`BufferView's highlight cache updates after an edit changes the buffer's content`
  is intermittently flaky under `--order rand`.** Found 2026-09-15 while stress-testing
  the new Wrap rule kind's own `--order rand` reruns -- confirmed unrelated to that work
  (the failure reproduces on its own, in a test file/subsystem the Wrap rollout never
  touches: `Editor/Mode.cpp`'s `formatCaptures` closure and the Wrap/Rules/Config files
  are all it changed, none of which this test exercises). Roughly 1 in 10-15
  `--order rand` runs. The surprising part: it's the test's OWN FIRST assertion that
  fails (`Tests/BufferViewTest.cpp:1774`, checking that a freshly-painted `"a"` string
  literal renders with `SyntaxClass::String` on the very first `Paint()` call), not the
  post-edit one the test's own name is about -- `REQUIRE(CellMatchesBrush(screen.PixelAt
  (gutter + 0, 0), fixture.theme.BrushFor(ned::editor::SyntaxClass::String)))` evaluates
  false, meaning the cell rendered as `Default` instead, as if the JSON mode's highlight
  query hadn't produced a capture yet at the moment of that first paint. Smells like a
  mode-construction/parse-readiness race (this codebase already has precedent for that
  class of bug -- see `ModePrewarmTest.cpp` and the dynamic-mode-race entry closed
  earlier) rather than anything about cache invalidation specifically, but not
  root-caused -- logged rather than guessed at.

- **`search-everywhere debounces a background text search and Enter opens the matching
  file at that line` fails occasionally under `ctest -j8`.** Seen once on 2026-09-20
  during the async-save work; passes on its own and on an immediate full rerun, and the
  branch it appeared on touches neither search-everywhere nor the debounce timer. Same
  shape as the parallel-run flakes closed on 2026-09-08: a timing assertion that loses
  its margin when eight test binaries contend for the machine, rather than a real defect.
  Worth pinning the debounce to a fake clock if it resurfaces; not chased now.

### Named Non-Goals (Leaning "Won't Do", Kept Visible So It's a Conscious Call)

- [ ] A plugin marketplace/package registry (VSCode extensions, MELPA/straight.el).
      Ned's model is one Janet-scriptable environment plus opt-in project-local plugins
      gated by `ProjectTrust`'s hash-based trust registry — a marketplace implies a
      supply-chain-trust problem this project has deliberately stayed out of.
- ~~A single fuzzy command palette~~ — **stale, corrected 2026-09-18.** This was
      already false when written: `search-everywhere` ships a merged Command/Macro/File/
      Buffer/Symbol/TextMatch palette on a double-tap-Shift gesture. The distinction the
      entry was reaching for is real and still holds, so keep it: M-x, `project-find-file`
      and `switch-to-buffer` remain separate purpose-built commands with their own
      bindings, and the palette is an *additional* front door rather than their
      replacement. Remaining work is under Editor Ergonomics ("Search-everywhere
      remainder").

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
- **Raw terminal I/O** — the `termios`/`poll` raw-mode work now lives in
  `Editor/Clipboard.h`'s OSC 52 path and `UI/EventLoop.cpp` (the earlier
  `UI/TerminalColorProbe.*` this used to name was removed with `--detect-theme`). Needs
  the Win32 console API, or VT passthrough on a recent-enough Windows Terminal.
- **Notcurses itself** would need to build and run against the Win32 console/Windows
  Terminal target — worth checking Notcurses' own upstream platform support before
  committing, since ned's `UI/` layer sits directly on it with no abstraction gap.
- A PowerShell-flavored bundled theme would be a small addition once the port exists —
  `UI/ThemeRegistry.h`'s fixed name→factory table is exactly the extension point.
- **LSP broker self-staleness detection** (`Editor/Lsp/BrokerMain.cpp`'s executable-
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

- [ ] **Jank replaces Janet** — swapping the scripting layer for
      [jank](https://github.com/jank-lang/jank), a Clojure dialect on LLVM. Full
      measured feasibility record: `Docs/JankFeasibility.md` (investigated 2026-09-08
      against a real local install, not from documentation). Verdict: embedding works
      today and per-form cost after warm-up is negligible, but three things block it.
      **Peak RSS ~237 MB** against ned's current ~28 MB with a file open — a 10x floor
      before a single buffer loads, which sits badly beside the huge-file work done
      specifically to keep memory bounded. **Eval forces the dynamic runtime**, so ned
      would ship a hard dependency on a matching Clang/LLVM (`libclang-cpp.so` 84 MB +
      `libLLVM.so` 182 MB) where Janet is a ~1 MB library with no toolchain dependency.
      And **BDWGC is statically baked into both jank archives**, so every `std::thread`
      in ned needs explicit `GC_register_my_thread` bracketing — about eight lines of
      RAII, but at 39 call sites. Upstream was reportedly easing embedded builds around
      the time this was measured; re-check before investing in any workaround.
- [ ] **Widening the project-wide problem list.** The list (slug for
      `git log --grep=`: `show project-wide diagnostics in the problem list`) covers
      whatever servers volunteer about files with no buffer open, which for a
      whole-project checker is most of what they say. Three ways it could go further,
      none of them started, each with the thing that would justify it:
      **Coverage** -- a server that only reports on what it has been asked about still
      yields a list scoped to open buffers. A project-tree crawl, or opening files
      headlessly to provoke a check, would widen it; the store is already the seam
      (keyed by path and connection, not by how a diagnostic arrived, so another tier
      writes into the same map and merges for free). Justified when a server someone
      actually uses turns out to be that quiet.
      **Cold start** -- the store is empty after a restart until each server re-checks:
      automatic for rust-analyzer, blank until a save for a check-on-save server.
      Persisting it would mean trusting positions against files that may have changed
      while ned was not running, so it wants the staleness answer below first.
      **Staleness** -- a file edited on disk under a recorded diagnostic is flagged
      (`(file changed since)`, off a size/mtime stamp) rather than re-resolved.
      Re-resolving against the file as it stands is a read-side change with no
      migration -- the store keeps `{line, character}` rather than byte offsets
      precisely so that stays possible. Justified when the flag starts showing up often
      enough to be noise rather than information.
- [ ] **ned in Carbon, eventually.** Speculative and deliberately unscoped, but worth
      remembering: ned is C++23 throughout, and Carbon's entire pitch is C++ interop as a
      migration path for large existing C++ codebases — which describes this one. Carbon is
      pre-0.1 (its own roadmap says end of 2026 is the *soonest* 0.1 could ship) and no
      admissible tree-sitter grammar exists — six `tree-sitter-carbon` repos, all zero
      stars, one archived — so the correct action is to watch, not wire. Full entry with
      the admission trigger in `Docs/LanguageCoverage.md` ("Ahead of the catalogue").
      Relevant to the parsing-engine work rather than separate from it: Carbon names *"low
      context-sensitivity"* as an explicit design principle, which is exactly the property
      Tier 0 structural inference relies on, so it is an easy case rather than a hard one.

- [ ] **Configurable indicator glyphs** — the five render-indicator characters
      (`Source/UI/BufferView/Internal.h`) are `constexpr char32_t`: `…` fold ellipsis,
      `»` truncation, `│` indent guide, `↳` wrap continuation, `¬` no-trailing-newline.
      Their *colours* already follow the theme — the wrap glyph paints in
      `indentGuideForeground`, which `ThemeFromPalette` defines as
      `Interpolate(0.5, subtleForeground, background)`, so it recedes correctly on light
      and dark alike (verified live across `light`/`high-contrast-dark`/`mono-light`) —
      but the characters themselves are fixed. Raised when the wrap indicator moved into
      the gutter (2026-09-11); deliberately not done for that one glyph alone, since a
      bespoke `ned/set-wrap-glyph` beside four hardcoded siblings is exactly the
      one-off this codebase avoids. The shape if it happens: one small module in
      `TabWidth.h`'s mutex-guarded-static pattern covering all five, a
      `ned/set-glyph "wrap-continuation" "⤷"`-style binding, and validation that the
      replacement is a single-width codepoint — a double- or zero-width glyph in the
      digits column shifts the whole gutter. A maybe rather than an open item because
      nobody has yet wanted a different character, only a different weight, and the
      theme already provides that.

- [ ] **Split BufferView's prompt/session machine out, with its LSP pickers** — the
      BufferView decomposition (`Docs/BufferViewDecomposition.md`) shrank `BufferView.cpp`
      from 16,823 lines to ~1,700 and `Paint` from 1,597 to 459, but `BufferView.h` is
      still ~4,000 lines and 198 members. Its plan's phase 5 said to fix that by
      extracting `LspFeatures`. That boundary was measured and rejected: 9 of the LSP
      functions *set* `inputMode_` and 17 read it, because most LSP features **are**
      prompts (code-action select, go-to-definition select, peek, document/workspace
      symbols, rename). Extracting them by protocol would bounce callbacks across a
      boundary for what is one interaction, and the sub-clusters that genuinely are
      ambient (hover, document highlight, signature help — 61 of 83 functions never touch
      session state) are ~4 members each and do not justify a class plus a callback
      struct.
      The boundary that would work is by *role* rather than by protocol: lift the
      prompt/session state machine (`inputMode_`, `prompt_`, the four shared drivers, the
      per-session state) into its own type and take the LSP pickers with it, leaving
      BufferView with the widget and its key/mouse entry points. That is a bigger piece
      of design than the plan describes and is the reason this is a maybe rather than an
      open item — it is worth doing only if the header actually starts costing time, not
      merely because it is long.

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
- LSP broker "server mode" shipped 2026-09-16 as `ned --foreground` (git log --grep=
  `foreground-mode`) — see the pre-warming follow-up below for the one piece split out
  of it.
- `ned --foreground` takes over an ordinary broker and refuses to displace another
  `--foreground` instance (2026-09-21, slug `foreground-one-instance`): it probes the
  socket with a new `ned/broker-info` control request, shuts down an ephemeral daemon and
  waits for it to actually stop before binding, and tells the user to restart their
  service rather than starting a second always-on daemon. A daemon too old to answer
  `ned/broker-info` reads as "unidentified" and is taken over like an ephemeral one.

- [ ] **LSP broker pre-warming** (split out from "LSP broker server mode" above,
      2026-09-16) — warm the N most-recently-used projects' language servers when
      `ned --foreground` starts, using `Editor/Project/Registry.h`'s existing
      `lastUsed`-ordered `ListProjects()` as the recency source (no new tracking
      needed for that half). Blocked on argv, not on root detection: root-marker
      detection (`Editor/Lsp/RootResolver.h`, finding `compile_commands.json`/
      `Cargo.toml`/etc.) is pure filesystem logic and could run headlessly today, but
      the per-language server *command* (`Editor/Lsp/ServerConfig.h`) is populated only
      by a live interactive `ned` process's `ned/set-lsp-command` Janet calls, and that
      file states as deliberate policy that nothing is bundled/auto-detected for any
      language (same convention as `TaskConfig.h`/`Dap/Config.h`) — a prewarm-only bundled
      table would be a real, called-out exception to that policy, not a small addition.
      Needs a `(root, language) -> argv` persistence mechanism (each real interactive
      attach writing its resolved config to a small state file the daemon can read at
      its own startup) before this can be built without either violating that policy or
      guessing.

Also shipped, one slug each for `git log --grep=`: `broker-reader-deadlock` (that same
daemon deadlocking in its own idle sweep — the bug is why `Tests/LspBrokerDaemonTest.cpp`
and the daemon's ASan coverage exist at all, both of which any server-mode work should
build on), `code-coverage-gutter`.

- [ ] Raw per-file `.gcov` output has no parser — `Editor/Coverage/OutputParser.h`
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
