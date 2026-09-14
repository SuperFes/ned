# Getting Started

## Launching ned

```sh
ned                  # opens with an empty scratch buffer
ned path/to/file.cpp # opens a file
ned some-directory/   # opens a directory as a project (project sidebar, search, etc.)
ned file1 file2      # extra paths beyond the first open as background buffers
```

If a file argument isn't inside a recognized project (a directory containing `.git` or
similar), ned still opens it fine — project-wide features (search, sidebar) just have
less to work with.

## The keybinding model

ned follows Emacs' notation and conventions: `C-x` means Ctrl+X, `M-x` means Alt+X (or
`ESC` followed by `x` on a terminal that eats Alt), and a sequence like `C-x C-s` means
"press Ctrl+X, release, then press Ctrl+S." Multi-key sequences and prefix keys (`C-x`,
`C-c`) are pervasive, by design — they're what leaves the rest of the keyboard free for
direct single-key commands.

If you already know Emacs, skip to whichever [Feature](features/language-intelligence.md)
chapter you care about — the defaults below are unchanged from what you'd expect. If you
don't, the essentials are genuinely short:

### Moving around

| Keys | Action |
|---|---|
| `C-f` / `C-b` | forward / backward one character |
| `C-n` / `C-p` | next / previous line |
| `C-a` / `C-e` (also `HOME`/`END`) | beginning / end of line |
| `M-f` / `M-b` | forward / backward one word |
| `C-v` / `M-v` (also `PAGEDOWN`/`PAGEUP`) | scroll one page down / up |
| `M-<` / `M->` | beginning / end of buffer |
| Arrow keys, mouse click/scroll/drag | work as expected too |

### Basic editing

| Keys | Action |
|---|---|
| `C-d` | delete the character under point |
| `DEL` (Backspace) | delete the character before point |
| `C-k` | kill (cut) from point to end of line |
| `C-SPC` | set the mark, starting a region |
| `C-w` | kill (cut) the region |
| `M-w` | copy the region to the kill ring, without deleting it |
| `C-y` | yank (paste) the most recent kill |
| `C-_` | undo |
| `M-/` | redo |

A "region" is the stretch of text between point (the cursor) and the mark, exactly like
Emacs — set the mark with `C-SPC`, move point to extend the selection, then act on it.
Shift+arrow-key selection also works and sets an anonymous mark for you.

### Files and buffers

| Keys | Action |
|---|---|
| `C-x C-f` | find-file — open a file (creates it if it doesn't exist yet) |
| `C-x C-s` | save the current buffer |
| `C-x b` | switch to another open buffer |
| `C-x k` | kill (close) the current buffer |
| `C-x C-c` | quit ned |

See [Windows, Buffers, and Sessions](key-concepts/windows-and-buffers.md) for splitting
the screen, tab behavior, and what gets remembered between sessions.

### Search

| Keys | Action |
|---|---|
| `C-s` / `C-r` | incremental search forward / backward |
| `M-%` | query-replace (interactive find-and-replace) |

See [Search and Replace](key-concepts/search-and-replace.md) for project-wide search,
regex replace, and the review-buffer workflow larger replacements go through.

### The universal escape hatches

| Keys | Action |
|---|---|
| `C-g` | cancel whatever's in progress (a search, a prompt, a pending prefix key, an active mark) |
| `M-x` (or `ESC x`) | run any command by name, with fuzzy-matched completion |

`M-x` is worth internalizing early: every capability in ned is a named command reachable
this way, whether or not it has a keybinding. Typing `M-x` and a few fuzzy-matched
letters of a command's name is often faster than remembering a chord, and it's how you
discover commands that don't have a default binding at all. The
[Command Reference](commands.md) lists every one of them, and
[Configuration](configuration.md) covers binding your own keys to the ones you use often.

## Saving and quitting

`C-x C-s` saves the current buffer; `C-x C-c` prompts to save any modified buffers and
then exits. If you started ned without a project directory, closing the last buffer
still leaves the editor open on an empty scratch buffer rather than exiting outright.

## Next steps

- [Configuration](configuration.md) — where `init.janet` lives and what a typical one
  looks like.
- [Editing Essentials](key-concepts/editing-essentials.md) — the kill ring, registers,
  rectangles, multiple cursors, and the undo tree in more depth.
- [Language Intelligence](features/language-intelligence.md) — if you're editing code in
  a language with an LSP server available, this is the biggest quality-of-life feature
  to turn on next.
