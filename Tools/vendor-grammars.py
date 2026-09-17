#!/usr/bin/env python3
"""Vendors bundled tree-sitter grammar sources into ThirdParty/tree-sitter-grammars/.

ned used to pull every grammar via CMake's FetchContent at *configure* time --
a fresh network fetch of ~35 upstream repos on every clean build tree. This
script replaces that: it is the one place in this project that touches the
network for grammar sources, run by hand when a language is added or an
existing grammar's pinned ref is bumped. CMakeLists.txt then just compiles
whatever's checked into ThirdParty/ -- no network involved in an ordinary
build.

What gets vendored per grammar, and no more: the whole `src/` directory
(parser.c, scanner.c/.cc if present, any local helper headers a hand-written
scanner splits out -- e.g. tree-sitter-html's scanner.c#include`s a sibling
tag.h -- `tree_sitter/*.h`, and `grammar.json`/`node-types.json`, the latter
two read by Tests/ImprintTest.cpp's live-inference check, not by the build
itself) and `queries/` (kept for diffing against Source/Languages/<name>/
upstream/ by hand). A grammar's test corpus is NOT vendored here: it lives in
Source/Languages/<name>/corpus/ as ned's own, imported once. A few grammars
(typescript/tsx, php, xml) share a `common/scanner.h` one level above their
own `src/`, included via a relative `#include "../../common/scanner.h"` --
this script detects that by scanning the fetched scanner for `#include "../`
lines and vendors whatever directory those resolve to as well, so it isn't
hardcoded per grammar.

Usage:
    Tools/vendor-grammars.py --list                 # print the manifest
    Tools/vendor-grammars.py <name> [<name> ...]     # (re-)vendor these entries
    Tools/vendor-grammars.py --all                   # (re-)vendor everything

Add a language by adding one MANIFEST entry below and running this script for
it, then point CMakeLists.txt's ned_add_treesitter_grammar_target at the
resulting ThirdParty/tree-sitter-grammars/<name>[/<subdir>] directory -- see
CMakeLists.txt's tree-sitter section for the existing calls.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import re
import shutil
import sys
import tarfile
import urllib.request
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEST_ROOT = REPO_ROOT / "ThirdParty" / "tree-sitter-grammars"


@dataclass
class Grammar:
    name: str  # destination dir under DEST_ROOT, and the CMake target name
    org_repo: str = ""  # "org/repo" on GitHub, fetched as a tag/commit tarball
    ref: str = ""  # tag or commit sha
    subdirs: tuple[str, ...] = ("",)  # "" = repo root is the grammar dir
    release_url: str = ""  # alternative to org_repo+ref: a direct tarball URL
    release_sha256: str = ""


# Mirrors CMakeLists.txt's tree-sitter section 1:1 -- keep the two in sync.
# fmt: off
MANIFEST: list[Grammar] = [

    Grammar("tree-sitter-json", "tree-sitter/tree-sitter-json", "v0.24.8"),
    Grammar("tree-sitter-c", "tree-sitter/tree-sitter-c", "v0.24.2"),
    Grammar("tree-sitter-cpp", "tree-sitter/tree-sitter-cpp", "v0.23.4"),
    Grammar("tree-sitter-php", "tree-sitter/tree-sitter-php", "v0.24.2", subdirs=("php",)),
    Grammar("tree-sitter-javascript", "tree-sitter/tree-sitter-javascript", "v0.25.0"),
    Grammar("tree-sitter-typescript-src", "tree-sitter/tree-sitter-typescript", "v0.23.2", subdirs=("typescript", "tsx")),
    Grammar("tree-sitter-html", "tree-sitter/tree-sitter-html", "v0.23.2"),
    Grammar("tree-sitter-css", "tree-sitter/tree-sitter-css", "v0.25.0"),
    Grammar("tree-sitter-python", "tree-sitter/tree-sitter-python", "v0.25.0"),
    Grammar("tree-sitter-bash", "tree-sitter/tree-sitter-bash", "v0.25.1"),
    Grammar("tree-sitter-janet-simple", "sogaiu/tree-sitter-janet-simple", "v0.0.7"),
    Grammar("tree-sitter-markdown", "tree-sitter-grammars/tree-sitter-markdown", "v0.5.3",
            subdirs=("tree-sitter-markdown", "tree-sitter-markdown-inline")),
    Grammar("tree-sitter-org", "SuperFes/tree-sitter-ned-org", "23c476d257db03169831315fdd123fc40278b93c"),
    Grammar("tree-sitter-yaml", "tree-sitter-grammars/tree-sitter-yaml", "v0.7.2"),
    Grammar("tree-sitter-toml", "tree-sitter-grammars/tree-sitter-toml", "v0.7.0"),
    Grammar("tree-sitter-clojure", "sogaiu/tree-sitter-clojure", "v0.0.13"),
    Grammar("tree-sitter-fish", "ram02z/tree-sitter-fish", "3.7.0"),
    Grammar("tree-sitter-xml", "tree-sitter-grammars/tree-sitter-xml", "v0.7.0", subdirs=("xml",)),
    Grammar("tree-sitter-rust", "tree-sitter/tree-sitter-rust", "v0.24.2"),
    Grammar("tree-sitter-go", "tree-sitter/tree-sitter-go", "v0.25.0"),
    Grammar("tree-sitter-c-sharp", "tree-sitter/tree-sitter-c-sharp", "v0.23.5"),
    Grammar("tree-sitter-java", "tree-sitter/tree-sitter-java", "v0.23.5"),
    Grammar("tree-sitter-kotlin", "fwcd/tree-sitter-kotlin", "0.3.8"),
    Grammar("tree-sitter-lua", "tree-sitter-grammars/tree-sitter-lua", "v0.5.0"),
    Grammar("tree-sitter-cmake", "uyha/tree-sitter-cmake", "v0.7.5"),
    Grammar("tree-sitter-diff", "tree-sitter-grammars/tree-sitter-diff", "v0.2.0"),

    Grammar("tree-sitter-sql",
            release_url="https://github.com/DerekStride/tree-sitter-sql/releases/download/v0.3.11/tree-sitter-sql-v0.3.11.tar.gz",
            release_sha256="a97a324eae9c81ed68f6e162b9b33f8911fc6442caa2950e57c498e2460d1387"),

    Grammar("tree-sitter-dockerfile", "camdencheek/tree-sitter-dockerfile", "v0.2.0"),
    Grammar("tree-sitter-make", "tree-sitter-grammars/tree-sitter-make", "v1.1.1"),
    Grammar("tree-sitter-hcl", "tree-sitter-grammars/tree-sitter-hcl", "v1.2.0"),
    Grammar("tree-sitter-nix", "nix-community/tree-sitter-nix", "v0.3.0"),
    Grammar("tree-sitter-ruby", "tree-sitter/tree-sitter-ruby", "v0.23.1"),
    Grammar("tree-sitter-gitcommit", "gbprod/tree-sitter-gitcommit", "v0.5.0"),
    Grammar("tree-sitter-gitrebase", "the-mikedavis/tree-sitter-git-rebase", "v1.0.0"),
    Grammar("tree-sitter-r", "r-lib/tree-sitter-r", "v1.3.0"),
]
# fmt: on

MANIFEST_BY_NAME = {g.name: g for g in MANIFEST}



def log(msg: str) -> None:
    print(f"[vendor-grammars] {msg}", file=sys.stderr)


def fetch_tarball_bytes(url: str, expected_sha256: str = "") -> bytes:
    log(f"fetching {url}")
    with urllib.request.urlopen(url, timeout=60) as resp:
        data = resp.read()
    if expected_sha256:
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected_sha256:
            raise SystemExit(f"sha256 mismatch for {url}: expected {expected_sha256}, got {actual}")
    return data


def extract_tarball(data: bytes) -> Path:
    """Extracts a tarball to a fresh temp dir, returning the single top-level
    directory inside it (GitHub tarballs always wrap in one; release assets
    sometimes don't, hence the fallback)."""
    import tempfile

    tmp = Path(tempfile.mkdtemp(prefix="ned-vendor-"))
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tf:
        tf.extractall(tmp, filter="data")
    entries = [p for p in tmp.iterdir()]
    if len(entries) == 1 and entries[0].is_dir():
        return entries[0]
    return tmp


def fetch_github_ref(org_repo: str, ref: str) -> Path:
    # Works for both tags and bare commit SHAs -- codeload resolves either.
    for url in (
        f"https://codeload.github.com/{org_repo}/tar.gz/refs/tags/{ref}",
        f"https://codeload.github.com/{org_repo}/tar.gz/{ref}",
    ):
        try:
            return extract_tarball(fetch_tarball_bytes(url))
        except Exception as exc:  # noqa: BLE001 -- try the next URL shape
            log(f"  ({url} failed: {exc})")
    raise SystemExit(f"could not fetch {org_repo}@{ref} as a tag or a commit")


def find_relative_escapes(scanner_file: Path) -> list[str]:
    """Returns repo-root-relative directories a scanner's #include "../..."
    lines resolve to outside its own src/ dir (e.g. "common")."""
    text = scanner_file.read_text(errors="replace")
    escapes: set[str] = set()
    for m in re.finditer(r'#include\s*"(\.\./[^"]+)"', text):
        # scanner_file is <subdir>/src/scanner.c; resolve relative to its dir,
        # then take the top-level path component under the grammar's own
        # subdir root (one level above src/) as the directory to vendor.
        resolved = (scanner_file.parent / m.group(1)).resolve()
        try:
            rel = resolved.relative_to(scanner_file.parent.parent.parent)
        except ValueError:
            continue
        escapes.add(rel.parts[0])
    return sorted(escapes)


def copy_grammar_subtree(src_root: Path, extracted_root: Path, dest: Path) -> None:
    # The whole src/ dir, whatever it holds -- parser.c/scanner.c(.cc),
    # grammar.json/node-types.json, tree_sitter/*.h, and any local helper
    # header a hand-written scanner splits out (e.g. tree-sitter-html's own
    # tag.h). A grammar's src/ never carries bindings/examples/test junk --
    # that all lives elsewhere in the repo -- so there's nothing to filter.
    src_dir = src_root / "src"
    if not src_dir.is_dir():
        raise SystemExit(f"no src/ under {src_root}")
    dest_src = dest / "src"
    shutil.copytree(src_dir, dest_src, dirs_exist_ok=True)

    # Not a build input -- kept beside the tables so a grammar bump can be
    # diffed against Source/Languages/<name>/upstream/*.janet by hand. Some
    # repos keep queries/ inside each grammar subdir (markdown/markdown-
    # inline); most keep one shared queries/ at the repo root sitting
    # *beside* the grammar subdir (php, xml, typescript) -- so check both
    # locations.
    local_queries = src_root / "queries"
    if local_queries.is_dir():
        shutil.copytree(local_queries, dest / "queries", dirs_exist_ok=True)
    if extracted_root != src_root:
        root_queries = extracted_root / "queries"
        if root_queries.is_dir():
            root_dest = extracted_root_dest_for(dest, src_root, extracted_root)
            shutil.copytree(root_queries, root_dest / "queries", dirs_exist_ok=True)

    for scanner_name in ("scanner.c", "scanner.cc"):
        scanner_file = src_dir / scanner_name
        if scanner_file.is_file():
            for escape in find_relative_escapes(scanner_file):
                src_escape = extracted_root / escape
                if src_escape.is_dir():
                    shutil.copytree(src_escape, extracted_root_dest_for(dest, src_root, extracted_root) / escape,
                                     dirs_exist_ok=True)


def extracted_root_dest_for(dest: Path, src_root: Path, extracted_root: Path) -> Path:
    """The vendored destination directory that corresponds to extracted_root
    (the fetched repo root), given dest is where src_root (a subdir of it, or
    src_root itself) was vendored to."""
    rel = src_root.relative_to(extracted_root)
    if rel == Path("."):
        return dest
    # dest is where src_root landed; walking up rel.parts from dest reaches
    # the equivalent of extracted_root.
    up = dest
    for _ in rel.parts:
        up = up.parent
    return up


def vendor_grammar(g: Grammar) -> None:
    if g.release_url:
        extracted = extract_tarball(fetch_tarball_bytes(g.release_url, g.release_sha256))
    else:
        extracted = fetch_github_ref(g.org_repo, g.ref)

    dest = DEST_ROOT / g.name
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)

    for sub in g.subdirs:
        src_root = extracted / sub if sub else extracted
        sub_dest = dest / sub if sub else dest
        copy_grammar_subtree(src_root, extracted, sub_dest)

    log(f"vendored {g.name} -> {dest.relative_to(REPO_ROOT)}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("names", nargs="*", help="manifest entries to (re-)vendor")
    parser.add_argument("--all", action="store_true", help="(re-)vendor every manifest entry")
    parser.add_argument("--list", action="store_true", help="print the manifest and exit")
    args = parser.parse_args()

    if args.list:
        for g in MANIFEST:
            src = g.release_url or f"{g.org_repo}@{g.ref}"
            print(f"{g.name:32s} {src}")
        return

    targets = MANIFEST if args.all else [MANIFEST_BY_NAME[n] for n in args.names]
    if not targets:
        parser.error("give one or more names, or --all")

    DEST_ROOT.mkdir(parents=True, exist_ok=True)
    for g in targets:
        vendor_grammar(g)


if __name__ == "__main__":
    main()
