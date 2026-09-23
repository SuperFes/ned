# Modern Keymap

ned's default keybindings are Emacs' own (`C-x`/`C-c` as prefix keys, `C-w`/`M-w`/`C-y`
for cut/copy/paste, and so on). For editing conventions closer to VS Code/JetBrains,
switch the keymap style instead:

```janet
(ned/set-keymap-style "modern")
```

`"emacs"` (the default) and `"vim"` (see [Vim Mode](vim-mode.md)) are the other two
values — the three are mutually exclusive whole-keymap styles, not switches layered on
top of each other.

## What changes

Only the handful of chords a modern editor gives universal meaning to are remapped, each
relocating whatever the Emacs default bound there:

| Chord  | Modern meaning                          | Emacs default it replaces |
|--------|------------------------------------------|----------------------------|
| `C-c`  | Copy (current line if nothing selected)   | `C-c` prefix (VCS, debug panel, org, projects, ...) |
| `C-x`  | Cut (current line if nothing selected)    | `C-x` prefix (file/window ops) |
| `C-v`  | Paste (replacing an active selection)     | `scroll-page-down` |
| `C-z`  | Undo                                      | `suspend-frame` |
| `C-y`  | Redo                                      | `yank` |
| `C-a`  | Select all                                | `beginning-of-line` |
| `C-s`  | Save                                      | `isearch-forward` |
| `C-f`  | Find (incremental search)                 | `forward-char` |
| `C-o`  | Open file                                 | `open-line` |
| `C-w`  | Close buffer                              | `kill-region` |
| `C-/`  | Toggle line comment                       | *(unbound in the Emacs default)* |
| `C-Tab` / `C-S-Tab` | Next / previous tab          | `C-x RIGHT` / `C-x LEFT` |

Copy/cut/paste are real commands of their own (`modern-copy`/`modern-cut`/
`modern-paste`), not aliases for `kill-ring-save`/`kill-region`/`yank` — the "nothing
selected" fallback to the current line, and paste replacing an active selection, are
both genuine modern-editor conventions Emacs' own commands don't share, so they needed
their own implementation. They still share the kill ring with `yank`/`yank-pop`, and
still see the system clipboard, exactly as the Emacs-default commands do.

Everything else the Emacs default binds — arrow keys, Home/End, Page Up/Down, `M-g g`
goto-line, `M-w`... — is untouched and keeps working, since this style only overrides
the chords listed above and otherwise dispatches through the same default keymap.

## What you lose: every other `C-c`/`C-x <key>` binding

Because `C-c` and `C-x` become commands in their own right rather than prefix keys,
every longer sequence built on top of them in the Emacs default — `C-c ?`
(describe-bindings), `C-c x` (merge conflicts), `C-c P` (projects), `C-c #`
(color-at-point), `C-x r y` (yank-rectangle), and dozens more — becomes unreachable by
keystroke. Nothing is removed: every one of those commands is still registered and
still reachable by name through `M-x` or the search-everywhere palette
(double-tap-Shift). This style is a keybinding convention on top of the same command
set, not a smaller editor.

## Next steps

- [Vim Mode](vim-mode.md) — the other alternative keymap style.
- [Editing Essentials](../key-concepts/editing-essentials.md) — the full Emacs-style
  command vocabulary this style still runs on underneath.
- [Configuration](../configuration.md)
