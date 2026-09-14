# FAQ and Troubleshooting

## ned won't start — "notcurses_core_init failed" or similar

Notcurses needs a resolvable terminfo entry for whatever `$TERM` is set to. This usually
only bites in a non-interactive context (a CI runner, a script that unset `$TERM`) — a
real terminal emulator sets it correctly on its own. If you hit this in a real terminal,
check `echo $TERM` resolves via `infocmp` at all; a very unusual or custom terminal type
might need a terminfo entry installed.

## Colors or Unicode look wrong under tmux/screen

Confirmed behavior, not a bug to report: tmux and screen both silently degrade
Notcurses' quadrant/sextant Unicode block rendering to plain half-blocks, and generally
constrain what capability probing can detect versus a real terminal. If something looks
subtly different inside tmux than outside it, try reproducing outside tmux first before
assuming it's ned.

## Ctrl+Space, mouse wheel-right, or paste don't work right on an older terminal

These are exactly the three behaviors ned's own Notcurses patches exist for (see
[Installation](installation.md#notcurses-specifics)) — a legacy terminal without the
kitty keyboard protocol or `modifyOtherKeys` swallows Ctrl+Space/Ctrl+@ as a raw NUL
byte, some terminals misreport SGR mouse wheel-right, and bracketed paste needs explicit
support to arrive as one event instead of a burst of individual keystrokes. If you built
ned against a Notcurses that doesn't carry these three patches, that's the likely cause
— see the Installation chapter for applying them.

## My theme doesn't look right — is my terminal the problem?

ned's themes are truecolor-only; there's no palette/ANSI-16 fallback tier, and a theme
is expected to carry its own contrast. A terminal that can't render truecolor is
Notcurses' problem to quantize down, not something ned adapts to specially. If a bundled
theme looks wrong specifically for you, check your terminal actually advertises
truecolor support (most modern ones do by default).

## An LSP/DAP/task command doesn't do anything

The most common cause: the configured `argv[0]` isn't actually on `$PATH` as far as the
process ned spawned sees it — resolution happens the same way a shell would resolve it,
against ned's own environment, not a shell you might have separately configured
`$PATH` in. Double-check the binary runs from a plain, non-interactive shell invocation
(`sh -c 'clangd --version'`, for instance) in the same environment ned was launched
from.

## A test I expect to run shows as skipped

This is likely intentional, not a bug — a test that depends on something environment-
specific (a system library that isn't installed, a feature the current platform doesn't
support) is written to skip cleanly rather than fail when that dependency is missing,
the same principle as [Installation](installation.md#running-the-test-suite)'s note
about the test suite in general.

## Where do I report a bug or request a feature?

[github.com/SuperFes/ned](https://github.com/SuperFes/ned) — issues and pull requests
both. `ROADMAP.md` in the repository root tracks known open work and design rationale
for planned features, and is worth a search before filing something that might already
be a deliberate, documented scope cut rather than an oversight.
