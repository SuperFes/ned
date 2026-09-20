#!/usr/bin/env python3
"""Byte-exact regression gate for the grammar table generator.

    Tools/grammar-tables.py check [--compile-with build/Source/ned]
    Tools/grammar-tables.py record [--compile-with build/Source/ned]

Every bundled grammar compiles to one `tables` file, and the generator is
deterministic: the same grammar.janet through the same generator produces the
same bytes. That makes a hash per language a complete gate on generator
changes -- a refactor that is meant to be behaviour-preserving either
reproduces all of them or it does not.

`check` hashes each language's compiled tables and compares them against
Tests/GrammarTables.sha256; `record` rewrites that manifest. The manifest is
plain `sha256sum` format, so `cd build/share/ned && sha256sum -c
../../../Tests/GrammarTables.sha256` verifies it without this script.

By default both read the tables the build already produced under
build/share/ned/languages. `--compile-with <ned>` instead runs that binary's
--compile-language over every grammar into a temporary directory, which
leaves the build tree alone and reports per-language compile times -- the
generator's benchmark and its correctness gate being the same pass.
"""

import argparse
import concurrent.futures
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_TABLES = REPO / "build" / "share" / "ned" / "languages"
DEFAULT_MANIFEST = REPO / "Tests" / "GrammarTables.sha256"
GRAMMARS = REPO / "Source" / "Languages"

MANIFEST_HEADER = """\
# sha256 of every bundled grammar's compiled `tables`, as
# Tools/grammar-tables.py records it. A generator change that is meant to
# preserve behaviour must reproduce this file exactly; a grammar change or a
# table-format change is expected to move the lines it touches, and is
# re-recorded deliberately (Tools/grammar-tables.py record).
"""


def languages(only=None):
    """The bundled languages, in manifest order: every directory under
    Source/Languages that has a grammar.janet. Taken from the source tree
    rather than the build tree, which keeps stale copied-but-deleted
    directories out of the comparison."""
    names = sorted(d.name for d in GRAMMARS.iterdir() if (d / "grammar.janet").is_file())
    if only is None:
        return names
    missing = sorted(set(only) - set(names))
    if missing:
        sys.exit(f"grammar-tables: no such bundled language: {', '.join(missing)}")
    return [name for name in names if name in only]


def digest(path):
    sha = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            sha.update(chunk)
    return sha.hexdigest()


def compile_all(ned, out_dir, names, jobs):
    """Compile each grammar with `ned --compile-language` into out_dir,
    laid out as the data tree lays it out. Returns elapsed seconds per
    language; a failed compile is fatal, with the generator's own stderr."""

    def one(name):
        target = out_dir / name / "tables"
        target.parent.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        done = subprocess.run([str(ned), "--compile-language", str(GRAMMARS / name), "-o", str(target)],
                              capture_output=True, text=True)
        return name, time.monotonic() - started, done.returncode, done.stderr

    elapsed = {}
    failed = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for name, seconds, code, stderr in pool.map(one, names):
            elapsed[name] = seconds
            if code != 0:
                failed.append((name, stderr.strip()))
    if failed:
        for name, stderr in failed:
            print(f"{name}: compile failed\n{stderr}", file=sys.stderr)
        sys.exit(f"grammar-tables: {len(failed)} grammar(s) failed to compile")
    return elapsed


def report_times(elapsed, jobs):
    """Per-language wall time, slowest first, and the totals that say whether
    a generator change moved the number that matters."""
    total = sum(elapsed.values())
    print(f"\ncompiled {len(elapsed)} grammars in {total:.1f} CPU-s across {jobs} jobs")
    print("slowest:")
    for name, seconds in sorted(elapsed.items(), key=lambda item: -item[1])[:10]:
        print(f"  {seconds:8.2f}s  {name}")


def read_manifest(path):
    if not path.is_file():
        sys.exit(f"grammar-tables: no manifest at {path} -- record one first")
    recorded = {}
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        try:
            sha, name = line.split(maxsplit=1)
        except ValueError:
            sys.exit(f"{path}:{number}: not a sha256sum line")
        recorded[Path(name.strip()).parts[0]] = sha
    return recorded


def write_manifest(path, hashes):
    lines = [f"{hashes[name]}  {name}/tables" for name in sorted(hashes)]
    path.write_text(MANIFEST_HEADER + "\n".join(lines) + "\n")


def collect(tables, names):
    """language -> sha256 of its compiled tables. A language the manifest
    covers but the tables directory lacks is reported as such rather than
    silently skipped."""
    hashes = {}
    absent = []
    for name in names:
        path = tables / name / "tables"
        if path.is_file():
            hashes[name] = digest(path)
        else:
            absent.append(name)
    return hashes, absent


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("check", "record"))
    parser.add_argument("--tables", type=Path, default=DEFAULT_TABLES,
                        help="directory of <language>/tables (default: the build tree's share/ned/languages)")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--compile-with", type=Path, metavar="NED",
                        help="compile every grammar with this ned binary into a temporary directory first")
    parser.add_argument("--jobs", "-j", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--languages", metavar="A,B", help="restrict to these languages")
    args = parser.parse_args()

    only = args.languages.split(",") if args.languages else None
    names = languages(only)

    scratch = None
    elapsed = None
    try:
        if args.compile_with:
            if not os.access(args.compile_with, os.X_OK):
                sys.exit(f"grammar-tables: {args.compile_with} is not an executable")
            scratch = Path(tempfile.mkdtemp(prefix="ned-grammar-tables-"))
            elapsed = compile_all(args.compile_with, scratch, names, args.jobs)
            tables = scratch
        else:
            tables = args.tables
            if not tables.is_dir():
                sys.exit(f"grammar-tables: no tables directory at {tables} -- build first, or pass --compile-with")

        hashes, absent = collect(tables, names)

        if args.command == "record":
            if absent:
                sys.exit(f"grammar-tables: refusing to record, {len(absent)} language(s) have no tables: {', '.join(absent)}")
            if only is not None:
                # A narrowed record re-records only what it compiled; the
                # rest of the manifest stands, so changing one grammar does
                # not silently drop the other hundred.
                merged = read_manifest(args.manifest)
                merged.update(hashes)
                hashes = merged
            write_manifest(args.manifest, hashes)
            print(f"recorded {len(hashes)} languages to {args.manifest}")
            if elapsed:
                report_times(elapsed, args.jobs)
            return 0

        if elapsed:
            report_times(elapsed, args.jobs)

        recorded = read_manifest(args.manifest)
        differing = sorted(name for name, sha in hashes.items() if name in recorded and recorded[name] != sha)
        unrecorded = sorted(set(hashes) - set(recorded))
        # Only when checking the full set does a manifest line without a
        # language mean anything; --languages narrows both sides.
        stale = sorted(set(recorded) - set(names)) if only is None else []

        # Diagnostics and the verdict go to stderr; flush first so a piped
        # stdout's block buffering can't land the timings after them.
        sys.stdout.flush()
        for name in differing:
            print(f"{name}: tables differ -- recorded {recorded[name][:16]}, got {hashes[name][:16]}", file=sys.stderr)
        for name in absent:
            print(f"{name}: no compiled tables at {tables / name / 'tables'}", file=sys.stderr)
        for name in unrecorded:
            print(f"{name}: compiled but not in the manifest", file=sys.stderr)
        for name in stale:
            print(f"{name}: in the manifest but no longer a bundled language", file=sys.stderr)

        if differing or absent or unrecorded or stale:
            print(f"\ngrammar-tables: {len(differing)} of {len(names)} languages changed. If that is intended "
                  f"(a grammar or the table format moved), re-record with:\n"
                  f"    Tools/grammar-tables.py record\n", file=sys.stderr)
            return 1
        print(f"grammar-tables: {len(hashes)} languages match {args.manifest}")
        return 0
    finally:
        if scratch:
            shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
