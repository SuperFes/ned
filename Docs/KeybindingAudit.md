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
| `M-f` / `M-b` | forward/backward-word | **Bound**, ESC-prefix only | Bound as `ESC f`/`ESC b`, not `M-f`/`M-b` — comment in `Commands.cpp` calls this stale (real Meta detection is reliable post-FTXUI per `KeyTranslation.h`) but only `M-x` got the dual binding treatment |
| `C-v` / `M-v` | scroll page down/up | **Not bound to these keys** | Commands exist (`scroll-page-down`/`up`) but bound only to `PAGEDOWN`/`PAGEUP`, not `C-v`/`M-v` |
| `M-<` / `M->` | beginning/end-of-buffer | **Missing entirely** | No command registered at all |
| `C-l` | recenter | **Missing** | No command |
| `C-d` | delete-char | **Bound** | |
| `DEL` | backward-delete-char | **Bound** | |
| `C-k` | kill-line | **Bound** | |
| `C-y` | yank | **Bound** | |
| `M-y` | yank-pop | **Missing** | No command |
| `M-w` | kill-ring-save (copy region) | **Missing** | No command |
| `C-w` | kill-region | **Missing** | No command — `Buffer::DeleteRange` exists and `KillRing` is region-agnostic by design, nothing wires them together on a keyboard mark |
| `C-SPC` | set-mark-command | **Missing** | `Buffer::SetMark` is only ever called from mouse-drag in `BufferView.cpp`; no keyboard way to set the mark at all |
| `C-x C-x` | exchange-point-and-mark | **Missing** | No command |
| `C-x h` | mark-whole-buffer | **Missing** | No command |
| `C-/` / `C-x u` | undo | **Bound (`C-/` only)** | `redo` command is registered but **not bound to any key** — real gap |
| `C-s` / `C-r` | isearch forward/backward | **Bound** | |
| `M-%` | query-replace | **Bound, ESC-prefix only** | `ESC %`, not `M-%`, same stale-comment situation as `M-f`/`M-b` |
| `C-x C-f` | find-file | **Bound** | |
| `C-x C-s` | save-buffer | **Bound** | |
| `C-x b` | switch-to-buffer | **Bound** | |
| `C-x k` | kill-buffer | **Missing as a keybinding** | Closing exists (`TabBar` close-icon → `BufferView::RequestCloseBuffer`) but there is no `kill-buffer` command reachable from the keyboard/M-x |
| `C-x C-c` | save-buffers-kill-terminal (quit) | **Bound** | |
| `C-x 2` / `C-x 3` | split-window-below/right | **Bound** | Phase 8 |
| `C-x 0` / `C-x 1` | delete-window / delete-other-windows | **Bound** | |
| `C-x o` | other-window | **Bound** | |
| `C-x (` / `C-x )` / `C-x e` | kbd-macro start/end/call | **Not reachable via those chords, alternates bound instead** | `Ctrl+(`/`Ctrl+)` genuinely can't be produced by real terminal input (documented in code); Ned binds `F3`/`F4` instead (modern Emacs' own real alternate bindings) |
| `C-x r SPC/j/s/i/k/d/y/t` | registers + rectangles | **Bound** | full set: point-to-register, jump-to-register, copy-to-register, insert-register, kill/delete/yank-rectangle, string-rectangle |
| `C-x n n` / `C-x n w` | narrow-to-region / widen | **Bound** | |
| `M-x` | execute-extended-command | **Bound, both forms** | `M-x` and `ESC x` |
| `C-g` | keyboard-quit | **Partial** | Handled ad hoc inside `BufferView` (`IsCancelKey` helper) to cancel an in-progress interactive session (isearch, prompts, etc.) — not a registered command, doesn't do anything in Normal mode (e.g. no "cancel prefix key" or "deselect region" behavior) |
| `TAB` | indent-for-tab-command | **Missing in Normal mode** | `TAB` is named and used inside prompt/completion (`BufferView::CompletePrompt`), but the global keymap never binds it — in Normal editing mode, pressing Tab does nothing (`Dispatcher` reports `Unbound`) since the self-insert loop only covers 0x20–0x7E |

## 2. "Normal" (non-Emacs) editor conventions

| Keybinding | Command/Action | Ned status | Notes |
|---|---|---|---|
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | copy/cut/paste | **Missing** | Collides with Emacs `C-c` (prefix key, used extensively: `C-c C-s`/`C-c C-p`/etc.) and `C-v` (page down) — binding these as copy/cut/paste would break the prefix-key scheme entirely |
| `Ctrl+Z` / `Ctrl+Y` (or `Ctrl+Shift+Z`) | undo/redo | **Missing** | Ned's `C-y` is already `yank`, a hard Emacs/non-Emacs collision |
| `Ctrl+A` | select-all | **Missing**, and collides | Ned's `C-a` is `beginning-of-line` |
| `Ctrl+F` | find | **Missing**, and collides | Ned's `C-f` is `forward-char`; `C-s` fills this role Emacs-style instead |
| `Ctrl+S` | save | **Missing**, and collides | Ned's `C-s` is `isearch-forward`; this is the single most-cited Emacs-vs-everyone-else collision |
| `Home` / `End` / `PageUp` / `PageDown` | line/page motion | **Bound** | Already wired to the Emacs-equivalent commands |
| `Ctrl+Left` / `Ctrl+Right` | word motion | **Expressible but unbound** | `KeyTranslation.cpp` already decodes `Control+Left/Right`; nothing in `BuildDefaultGlobalKeymap()` binds them to `forward-word`/`backward-word` |
| `Shift+arrows` | extend selection | **Missing entirely** | No shift-modified arrow handling anywhere; selection today is mouse-drag-only (`BufferView::OnMouseEvent`) — there is no keyboard-driven selection extension at all (downstream of `C-SPC`/set-mark also being missing) |
| `Ctrl+Home` / `Ctrl+End` | buffer start/end | **Missing** | Same underlying gap as `M-<`/`M->` above — no beginning/end-of-buffer command exists at all, Emacs-style or otherwise |

## 3. Advanced/modern editor features

| Feature | Typical binding | Ned status | Cross-check vs. ROADMAP Phase 9 |
|---|---|---|---|
| Multi-cursor / select-next-occurrence | `Ctrl+D`, Alt+Click | **Missing** | On the wishlist: "Multiple cursors / multi-cursor editing" under Editor ergonomics |
| Go-to-definition / hover / rename symbol | varies, usually `F12`/LSP-bound | **Missing** | On the wishlist as part of "LSP client: autocomplete, diagnostics, go-to-definition, hover docs, code actions, rename, multi-language support" |
| Fuzzy file finder / command palette | `Ctrl+P` / `Ctrl+Shift+P` | **Missing** | On the wishlist: "Fast fuzzy file finder / command palette" — note Ned already has `find-file`'s path-completion (`Tab`) and `M-x`'s fuzzy-narrowed `execute-extended-command`, so the foundation is closer than "missing" alone suggests |
| Format document (on demand, not just on save) | `Ctrl+Shift+I` / `Alt+Shift+F` | **Partial** | `FormatOnSave.h` + `save-buffer` runs the configured formatter automatically on save; there's no standalone "format buffer now" command independent of saving |
| Toggle line comment | `Ctrl+/` | **Missing**, and would collide | No comment-toggle command exists; `Ctrl+/` is already `undo` in Ned |
| Duplicate line | `Ctrl+D` / `Ctrl+Shift+D` | **Missing** | No command |
| Move line up/down | `Alt+Up`/`Alt+Down` | **Missing** | No command |
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

- **`C-SPC` / set-mark-command** — `Buffer::SetMark`/`HasMark`/`Region` already exist and
  are already used by rectangles/registers/narrowing; the only way to populate the mark
  today is a mouse drag. Without this, Ned has no keyboard-only way to select a region at
  all, which blocks kill-region/copy-region-as-kill and any future shift-selection work.
- **`C-w` (kill-region) / `M-w` (kill-ring-save)** — `KillRing` was explicitly designed
  region-agnostic ("kill/yank commands compose the two via `Buffer::DeleteRange`/
  `InsertAtPoint`" per `KillRing.h`'s own doc comment) specifically so these could be
  added; today only line-based `kill-line` exists. A glaring gap once `C-SPC` lands.
  These three (set-mark, kill-region, kill-ring-save) are really one unit of work.
- **Redo keybinding** — the `redo` command is fully registered and functional but bound
  to nothing; this is a one-line fix, not a design question, and "undo works, redo
  doesn't" is a jarring first-five-minutes bug report waiting to happen.
- **`M-<` / `M->` (beginning/end-of-buffer)** — completely absent, not even under an
  ESC-prefix fallback. Combined with the missing `Ctrl+Home`/`Ctrl+End` equivalent, there
  is currently *no* keyboard way to jump to the start or end of a buffer at all.
- **`C-x C-x` (exchange-point-and-mark)** — small, but a real everyday Emacs reflex,
  and cheap once mark-setting exists.
- **`TAB` doing nothing in Normal mode** — pressing literal Tab while editing is silently
  swallowed (`Dispatcher` reports Unbound); needs at minimum a bound
  `indent-for-tab-command`/self-insert-tab behavior, since right now there is no way to
  type a tab character interactively at all outside a minibuffer prompt.

### Want
Real value, natural next step, several already named on Ned's own Phase 9 wishlist.

- **`C-v`/`M-v` aliases for scroll-page-down/up** — the commands exist and are already
  bound to `PAGEDOWN`/`PAGEUP`; adding the canonical Emacs chords is a one-line addition
  reusing existing commands, not new design.
- **`Ctrl+Left`/`Ctrl+Right` bound to word motion** — already fully decodable per
  `KeyTranslation.cpp`; just unbound. Low-risk, high-value since it's the most common
  "non-Emacs muscle memory that still feels natural in Emacs" motion.
- **`M-f`/`M-b`/`M-%` as real Meta chords, not just ESC-prefix** — the code's own comment
  flags this as stale now that Meta detection is reliable; `M-x` already gets the
  dual-binding treatment, the other Meta bindings should too for consistency.
- **Structural/AST-aware selection expansion** — already Phase 9 wishlist, and Ned's
  tree-sitter foundation (real parse trees per buffer) makes this a comparatively natural
  fit rather than a bolt-on.
- **Fuzzy file finder / command palette** — already Phase 9 wishlist; worth noting the
  groundwork (`CompleteCommandNames`, `M-x`'s fuzzy narrowing, `find-file`'s path
  completion) is further along than a from-scratch feature would be.
- **`kill-buffer` as a real command** (not just the tab-bar close icon) — closing a
  buffer currently requires a mouse click on its tab; a keyboard-reachable
  `C-x k`/`kill-buffer` is standard Emacs and there's no structural reason it's missing
  (`BufferView::RequestCloseBuffer`/`CloseBufferNow` already implement the logic the
  tab-bar close icon calls).
- **Format document on demand** — `FormatOnSave`'s machinery already does the hard part
  (shelling out, temp-file safety); exposing it as a standalone command independent of
  saving is a small, high-value addition.

### Maybe want
Plausible, but a real tradeoff either way — worth an explicit decision, not a default yes.

- **`Ctrl+S` for save** — the canonical non-Emacs binding, but it's Ned's `isearch-forward`
  today; changing it breaks a core Emacs binding for a non-Emacs one on the exact key an
  Emacs user would least expect it. If ever done, it should be additive/configurable
  (e.g. a Janet-toggleable "non-Emacs bindings" layer) rather than a silent swap.
- **`Ctrl+Z`/`Ctrl+Y` for undo/redo** — `Ctrl+Y` is already `yank` in Ned; same shape of
  collision as `Ctrl+S`, same "additive, not a swap" recommendation if pursued.
- **`Shift+arrows` for selection extension** — real value and expected by most users, but
  needs `C-SPC`/mark-setting to land first (they're the same underlying mark mechanism),
  and needs a design decision on how it interacts with Emacs' own mark/region model
  rather than just being bolted on as a separate GUI-style selection concept.
- **Multi-cursor editing** — already Phase 9 wishlist and has real value, but is a
  substantial design lift (editing model, not just a keybinding) or gets deferred with a
  simple 20-minute analysis; flagged here rather than under Want because the *keybinding*
  question genuinely depends on unresolved design questions.
- **Move line up/down, duplicate line** — genuinely useful, low collision risk (no
  standard Emacs binding at those keys to protect), but not on ROADMAP at all yet —
  worth a deliberate decision to adopt rather than assuming they belong just because
  they're common elsewhere.
- **Toggle line comment** — high real-world value, but `Ctrl+/` collides directly with
  `undo` in Ned; a different chord would be needed, and comment-syntax-per-language is
  a real design question (tree-sitter grammars know comment delimiters, so this is more
  tractable than it looks, but it's not free).

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
