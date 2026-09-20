#!/usr/bin/env python3
"""Bounds gate for the bundled external scanners' `valid_symbols` reads.

    Tools/scanner-externals.py check [--language nu ...] [--verbose]

The engine hands a scanner one row of the grammar's external-lex-state table
(Editor/Parse/Parser.cpp, `ExternalScannerScan`), and that row is exactly as
wide as the grammar's `:externals` list. A scanner is a hand-port of the
upstream grammar's scanner.c, so its `TokenType` enum and that list are two
copies of the same vocabulary: if the port keeps a token the converted
grammar.janet dropped -- an `error_sentinel` is the usual one -- every scan
reads past the row, and past the whole table on its last row.

That is a static property of the source, so this checks it statically: for
each bundled scanner, every `valid_symbols[NAME]` whose NAME is an enum
constant must index inside the grammar's `:externals`. Names that aren't enum
constants (a variable holding a token, a table lookup) can't be checked here
and are listed by --verbose.

Enum size is deliberately *not* compared against the list: several scanners
carry a trailing non-token sentinel (ruby's `NONE`, crystal's) that is state,
never an index.
"""

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SCANNERS = REPO / "Source" / "Editor" / "Languages" / "Scanners"
REGISTRY = SCANNERS / "Scanners.cpp"
GRAMMARS = REPO / "Source" / "Languages"


def registered_scanners():
    """The (language, namespace) pairs Scanners.cpp registers, and the file
    each namespace is defined in."""
    pairs = re.findall(r'X\("([^"]+)",\s*(\w+)\)', REGISTRY.read_text())
    by_namespace = {}
    for path in sorted(SCANNERS.glob("*.cpp")):
        found = re.search(r"namespace ned::editor::languages::scanners::(\w+)\s*\{", path.read_text())
        if found:
            by_namespace[found.group(1)] = path
    out = []
    for language, namespace in pairs:
        path = by_namespace.get(namespace)
        if path is None:
            sys.exit(f"scanner-externals: no file defines namespace scanners::{namespace} ({language})")
        out.append((language, path))
    return out


def strip_comments(text):
    """Comments out, preprocessor lines out. Block comments go first: a
    commented-out enumerator can hold a line comment inside a block one
    (gdscript's `/* COLON, // ... */`), and stripping line comments first
    would leave the block unterminated."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r"(?m)^\s*#[^\n]*$", "", text)


def enum_constants(source):
    """Every enum constant in `source` as name -> value, for the enums whose
    values are plain counted constants. An enumerator with an
    initializer that isn't an integer literal (a bit-flag enum, an alias)
    stops that enum being counted from there on."""
    out = {}
    for match in re.finditer(r"enum\s+(?:class\s+)?(\w*)\s*(?::[^{]*)?\{([^{}]*?)\}\s*(\w*)\s*;", source, re.S):
        body = strip_comments(match.group(2))
        value = 0
        for item in body.split(","):
            item = item.strip()
            if not item:
                continue
            if "=" in item:
                name, literal = (part.strip() for part in item.split("=", 1))
                try:
                    value = int(literal, 0)
                except ValueError:
                    break
            else:
                name = item
            if re.fullmatch(r"[A-Za-z_]\w*", name):
                out.setdefault(name, value)
            value += 1
    return out


def externals(language):
    """The names in a grammar's `:externals` list, in order. The scan is
    string-aware: `"]"` is a perfectly ordinary external token."""
    path = GRAMMARS / language / "grammar.janet"
    if not path.is_file():
        return None
    text = path.read_text()
    start = text.find(":externals")
    if start < 0:
        return []
    open_bracket = text.index("[", start)
    depth, index, in_string, escaped = 0, open_bracket, False, False
    while index < len(text):
        char = text[index]
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
        elif char == '"':
            in_string = True
        elif char == "[":
            depth += 1
        elif char == "]":
            depth -= 1
            if depth == 0:
                break
        index += 1
    inner = text[open_bracket + 1 : index]

    names, current, depth, in_string, escaped = [], "", 0, False, False
    for char in inner:
        if in_string:
            current += char
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
            current += char
        elif char.isspace() and depth == 0:
            if current.strip():
                names.append(current.strip())
            current = ""
        else:
            if char in "([{":
                depth += 1
            elif char in ")]}":
                depth -= 1
            current += char
    if current.strip():
        names.append(current.strip())
    return names


def check(only=None, verbose=False):
    failures, unresolved = [], []
    checked = 0
    for language, path in registered_scanners():
        if only and language not in only:
            continue
        declared = externals(language)
        if declared is None:
            failures.append(f"{language}: registers a scanner but has no Source/Languages/{language}/grammar.janet")
            continue
        source = strip_comments(path.read_text())
        constants = enum_constants(source)
        used = sorted(set(re.findall(r"valid_symbols\s*\[\s*([A-Za-z_]\w*)\s*\]", source)))
        checked += 1
        for name in used:
            index = constants.get(name)
            if index is None:
                unresolved.append(f"{language}: valid_symbols[{name}] -- {name} is not an enum constant")
            elif index >= len(declared):
                failures.append(
                    f"{language}: {path.name} reads valid_symbols[{name}] (index {index}) but "
                    f"{language}/grammar.janet declares {len(declared)} externals -- "
                    f"the row is {len(declared)} wide, so this reads past it"
                )

    if verbose:
        for line in unresolved:
            print(f"note: {line}")
    if failures:
        for line in failures:
            print(f"scanner-externals: {line}", file=sys.stderr)
        print(
            f"\n{len(failures)} out-of-bounds read(s). Either the grammar's :externals lost a "
            f"token the port still reads (add it back) or the port reads one the grammar never had.",
            file=sys.stderr,
        )
        return 1
    print(f"scanner-externals: {checked} scanners index inside their grammar's :externals")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["check"], nargs="?", default="check")
    parser.add_argument("--language", action="append", help="only this language (repeatable)")
    parser.add_argument("--verbose", action="store_true", help="also list the reads this can't resolve statically")
    args = parser.parse_args()
    return check(only=args.language, verbose=args.verbose)


if __name__ == "__main__":
    sys.exit(main())
