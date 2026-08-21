# Ned

[![Build & Test](https://github.com/SuperFes/ned/actions/workflows/build-test.yml/badge.svg)](https://github.com/SuperFes/ned/actions/workflows/build-test.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)

A terminal-based text editor written in modern C++23, aiming for Emacs-class feature
coverage with [Janet](https://janet-lang.org/) filling the role Elisp plays in Emacs —
the editor is scriptable and extensible throughout (buffers, commands, keymaps, modes,
LSP/DAP/VCS/task-runner configuration), not a fixed app with a config file bolted on.
The terminal UI is a from-scratch widget/layout/event-loop layer built directly on
[Notcurses](https://github.com/dankamongmen/notcurses).

!(Docs/Screenshot_20260821_151203.png)

## Features

- **Buffers, kill-ring, undo tree, isearch, query-replace, registers, rectangles,
  multiple cursors** — the core Emacs-class editing vocabulary.
- **Janet scripting throughout** — commands, keybindings, modes, and most editor
  settings are reachable from a `~/.config/ned/init.janet`, not a fixed config format.
- **Tree-sitter syntax highlighting** for C, C++, PHP, JavaScript, TypeScript/TSX,
  HTML, CSS, Python, Bash, JSON, Janet, Markdown, YAML, TOML, Clojure/Jank, and Org,
  with per-capture-name theming and code folding.
- **LSP and DAP clients** for language server features (diagnostics, completion, code
  actions, go-to-definition, rename) and debugging (breakpoints, stepping, variable
  inspection).
- **VCS integration** through a provider-agnostic plugin interface (bundled reference
  implementation for git) — blame, log, diff, stage/unstage (including per-hunk),
  commit, branches.
- **A built-in terminal panel** (libvterm-backed), task runner, and Emacs-style
  recursive window splitting.
- **Org-mode-style structured editing** — headlines, TODO states, checkboxes, tables,
  links, project-wide agenda — plus GFM table editing for Markdown.
- **Project-wide search/replace, a file sidebar, session persistence** (restores open
  files, point, and window layout per project), and a handful of bundled/clonable
  color themes.

See [`ROADMAP.md`](ROADMAP.md) for what's still open.

## Requirements

- CMake 3.29+
- Clang (the documented build uses Clang + LLD explicitly — see `CMakePresets.json`)
- [Janet](https://janet-lang.org/) installed and discoverable via `pkg-config`

Everything else — Notcurses, `utf8proc`, CLI11, `nlohmann/json`, tree-sitter core plus
every bundled grammar, `libvterm`, and Catch2 (tests) — is fetched automatically by
CMake via `FetchContent`; no manual setup needed for those.

## Build

```sh
cmake --preset default   # Clang + LLD; see CMakePresets.json
cmake --build build
./build/ned
```

Run the test suite with `ctest --test-dir build`.

## Status

Under active development. Real, daily-usable, and extensively tested (1495+ tests via
`ctest`), but pre-1.0 — the Janet API surface, config file formats, and keybindings may
still change. See [`ROADMAP.md`](ROADMAP.md) for what's open next.

## License

MIT — see [`LICENSE`](LICENSE). Third-party dependency licenses are listed in
[`THIRD-PARTY-LICENSES.md`](THIRD-PARTY-LICENSES.md).
