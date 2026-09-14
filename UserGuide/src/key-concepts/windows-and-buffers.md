# Windows, Buffers, and Sessions

## Buffers vs. windows

As in Emacs, a **buffer** is a piece of content (usually, but not always, backed by a
file) and a **window** is a viewport onto one — the same buffer can be visible in more
than one window at once, and switching what a window shows doesn't close anything.

## Splitting the screen

| Keys | Command | Effect |
|---|---|---|
| `C-x 2` | `split-window-below` | Split the current window horizontally |
| `C-x 3` | `split-window-right` | Split the current window vertically |
| `C-x 0` | `delete-window` | Close the current window |
| `C-x 1` | `delete-other-windows` | Close every window except this one |
| `C-x o` | `other-window` | Move focus to the next window |

Splits are recursive — you can split an already-split window again — and every window
is a fully independent pane with its own buffer, scrollbar/minimap, mode line, and
keyboard dispatch. `enlarge-window`/`shrink-window` (and their `-horizontally`
counterparts) resize the current window against its nearest split.

## The tab bar

Above the editing area, a one-row tab strip shows every open buffer as a colored block.
Click to switch, drag to reorder, scroll when there are more tabs than fit, and the close
icon on each tab prompts before discarding unsaved changes. Right-clicking a tab opens a
context menu (close, close others, close to the right, reveal in project sidebar).

## Managing buffers

| Keys | Command | Effect |
|---|---|---|
| `C-x b` | `switch-to-buffer` | Switch this window to a different buffer |
| `C-x k` | `kill-buffer` | Close the current buffer |
| — | `list-buffers` | A keyboard-navigable buffer list (mark, kill, switch) |
| — | `find-scratch` | Open (or jump to) the persistent scratch buffer |

The scratch buffer is disk-backed under ned's own data directory and auto-saves
periodically — a good place for throwaway notes that should still survive a restart.

## The project sidebar

`toggle-project-sidebar` shows or hides a file tree for the current project;
`focus-project-sidebar` moves keyboard focus into it for arrow-key navigation (or just
click into it — it's mouse-primary). Directories start collapsed. A single click opens a
file as a transient, VS Code-style **preview** — it reuses an already-open buffer if one
exists, and a fresh preview buffer is silently replaced by the next file you preview,
rather than piling up tabs — while a double-click promotes it to a real, permanent
buffer. `project-find-file` is the keyboard-driven equivalent: fuzzy-find a file by name
without touching the mouse or the sidebar at all.

## Save-place and sessions

ned remembers where you left off, at two levels:

- **Per-file save-place** — the last cursor position and viewport scroll for every file
  you've had open, restored automatically the next time you open that file, regardless
  of project.
- **Per-project sessions** — for a directory that looks like a real project (a `.git`
  marker or similar), ned can additionally remember which files were open, which one was
  active, the project-sidebar state, and your window-split layout, restoring the whole
  arrangement the next time you open that project. This is opt-in in the sense that it
  only activates for a directory with a genuine project marker — a random folder doesn't
  get a session file.

Neither of these requires any action on your part beyond just using ned normally; both
are pure conveniences that write to ned's own state directory, never into your project.

## Next steps

- [Editing Essentials](editing-essentials.md)
- [Language Intelligence](../features/language-intelligence.md)
