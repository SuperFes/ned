#!/usr/bin/env bash
# Rebuild this directory's .patch files from CMake/PatchNotcurses*.cmake, which
# are the source of truth (see README.md). Run after editing any of those
# scripts, or after bumping the pinned Notcurses version in CMakeLists.txt.
#
# Each script is replayed onto a throwaway pristine checkout and committed, in
# CMakeLists.txt's own PATCH_COMMAND order, so every patch's context reflects
# the tree as it stands after the previous one -- which is what lets them apply
# in sequence.
#
# Output is byte-stable: re-running with nothing changed reproduces identical
# files. That relies on the commit date being derived rather than "now" -- each
# patch is dated from the last commit that touched the CMake script it comes
# from, so the date moves only when the patch's content actually does.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="$repo/Patches/notcurses"

# Order must match CMakeLists.txt's PATCH_COMMAND chain.
scripts=(PatchNotcursesNulKey PatchNotcursesMouseWheel PatchNotcursesBracketedPaste)

# The pinned tag, read from CMakeLists.txt rather than duplicated here.
tag="$(sed -n '/FetchContent_Declare(notcurses/,/)/p' "$repo/CMakeLists.txt" \
       | sed -n 's/.*GIT_TAG[[:space:]]\+\([^[:space:]]*\).*/\1/p' | head -1)"
[ -n "$tag" ] || { echo "regenerate: no notcurses GIT_TAG in CMakeLists.txt" >&2; exit 1; }

# Prefer the already-fetched tree (no network); fall back to upstream.
src="$repo/build/_deps/notcurses-src"
[ -d "$src/.git" ] || src="https://github.com/dankamongmen/notcurses.git"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
git clone -q --shared "$src" "$work" 2>/dev/null || git clone -q "$src" "$work"
git -C "$work" checkout -q "$tag"
echo "pristine notcurses at $tag"

git_c=(git -C "$work" -c user.name=Ned -c user.email=ned@localhost)

i=1
for script in "${scripts[@]}"; do
    n="$(printf '%04d' "$i")"
    msg="$out/messages/$n.txt"
    [ -f "$msg" ] || { echo "regenerate: missing $msg" >&2; exit 1; }

    # Date the patch from the CMake script it derives from, not from now.
    when="$(git -C "$repo" log -1 --format=%aI -- "CMake/$script.cmake")"
    [ -n "$when" ] || when="1970-01-01T00:00:00+00:00"

    (cd "$work" && cmake -P "$repo/CMake/$script.cmake" >/dev/null)
    "${git_c[@]}" add -A
    GIT_AUTHOR_DATE="$when" GIT_COMMITTER_DATE="$when" \
        "${git_c[@]}" commit -q -F "$msg"
    echo "  applied $script.cmake"
    i=$((i + 1))
done

rm -f "$out"/*.patch
"${git_c[@]}" format-patch -q --no-signature --zero-commit --no-numbered \
    -o "$out" "$tag..HEAD"

echo
echo "regenerated:"
ls -1 "$out"/*.patch | xargs -n1 basename | sed 's/^/  /'
