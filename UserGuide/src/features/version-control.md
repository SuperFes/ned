# Version Control

ned's VCS support is provider-agnostic — the built-in commands below talk to a plugin
interface, and git is the bundled reference implementation. Everything works against a
real git working tree with no extra configuration.

## Diff gutter and per-hunk actions

An open file that's part of a git working tree shows added/changed/removed lines in the
gutter automatically. From any line in a changed hunk:

| Command | Effect |
|---|---|
| `vcs-next-hunk` / `vcs-previous-hunk` | Jump between changed hunks in this buffer |
| `vcs-stage-hunk` | Stage just this hunk |
| `vcs-unstage-hunk` | Unstage this hunk |
| `vcs-revert-hunk` | Discard this hunk from the working tree |

## The status panel and side panel

`vcs-status` opens a `*vcs status*` buffer listing staged/unstaged/untracked files;
`vcs-stage-file` / `vcs-unstage-file` act on the file at point (or the current buffer's
own file). A persistent VCS side panel is also available — the same tree-shaped surface
as the project sidebar, but for the working tree's changed files, with multi-select
batch stage/unstage.

## Committing

`vcs-commit` opens a `*vcs commit message*` buffer for the staged changes. Compose your
message, then:

- `C-c C-c` (`vcs-commit-finish`) commits it.
- `C-c C-k` (`vcs-commit-abort`) discards the in-progress message.

## History and blame

| Command | Effect |
|---|---|
| `vcs-show-log` | Commit history for the current file |
| `vcs-show-blame` | Per-line commit attribution, inline in the gutter |
| `vcs-blame-buffer` | The same attribution as a dedicated `*vcs blame*` buffer |
| `vcs-blame-detail-at-point` | Full commit info (author/date/summary) for the blamed line at point |
| `vcs-full-diff-buffer` | Every changed file's diff, stitched into one buffer |

## Branches

`vcs-branches` lists branches; `vcs-switch-branch` switches (with Tab-completion over the
branch list); `vcs-create-branch` creates and switches to a new one.

## Next steps

- [Tasks and Tests](tasks-and-tests.md)
- [Language Intelligence](language-intelligence.md)
