# Editing Essentials

These are the pieces of Emacs' editing vocabulary that go beyond basic movement and
typing — most of them don't exist at all in editors outside the Emacs family, and are
worth understanding on purpose rather than discovering by accident.

## The kill ring

`C-k` (kill-line), `C-w` (kill-region), and `M-w` (kill-ring-save, i.e. copy) don't just
overwrite a single clipboard slot — they push onto a **ring** of everything you've
recently killed, one global ring shared across every buffer. `C-y` (yank) pastes the most
recent entry; immediately after a yank, `M-y` (`yank-pop`) replaces what was just pasted
with the next-older entry instead, and you can keep pressing `M-y` to walk back through
history. This means killing several things in sequence and then yanking them in a
different order, or in a different place, doesn't require re-selecting anything — just
yank, then pop until the entry you want comes up.

Killing and yanking work with [multiple cursors](#multiple-cursors) too: a kill made
with several cursors active becomes one kill-ring entry holding one piece of text per
cursor, and yanking it back places each piece at its corresponding cursor rather than
the whole blob at every one.

## Registers

A register is a single named slot — one character as its name (`a`, `1`, whatever you
pick) — that holds either a saved location or a piece of text, and unlike the kill ring,
writing to one doesn't affect any other register.

| Command | Effect |
|---|---|
| `point-to-register` | Save point's current location under a register name |
| `jump-to-register` | Jump back to a location saved in a register |
| `copy-to-register` | Save the current region's text into a register |
| `insert-register` | Insert a register's saved text at point |

Registers are good for exactly what they sound like: bookmarking a handful of specific
locations you'll want to jump back to repeatedly, or stashing a piece of boilerplate
text you'll insert more than once, without it getting buried under later kills the way a
kill-ring entry would.

## Rectangles

A rectangle operation treats the region between point and mark not as a run of text but
as a column-aligned block spanning several lines — select a region so its start and end
land in the columns you want, then:

| Command | Effect |
|---|---|
| `kill-rectangle` | Cut the rectangle, saving it for `yank-rectangle` |
| `delete-rectangle` | Delete the rectangle without saving it |
| `yank-rectangle` | Insert the last killed rectangle at point |
| `string-rectangle` | Replace the rectangle with a typed string, on every line |

This is the classic "delete this column of text across 40 lines at once" tool — for
example, stripping a common prefix, or inserting the same short string at the same
column on a run of lines.

## Multiple cursors

Beyond the primary cursor, ned can track any number of secondary cursors, each with its
own point and (optionally) its own mark, all edited simultaneously:

| Command | Effect |
|---|---|
| `add-cursor-above` / `add-cursor-below` | Add a cursor directly above/below the current set |
| `select-next-occurrence` | Select the word at point, or add a cursor at the next matching occurrence |
| `select-all-occurrences` | Add a cursor at every occurrence of the current selection |

Every ordinary editing command — typing, killing, indenting — applies at every cursor at
once, as a single undo step. Cursors created via the keyboard commands above are
deliberately the only way to create them; mouse-driven cursor creation (Alt+Click, as
seen in some GUI editors) isn't supported, since mouse passthrough is unreliable enough
over SSH/tmux that it isn't worth the inconsistency.

Undo clears every secondary cursor back to just the primary — a deliberate
simplification, so undo's meaning stays unambiguous when the cursor set itself was part
of what changed.

## Narrowing

`narrow-to-region` restricts editing and display to just the current region — the rest
of the buffer is still there, just hidden and unreachable until `widen` removes the
restriction. This is useful for working on one function or section of a large file
without the rest of it as a distraction, or to scope an operation (like a query-replace)
to exactly the lines you've narrowed to.

## The undo tree

Undo in ned isn't a flat stack the way it is in most editors — it's a real tree. Undoing
moves to the edit's parent; redoing moves to whichever child you most recently visited;
and making a *new* edit after an undo doesn't discard the redone-away branch, it creates
a sibling branch alongside it. Nothing is ever lost to a stray edit after an undo the way
it would be with a flat undo/redo stack — every state you've ever been in during the
current session is still reachable, just possibly down a branch you're not currently on.

`C-_` undoes, `M-/` redoes (redo follows the most-recently-visited child at each branch
point, so simply alternating undo/redo retraces your steps exactly).

## Next steps

- [Search and Replace](search-and-replace.md)
- [Windows, Buffers, and Sessions](windows-and-buffers.md)
