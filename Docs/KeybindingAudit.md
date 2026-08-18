# Keybinding Audit

Ground-truthed against the actual keymap wiring as of `56adbd3` (`git log -1`), not
guessed: `Source/Editor/Commands.cpp` (`BuildDefaultGlobalKeymap()` + every
`registry.Register(...)`), `Source/Editor/Mode.cpp` (`OrgMode()` is the only mode with a
non-empty `Keymap`; every other `*Mode()` factory builds an empty one), `Source/Editor/Key.cpp`
(`NamedKeys()` — the exact set of special keys `kbd`-style sequences can name today), and
every `ParseKeySequence(` call site across `Source/` (only `Commands.cpp`, `Mode.cpp`, and
`Janet/EditorBindings.cpp`, the last being the user-facing `ned/define-key` path, not a
built-in binding). Three reference tables below (canonical Emacs, "normal" editor
conventions, advanced/IDE features), each cross-checked against what Ned already binds,
followed by a prioritization into four buckets. Rows in the reference tables are backed by
what was actually read in code, not assumption — where something is more subtle than a
flat yes/no (bound to a different key, registered but unbound, expressible but unused),
the Notes column says so explicitly.

**Named keys today** (`Key.cpp::NamedKeys()`): `RET TAB DEL DELETE ESC UP DOWN LEFT RIGHT
HOME END PRIOR/PAGEUP NEXT/PAGEDOWN F1–F12`. No `INSERT`. Any modifier (`C-`/`M-`/`S-`)
combines with any of these or a literal codepoint, so e.g. `C-RIGHT` is already
expressible even though nothing binds it by default (confirmed: `KeyTranslation.cpp`
already reports `Control+Special::Left/Right` for Ctrl+Arrow).

---

## 1. Canonical Emacs keybindings

| Keybinding | Command/Action | Ned status | Notes |
|---|---|---|---|
| `C-f` / `C-b` | forward/backward-char | **Bound** | `Commands.cpp` |
| `C-n` / `C-p` | next/previous-line | **Bound** | |
| `C-a` / `C-e` | beginning/end-of-line | **Bound** | also `HOME`/`END` |
| `M-f` / `M-b` | forward/backward-word | **Bound** | Real `M-f`/`M-b` chords now, plus `ESC f`/`ESC b` |
| `C-v` / `M-v` | scroll page down/up | **Bound** | Also still bound to `PAGEDOWN`/`PAGEUP`; `ESC v` too |
| `M-<` / `M->` | beginning/end-of-buffer | **Bound** | `ESC <`/`ESC >` also bound |
| `C-l` | recenter | **Missing** | No command |
| `C-d` | delete-char | **Bound** | |
| `DEL` | backward-delete-char | **Bound** | |
| `C-k` | kill-line | **Bound** | |
| `C-y` | yank | **Bound** | |
| `M-y` | yank-pop | **Missing** | No command |
| `M-w` | kill-ring-save (copy region) | **Bound** | `M-w`/`ESC w`; no-op without a mark |
| `C-w` | kill-region | **Bound** | No-op without a mark |
| `C-SPC` | set-mark-command | **Bound** | Mark now persists across keyboard motion (see `ClearMark` note in `Commands.cpp`) |
| `C-x C-x` | exchange-point-and-mark | **Bound** | No-op without a mark |
| `C-x h` | mark-whole-buffer | **Missing** | No command |
| `C-_` / `C-x u` | undo | **Bound (`C-_`)** | Was bound to `C-/`, which turned out to be unreachable from a real terminal — no terminal byte distinguishes Ctrl+/ from Ctrl+_, and the codebase only decoded the latter; confirmed against a live terminal, then fixed by decoding byte 0x1F as Control+'_' (`KeyTranslation.cpp`) and rebinding to `C-_`, real Emacs' own actual undo chord. `redo` is bound to `M-/`/`ESC /`. |
| `C-s` / `C-r` | isearch forward/backward | **Bound** | |
| `M-%` | query-replace | **Bound** | Real `M-%` chord now, plus `ESC %` |
| `C-x C-f` | find-file | **Bound** | |
| `C-x C-s` | save-buffer | **Bound** | |
| `C-x b` | switch-to-buffer | **Bound** | |
| `C-x k` | kill-buffer | **Bound** | Routes to the same `BufferView::RequestCloseBuffer`/`CloseBufferNow` flow the tab-bar close icon already used |
| `C-x C-c` | save-buffers-kill-terminal (quit) | **Bound** | |
| `C-x 2` / `C-x 3` | split-window-below/right | **Bound** | Phase 8 |
| `C-x 0` / `C-x 1` | delete-window / delete-other-windows | **Bound** | |
| `C-x o` | other-window | **Bound** | |
| `C-x (` / `C-x )` / `C-x e` | kbd-macro start/end/call | **Not reachable via those chords, alternates bound instead** | `Ctrl+(`/`Ctrl+)` genuinely can't be produced by real terminal input (documented in code); Ned binds `F3`/`F4` instead (modern Emacs' own real alternate bindings) |
| `C-x r SPC/j/s/i/k/d/y/t` | registers + rectangles | **Bound** | full set: point-to-register, jump-to-register, copy-to-register, insert-register, kill/delete/yank-rectangle, string-rectangle |
| `C-x n n` / `C-x n w` | narrow-to-region / widen | **Bound** | |
| `M-x` | execute-extended-command | **Bound, both forms** | `M-x` and `ESC x` |
| `C-g` | keyboard-quit | **Partial** | Handled ad hoc inside `BufferView` (`IsCancelKey` helper) to cancel an in-progress interactive session (isearch, prompts, etc.) — not a registered command, doesn't do anything in Normal mode (e.g. no "cancel prefix key" or "deselect region" behavior) |
| `TAB` | indent-for-tab-command | **Bound** | Inserts a literal tab; no per-mode indent logic yet |

## 2. "Normal" (non-Emacs) editor conventions

| Keybinding | Command/Action | Ned status | Notes |
|---|---|---|---|
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | copy/cut/paste | **Missing** | Collides with Emacs `C-c` (prefix key, used extensively: `C-c C-s`/`C-c C-p`/etc.) and `C-v` (page down) — binding these as copy/cut/paste would break the prefix-key scheme entirely |
| `Ctrl+Z` / `Ctrl+Y` (or `Ctrl+Shift+Z`) | undo/redo | **Missing** | Ned's `C-y` is already `yank`, a hard Emacs/non-Emacs collision |
| `Ctrl+A` | select-all | **Missing**, and collides | Ned's `C-a` is `beginning-of-line` |
| `Ctrl+F` | find | **Missing**, and collides | Ned's `C-f` is `forward-char`; `C-s` fills this role Emacs-style instead |
| `Ctrl+S` | save | **Missing**, and collides | Ned's `C-s` is `isearch-forward`; this is the single most-cited Emacs-vs-everyone-else collision |
| `Home` / `End` / `PageUp` / `PageDown` | line/page motion | **Bound** | Already wired to the Emacs-equivalent commands |
| `Ctrl+Left` / `Ctrl+Right` | word motion | **Bound** | `forward-word`/`backward-word` |
| `Shift+arrows` | extend selection | **Bound** | `shift-select-forward-char`/`backward-char`/`next-line`/`previous-line` |
| `Ctrl+Home` / `Ctrl+End` | buffer start/end | **Missing** | Same underlying gap as `M-<`/`M->` above — no beginning/end-of-buffer command exists at all, Emacs-style or otherwise |

## 3. Advanced/modern editor features

| Feature | Typical binding | Ned status | Cross-check vs. ROADMAP Phase 9 |
|---|---|---|---|
| Multi-cursor / select-next-occurrence | `Ctrl+D`, Alt+Click | **Missing** | On the wishlist: "Multiple cursors / multi-cursor editing" under Editor ergonomics |
| Go-to-definition / hover / rename symbol | varies, usually `F12`/LSP-bound | **Missing** | On the wishlist as part of "LSP client: autocomplete, diagnostics, go-to-definition, hover docs, code actions, rename, multi-language support" |
| Fuzzy file finder / command palette | `Ctrl+P` / `Ctrl+Shift+P` | **Missing** | On the wishlist: "Fast fuzzy file finder / command palette" — note Ned already has `find-file`'s path-completion (`Tab`) and `M-x`'s fuzzy-narrowed `execute-extended-command`, so the foundation is closer than "missing" alone suggests |
| Format document (on demand, not just on save) | `Ctrl+Shift+I` / `Alt+Shift+F` | **Bound** | `format-buffer`, M-x-only (no default keybinding) |
| Toggle line comment | `Ctrl+/` | **Bound** | `toggle-line-comment`, bound to `M-;` (real Emacs' own chord) instead of `Ctrl+/`, which would be equally unreachable from a real terminal as `C-/`'s old undo binding was |
| Duplicate line | `Ctrl+D` / `Ctrl+Shift+D` | **Bound** | `duplicate-line`, bound to `C-c d` instead (both usual chords already taken/unreachable) |
| Move line up/down | `Alt+Up`/`Alt+Down` | **Bound** | `move-line-up`/`move-line-down`; `ESC UP`/`ESC DOWN` too |
| Expand selection (AST-aware) | `Alt+Shift+Right`/`Ctrl+W` in some IDEs | **Missing** | Explicitly on the wishlist: "Structural/AST-aware selection expansion (expand-to-next-syntax-node)" |
| Quick-fix / code actions | `Ctrl+.` | **Missing** | Covered under the LSP wishlist item's "code actions" |
| Git gutter actions (stage hunk, blame, diff) | varies | **Missing** | On the wishlist: "Git integration: inline blame, diff gutters, hunk staging, a git status panel" |
| Integrated terminal toggle | `` Ctrl+` `` | **Missing** | On the wishlist: "Built-in terminal panel" |
| Minimap | n/a (visual) | **Missing** | Explicitly on the wishlist, tagged as overlapping Phase 6 theming |

---

## Prioritization

### Need
Core Emacs-parity gaps that undercut the "Emacs-class parity" claim itself — these are
table-stakes Emacs commands, several with existing infrastructure sitting right next to
the gap.

- ~~**`C-SPC` / set-mark-command**~~ — **done.** `set-mark-command` registered, bound to
  `C-SPC`. Landing this required also removing the `ClearMark()` call every plain motion
  command (`forward-char`, `next-line`, etc.) used to make before moving point — otherwise
  the very next arrow-key press after `C-SPC` would immediately wipe the mark, making a
  keyboard-driven region impossible to grow past zero width. Mark now persists across
  keyboard motion until an explicit mouse click or `kill-region`/`kill-ring-save`
  clears it — this also changed how a mouse-drag selection behaves afterward: a
  navigation key now *extends* it (moves point, keeps mark) instead of collapsing it,
  since both paths share the same mark now (see `Tests/BufferViewTest.cpp`'s updated
  case).
- ~~**`C-w` (kill-region) / `M-w` (kill-ring-save)**~~ — **done.** Both registered,
  `C-w`/`M-w`+`ESC w` bound, act on `Buffer::Region()` when a mark is set (silent no-op
  otherwise, matching `kill-line`'s own "nothing to do" convention) and clear the mark
  afterward.
- ~~**Redo keybinding**~~ — **done.** Bound to `M-/`+`ESC /`. While picking this, reading
  `KeyTranslation.cpp`'s `DecodeBaseKey` turned up a real, pre-existing, unrelated gap in
  the *existing* `C-/` undo binding: `DecodeBaseKey` only ever produced `Control=true` for
  C0 control bytes 1-26 (`Control+<a-z>`), so a real terminal's Ctrl+/ byte (0x1F, outside
  that range) could never match a Control-parsed `/` binding. Left as a flagged, unfixed
  comment initially — then independently confirmed against a live terminal (multiple real
  terminal emulators, not simulated) that `C-/` genuinely never fired, and fixed
  separately: `DecodeBaseKey` now decodes byte 0x1F as `Control+'_'` (the same byte a
  terminal sends for both Ctrl+/ and Ctrl+_, since Shift isn't tracked on top of a control
  byte), and undo is rebound to `C-_` — real Emacs' own actual undo chord, not a
  GUI-Emacs-only convenience alias. See the canonical-Emacs table's `undo` row above.
- ~~**`M-<` / `M->` (beginning/end-of-buffer)**~~ — **done.** `beginning-of-buffer`/
  `end-of-buffer` registered, bound to `M-<`/`M->` + `ESC <`/`ESC >`. `Ctrl+Home`/
  `Ctrl+End` are still unbound (tracked separately below, "Normal" editor conventions
  table).
- ~~**`C-x C-x` (exchange-point-and-mark)**~~ — **done.** Swaps point and mark via
  `Buffer::SetPoint`/`SetMark`; a no-op without a mark, same convention `kill-region`/
  `kill-ring-save` already established.
- ~~**`TAB` doing nothing in Normal mode**~~ — **done.** `indent-for-tab-command`
  registered (inserts a literal `\t`, not real indent logic — this codebase has no
  per-mode indent rules yet, a deliberate v1 scope cut) and bound globally to `TAB`; a
  mode's own keymap (`org-cycle`, `markdown-table-align`) still wins via
  `KeymapStack`'s priority order, so this only fires where nothing more specific already
  claimed `TAB`.

### Want
Real value, natural next step, several already named on Ned's own Phase 9 wishlist.

- ~~**`C-v`/`M-v` aliases for scroll-page-down/up**~~ — **done.** Both bound (`M-v` also
  gets the usual `ESC v` dual binding), reusing the existing `scroll-page-down`/
  `scroll-page-up` commands unchanged.
- ~~**`Ctrl+Left`/`Ctrl+Right` bound to word motion**~~ — **done.** `ParseKeyChord`
  already resolved `"C-LEFT"`/`"C-RIGHT"` correctly (the `C-` prefix strips off first,
  then `LEFT`/`RIGHT` resolve via `NamedKeys()`) — this was purely a missing
  `keymap.Bind` call, no new decoding.
- ~~**`M-f`/`M-b`/`M-%` as real Meta chords, not just ESC-prefix**~~ — **done.** All
  three now get the same dual-binding treatment `M-x` already had (real Meta chord +
  `ESC`-prefix fallback), closing the stale-comment gap the code itself flagged.
- **Structural/AST-aware selection expansion** — already Phase 9 wishlist, and Ned's
  tree-sitter foundation (real parse trees per buffer) makes this a comparatively natural
  fit rather than a bolt-on.
- **Fuzzy file finder / command palette** — already Phase 9 wishlist; worth noting the
  groundwork (`CompleteCommandNames`, `M-x`'s fuzzy narrowing, `find-file`'s path
  completion) is further along than a from-scratch feature would be.
- ~~**`kill-buffer` as a real command**~~ — **done.** Registered, bound to `C-x k` —
  just signals `InteractiveRequest::KillBuffer`, which `BufferView::StartInteractiveSession`
  routes straight to the existing `RequestCloseBuffer`/`CloseBufferNow`/
  `ConfirmCloseBuffer` flow the tab-bar close icon already used, no new close logic.
- ~~**Format document on demand**~~ — **done.** `format-buffer` registered (M-x-only,
  no default keybinding — none of the usual GUI-editor chords for this are free here),
  reuses `FormatCommand()`/`RunFormatCommand()`/the same whole-buffer-replace
  `save-buffer` already does, just without the `Save()` call. Reports "No format
  command configured" explicitly rather than staying silent the way `save-buffer` does
  — a direct format request with nothing configured should say so, not look like it
  silently did nothing.

### Maybe want
Plausible, but a real tradeoff either way — worth an explicit decision, not a default yes.

- **`Ctrl+S` for save** — the canonical non-Emacs binding, but it's Ned's `isearch-forward`
  today; changing it breaks a core Emacs binding for a non-Emacs one on the exact key an
  Emacs user would least expect it. If ever done, it should be additive/configurable
  (e.g. a Janet-toggleable "non-Emacs bindings" layer) rather than a silent swap.
- **`Ctrl+Z`/`Ctrl+Y` for undo/redo** — `Ctrl+Y` is already `yank` in Ned; same shape of
  collision as `Ctrl+S`, same "additive, not a swap" recommendation if pursued.
- ~~**`Shift+arrows` for selection extension**~~ — **done.** `S-LEFT`/`S-RIGHT`/`S-UP`/
  `S-DOWN` bound to new `shift-select-*` commands that set a mark at point only if one
  isn't already active, then move -- layered on the same persistent-mark model
  `set-mark-command` already established, not a separate selection concept.
  `KeyTranslation.cpp` gained Shift+Arrow decoding to make this possible (FTXUI has no
  pre-built constant for it the way it does `ArrowLeftCtrl`; built directly from the raw
  xterm CSI modifier sequence instead). Documented v1 scope cut: real Emacs'
  `shift-select-mode` additionally deactivates a shift-started selection the instant any
  *non*-shifted command runs (tracked via its own extra bit distinguishing a
  shift-started mark from an explicit `C-SPC` one) -- not implemented here, so a
  shift-extended region persists exactly as long as any other mark would.
- **Multi-cursor editing** — already Phase 9 wishlist and has real value, but is a
  substantial design lift (editing model, not just a keybinding) or gets deferred with a
  simple 20-minute analysis; flagged here rather than under Want because the *keybinding*
  question genuinely depends on unresolved design questions.
- ~~**Move line up/down, duplicate line**~~ — **done.** `move-line-up`/`move-line-down`
  (`M-UP`/`M-DOWN` + `ESC UP`/`ESC DOWN`) swap the current line with its neighbor,
  preserving point's column within the moved line; `duplicate-line` (`C-c d` — no
  standard cross-editor chord was free here: `Ctrl+D` is already `delete-char`,
  `Ctrl+Shift+<letter>` isn't reliably decodable from a real terminal) copies the
  current line below it, moving point into the copy (VSCode/Sublime/JetBrains'
  "duplicate down" convention). All three get the buffer's last (trailing-newline-less)
  line's edge case right via a shared `GetLineSpan` helper, not raw substring
  concatenation across the swap/insert boundary — traced by hand and covered by tests
  for both directions landing on that line.
- ~~**Toggle line comment**~~ — **done.** `toggle-line-comment` bound to `M-;` +
  `ESC ;` — real Emacs' own actual `comment-dwim`/`comment-line` chord, not `Ctrl+/`
  (which would hit the exact same real-terminal-unreachability problem `C-/`'s old
  undo binding had). `Mode` gained a `lineCommentPrefix` field (empty by default,
  matching `FundamentalMode`'s own "no special support" convention), set per language
  by hand rather than pulled from each grammar's own query (`//` for the C-family,
  `#` for Python/Bash/Org, `;` for Janet; deliberately left empty for JSON/HTML/CSS/
  Markdown, none of which have a native single-line-comment token to toggle). Toggles
  every line a region spans if a mark is set, otherwise just the current line;
  skips blank lines; comments if any non-blank line in range is still uncommented,
  only uncomments once every one already is; excludes a line the region's end merely
  touches at column 0 (Emacs' own `comment-region` convention for the same
  off-by-one gotcha).

### Don't want
Actively wrong fit for a terminal, Emacs-parity, Janet-scriptable editor, or better
served by something Ned already has/plans.

- **`Ctrl+C`/`Ctrl+X`/`Ctrl+V` for copy/cut/paste** — would require sacrificing the
  entire `C-c`-prefix keymap namespace (project-search, project-replace,
  toggle-project-sidebar, create/delete/rename-file, find-scratch, org-mode bindings all
  live under `C-c`), a wildly disproportionate cost for a convention Emacs users
  specifically don't expect.
- **`Ctrl+A` for select-all** — collides with `beginning-of-line`, one of the single most
  reflexive Emacs bindings there is; `mark-whole-buffer` (`C-x h`) is the Emacs-native
  equivalent and belongs in the Need bucket instead, not this key.
- **Alt+Click to add a cursor** — downstream of multi-cursor editing (Maybe want) and
  additionally mouse-dependent in a terminal app where a meaningful fraction of usage is
  over SSH/tmux with imperfect mouse passthrough; low priority even if multi-cursor ships.
- **Right-click context menu for project-file-ops** — already explicitly investigated
  and descoped in `ROADMAP.md` ("Sidebar context menu" note): neither FTXUI nor the
  pre-migration TermOx has a floating/overlay widget concept, so this would require new
  rendering infrastructure disproportionate to what `create-directory`/`delete-file`/
  `rename-file` already deliver as global prompted commands.
- **Real-time collaborative editing bindings** — nothing to bind yet; the underlying
  feature is explicitly flagged in ROADMAP as "the biggest lift in this list," premature
  to think about keybindings for.
- **AI-assisted inline completion bindings** — same reasoning as above, and ROADMAP
  itself already frames this as "likely implementable as a Janet-scriptable integration
  rather than something hardcoded in C++" — i.e. this is a job for `ned/register-command`
  + `ned/define-key` from init.janet, not a new built-in global keybinding.
