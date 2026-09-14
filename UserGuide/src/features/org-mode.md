# Org Mode

ned bundles Org-mode-style structured editing for `.org` files: outline folding, TODO
workflow, scheduling, clocking, property drawers, checkboxes, tables, and links — the
Org syntax and conventions, reimplemented directly rather than shelling out to Emacs.

## Outline and TODO state

| Command | Effect |
|---|---|
| `org-cycle` | Cycle the fold state of the subtree at point (or realign a table, if point is in one) |
| `org-cycle-todo` | Cycle the headline's TODO keyword |
| `org-cycle-priority` | Cycle its `[#A]`/`[#B]`/`[#C]` priority cookie |
| `org-set-tags` | Set colon-separated tags (`work:urgent`) |
| `org-metaup` / `org-metadown` | Move the current line (or table row) up/down |

TODO keywords are configurable via `ned/set-org-todo-keywords`/`ned/org-todo-keywords`
if the default set doesn't match your workflow.

## Checkboxes

`org-toggle-checkbox` toggles a `- [ ]`/`- [X]` checkbox at point, propagating the change
up into any parent checkbox (a parent shows `[X]` once every child is checked, `[-]` for
a partial state).

## Scheduling and deadlines

`org-schedule` and `org-deadline` set a headline's `SCHEDULED:`/`DEADLINE:` timestamp,
supporting Org's repeater syntax (`+1w`, `++1m`, etc.) for recurring items.
`org-agenda` lists every active (non-`DONE`) TODO headline across every `.org` file in
the current project, in one place.

## Clocking time

| Command | Effect |
|---|---|
| `org-clock-in` | Start the clock on the headline at point |
| `org-clock-out` | Stop whichever clock is currently running |
| `org-clock-report` | List clocked time per headline, with subtree totals |

Clock entries are stored in the headline's own `:LOGBOOK:` drawer, same as real Org, and
the current clock (if any) shows in the mode line.

## Properties

`org-set-property` / `org-delete-property` manage a headline's `:PROPERTIES:` drawer —
useful for a `:CUSTOM_ID:` to link to, or any other per-headline metadata your workflow
needs.

## Capture templates

`org-capture` inserts a note directly into a registered template's target file/headline,
no separate finalize step needed. Register templates from `init.janet`:

```janet
(ned/org-capture-register-template "t" "* TODO %?" "~/notes/tasks.org")
```

## Tables

Org's plain-text tables are fully editable: `org-table-align` realigns column widths to
content, `org-table-insert-row`/`-column` (and their delete/move counterparts, see the
[Command Reference](../commands.md)) manage structure, and `org-table-previous-cell`
(with its Tab-bound counterpart for "next cell") navigates between cells, realigning as
you go. The same table-editing engine backs GFM tables in Markdown files too.

## Links

`[[target][description]]`-style bracket links resolve to another headline (by title or
`:CUSTOM_ID:`) or an external file/URL, openable at point the same way any other
detected link is.

## Next steps

- [Vim Mode](vim-mode.md)
- [Configuration](../configuration.md)
