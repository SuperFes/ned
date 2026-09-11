# Notcurses patches

Three fixes ned needs from Notcurses, as `git am`-able / `patch -p1`-able files,
for building against a **system** Notcurses instead of the FetchContent copy.

Generated against **notcurses v3.0.17** (`7767278`). Verified 2026-09-11: all
three apply with `patch -p1` in numeric order against a pristine v3.0.17, and
the result is byte-identical to the tree the CMake scripts produce.

| Patch | Fixes |
|---|---|
| `0001-...-Ctrl-Space...` | Ctrl+Space / Ctrl+@ swallowed as a NUL byte on a terminal with neither kitty keyboard protocol nor modifyOtherKeys (a default tmux) |
| `0002-...-wheel-right...` | SGR wheel-right (`Cb=67`) misdecoded as motion+release, losing the scroll |
| `0003-...-bracketed-paste...` | Paste boundaries not surfaced as events, so a paste arrives as N individual keypresses |

## Ordering is load-bearing

All three touch `src/lib/in.c`, and each is generated against the tree as it
stands *after* the previous one. Apply in numeric order. Out of order, the
context lines will not match.

## Source of truth

These files are **derived**. The originals are the CMake
string-replacement scripts under `CMake/PatchNotcurses*.cmake`, which
`CMakeLists.txt` runs as Notcurses' `FetchContent` `PATCH_COMMAND` — that is
what the normal in-tree build uses, and it is what must be edited to change
behaviour. These `.patch` files exist because a distro package manager wants a
unified diff, not a CMake script.

Being derived, they can go stale. `./regenerate.sh` rebuilds them from the
CMake scripts; run it after touching any `PatchNotcurses*.cmake`, or after
bumping the pinned Notcurses version in `CMakeLists.txt`.

The CMake scripts each `FATAL_ERROR` if their anchor text no longer matches, so
a Notcurses bump that moves the code fails the configure loudly rather than
silently dropping a fix. That check protects the in-tree build; `regenerate.sh`
is what propagates the result here.

## Using them

Gentoo ebuild:

```bash
PATCHES=(
    "${FILESDIR}"/0001-in-recover-Ctrl-Space-Ctrl-on-legacy-terminals.patch
    "${FILESDIR}"/0002-in-decode-SGR-wheel-right-Cb-67-instead-of-misreport.patch
    "${FILESDIR}"/0003-in-report-bracketed-paste-boundaries-as-events.patch
)
```

By hand:

```sh
cd notcurses-3.0.17
for p in /path/to/ned/Patches/notcurses/*.patch; do patch -p1 -i "$p"; done
# or, in a git checkout:
git am /path/to/ned/Patches/notcurses/*.patch
```

## Upstreaming

Not submitted anywhere yet — a deliberate choice, tracked in `ROADMAP.md`'s
"Notcurses Patches Worth Upstreaming" watch list. 0001 and 0002 were verified
still-reproducible against upstream `master` as of 2026-09-06 and appear to be
unreported. 0003 relates to upstream issue
[#2704](https://github.com/dankamongmen/notcurses/issues/2704), but takes a
deliberately smaller shape than the design sketch there.

Each patch carries a real commit message, so they are PR-ready as-is.
