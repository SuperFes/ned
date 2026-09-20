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

## Using ned as your VCS editor

ned works as the editor your version control system launches for a commit message, a tag
annotation or a rebase todo. Point the relevant setting at it:

```sh
git config --global core.editor ned      # git
export HGEDITOR=ned                      # mercurial
export SVN_EDITOR=ned                    # subversion
export JJ_EDITOR=ned                     # jujutsu
fossil settings editor ned               # fossil
```

Such a run is **transient**: ned recognizes the message file by name and stores nothing
about it. No project session is restored or saved, no save-place entry, no recent-files
entry, no persistent undo, no backup versions, and no project-local `.ned/` config is
loaded or prompted about.

That last point is the one that matters most. A version control system runs your editor
from the repository root, so without this a commit would quit having replaced the
project's real saved session with one containing nothing but `COMMIT_EDITMSG` — the next
`ned` in that repository would reopen the commit message instead of your work.

Detection covers every file the five tools above name, including git's `MERGE_MSG`,
`TAG_EDITMSG`, `SQUASH_MSG`, `NOTES_EDITMSG` and `git-rebase-todo`. It keys on the
filename alone, so it holds for worktrees, submodules and a relocated `GIT_DIR`.

For a tool whose editor file has a random name — `crontab -e`, `sudoedit`, `cvs`, or
`gh pr create` — say so explicitly:

```sh
export EDITOR="ned --transient"
```

`--no-transient` forces a normal, recorded run even for a file that would otherwise be
detected, for the rare case where you really do want the commit message in your session.

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
