# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Ned ("Native Text Editor") is a terminal-based text editor written in C++23, aiming for
Emacs-class feature parity with Janet filling the role Elisp plays in Emacs — the editor
is scriptable throughout (buffers, commands, keymaps, modes, LSP/DAP/VCS/task-runner
config), not a C++ app with a config file bolted on. Its TUI is rendered directly on
**Notcurses** (`Source/UI/`, `ned::ui` namespace) — a from-scratch widget/layout/event-loop
layer this project owns, not a wrapper around a higher-level TUI framework. See
`ROADMAP.md` for open work; it's pruned to current open items only, with completed work's
design history left in git log rather than duplicated here.

This file documents current architecture only. It does not narrate how things got this
way (migrations, phase numbers, "was X, changed to Y") — that history is in `git log`
and in the source's own comments where it matters for a specific invariant.

## Architecture

Things should be neat and organized, refactoring things into subdirectories where it makes
sense, using namespaces internally hurts nothing, so moving things around and keeping
everything clean and tidy will keep the code easy to work on.

## Conventions

- **XDG Base Directories.** Any file read/written outside the project directory or an
  explicitly-given path (config, cached data, history/recent-files, etc.) goes under
  `$XDG_CONFIG_HOME`/`$XDG_DATA_HOME`/`$XDG_CACHE_HOME`/`$XDG_STATE_HOME` (falling back to
  their spec defaults under `~/.config`, `~/.local/share`, `~/.cache`, `~/.local/state` if
  unset), in a `ned/` subdirectory — never a bare dot-file/dot-dir directly in `$HOME`.
  Config (`init.janet`) lives under `$XDG_CONFIG_HOME`; user content
  (scratches) under `$XDG_DATA_HOME`; disposable editor state (save-place,
  sessions, trust, backups, remembered variables) under `$XDG_STATE_HOME`. A project-local
  `.ned/` directory is strictly opt-in — nothing in this codebase ever creates one
  silently.
- **Memory safety.** No raw owning pointers or manual `new`/`delete` in editor code —
  `std::unique_ptr`/`shared_ptr`, `std::string`/`string_view`, `std::vector`/`std::span`.
  Janet's own C heap is external and stays malloc'd internally by the library; this is
  about the C++ side only.
- **Bundled data is read from disk, never embedded.** Language packages -- definition,
  `grammar.janet`, query files, corpora (`Source/Languages/<name>/`) -- and Janet plugins
  (`Source/Janet/Plugins/`) are read at runtime from `DataDir()` (`Editor/DataDir.h`:
  `$NED_DATA_DIR`, else `<exe>/../share/ned`, else the configured install datadir). The
  build tree assembles `build/share/ned/` (`CMake/DataTree.cmake`) and compiles each
  grammar's `tables` beside it with `ned --compile-language` (`CMake/LanguageTables.cmake`)
  so a dev build and an installed prefix resolve identically; `install()` puts the same
  tree under `share/ned/`. Text/data goes under `share/ned`, helper executables not meant
  for `$PATH` under `libexec/ned`.
- **No tree-sitter anywhere.** Grammars are `grammar.janet`, compiled by ned's own
  generator (`Editor/Grammar/Compile/`) and run by ned's own engine (`Editor/Parse/`);
  external scanners are ned code against `Editor/Parse/Scanner.h`. A new language comes
  in as a package (grammar, scanner, queries), never as generated C.
- **Static dispatch by default; an interface only where the type is genuinely unknown.**
- **Mutex-guarded static state for process-wide settings.** The dominant pattern for
- **`Set*`/register-then-connect widget wiring.** UI widgets are constructed with only

See `ROADMAP.md` for open work and `git log`/`git show <rev>:ROADMAP.md` for the design
history of completed features (pruned out of this file and out of `ROADMAP.md` itself as
of 2026-08-20).
