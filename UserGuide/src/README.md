# Introduction

**ned** ("Native Text Editor") is a terminal-based text editor aiming for Emacs-class
feature parity, with [Janet](https://janet-lang.org/) filling the role Elisp plays in
Emacs. The editor is scriptable throughout — buffers, commands, keymaps, modes,
LSP/DAP/VCS/task-runner configuration — rather than being a fixed application with a
config file bolted on. Its terminal UI is a from-scratch widget/layout/event-loop layer
built directly on [Notcurses](https://github.com/dankamongmen/notcurses).

If you've used Emacs, most of ned's vocabulary will already be familiar: buffers, the
kill ring, a real undo *tree* (not a flat undo stack), incremental search, query-replace,
registers, rectangles, `M-x`, and a Janet-scriptable init file playing Elisp's role. If
you haven't, that's fine too — this guide doesn't assume it.

## What this guide covers

- **[Installation](installation.md)** — building ned from source (there's no packaged
  release yet).
- **[Getting Started](getting-started.md)** — launching ned, moving around, opening and
  saving files, and the M-x/help surface.
- **[Configuration](configuration.md)** — `init.janet`, where it lives, and the shape of
  a typical configuration.
- **Key Concepts** — the Emacs-class editing vocabulary: kill ring, registers,
  rectangles, multiple cursors, narrowing, undo tree, search and replace across one
  buffer or a whole project, window splits, and session persistence.
- **Features** — the larger integrations: language servers (LSP), debugging (DAP),
  version control, snippets, Org mode, Vim emulation, task/test runners, and the
  embedded terminal and agent panels.
- **Reference** — per-language setup recipes, theming, the full Janet scripting surface,
  and the complete command list.

## A note on how this guide is generated

Two reference chapters — [Scripting](scripting.md) and the
[Command Reference](commands.md) — aren't hand-maintained. They're pulled directly from
ned's own command registry and Janet binding table, and the build fails if either one
drifts from what's actually registered. If a command or binding exists, it's documented;
if this guide says a command exists, you can trust that it does, today, in the version
you're running.

## Getting help inside ned

Everything documented here is also discoverable from inside the editor itself:

- `M-x execute-extended-command` (bound to `M-x`) fuzzy-matches against every registered
  command by name.
- `C-h k` (if bound in your configuration) or reading a command's own docstring via the
  Janet REPL (`(doc 'command-name)`) shows what a specific command does.
- The [Command Reference](commands.md) and [Scripting](scripting.md) chapters are the
  same information, laid out for browsing rather than fuzzy-searching.

## Project links

- Source: [github.com/SuperFes/ned](https://github.com/SuperFes/ned)
- Developer-facing design docs (architecture deep dives, capability audits): the
  [Developer Docs](../../dev/) book, built from the same repository.
- Open work and design rationale for planned features: `ROADMAP.md` in the repository
  root.
