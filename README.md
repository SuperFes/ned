# Ned

A terminal-based text editor written in modern C++, aiming for Emacs-class feature
coverage with [Janet](https://janet-lang.org/) filling the role Elisp plays in Emacs —
the editor is meant to be scriptable and extensible throughout, not a fixed app with a
config file bolted on. The terminal UI is built on [TermOx](https://github.com/a-n-t-h-o-n-y/TermOx).

This is early-stage: the current tree is a minimal skeleton (a Janet VM wired up, a
placeholder TermOx window, and a not-yet-editable buffer type). See
[`ROADMAP.md`](ROADMAP.md) for the full project plan and current phase.

## Requirements

- CMake 3.26+
- A C++20-capable compiler (moving to C++23, see `ROADMAP.md`)
- [Janet](https://janet-lang.org/) installed and discoverable via `pkg-config`

TermOx and its dependencies (escape, signals-light, zzz) are fetched automatically by
CMake via `FetchContent` — no manual setup needed for those.

## Build

```sh
cmake -S . -B build
cmake --build build
./build/ned
```

## Status

No test suite yet. See `ROADMAP.md` for planned work, in order: text/buffer core,
command & keymap system, Janet-as-extension-language integration, window/frame UI,
editing feature parity, and — once the editor itself works — advanced TermOx theming
(gradients, fades).
