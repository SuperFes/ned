#!/usr/bin/env python3
"""Port a tree-sitter grammar's src/scanner.c to ned's scanner interface.

    Tools/port-scanner.py <scanner.c> <language> <out.cpp> [--repo URL]

Does the mechanical half: the tree-sitter headers become Editor/Parse/
Scanner.h (+ ScannerSupport.h when the array vocabulary is used), TSLexer/
TSSymbol and the lexer's member names become ned's, the five entry points
become static Create/Destroy/Scan/Serialize/Deserialize behind a
ScannerVTable (registered in Scanners.cpp), and the file is wrapped in the language's namespace. A
`#include "../../common/scanner.h"` is inlined. What remains is C-to-C++
friction (void* conversions, out-of-order designated initializers, goto over
an initialization), which the compiler names and a person fixes.
"""

import argparse
import re
import sys
from pathlib import Path

ENTRY_POINTS = {
    "create": "Create",
    "destroy": "Destroy",
    "scan": "Scan",
    "serialize": "Serialize",
    "deserialize": "Deserialize",
}

MEMBER_RENAMES = {
    "result_symbol": "resultSymbol",
    "mark_end": "markEnd",
    "get_column": "getColumn",
    "is_at_included_range_start": "isAtIncludedRangeStart",
}

IDENTIFIER_RENAMES = {
    "TSLexer": "Lexer",
    "TSSymbol": "Symbol",
    "TREE_SITTER_SERIALIZATION_BUFFER_SIZE": "kSerializationBufferSize",
    "ts_builtin_sym_end": "kEndSymbol",
    "ts_malloc": "scanner_malloc",
    "ts_calloc": "scanner_calloc",
    "ts_realloc": "scanner_realloc",
    "ts_free": "free",
    "malloc": "scanner_malloc",
    "calloc": "scanner_calloc",
    "realloc": "scanner_realloc",
}

ALWAYS_INCLUDED = ["#include <cstddef>", "#include <cstdint>", "#include <cstdlib>", "#include <cstring>"]

C_TO_CXX_HEADERS = {
    "assert.h": "cassert", "ctype.h": "cctype", "inttypes.h": "cinttypes", "limits.h": "climits",
    "stdbool.h": None, "stddef.h": "cstddef", "stdint.h": "cstdint", "stdio.h": "cstdio",
    "stdlib.h": "cstdlib", "string.h": "cstring", "wctype.h": "cwctype", "math.h": "cmath",
}


def inline_local_includes(source: str, directory: Path) -> str:
    """A `#include "file"` that names a file beside the scanner (a shared
    common/scanner.h, html's tag.h, yaml's schema tables) is inlined."""
    def replace(match: re.Match) -> str:
        if match.group(1).startswith("tree_sitter/"):
            return match.group(0)
        local = (directory / match.group(1)).resolve()
        if not local.is_file():
            return match.group(0)
        return f"// --- {local.name} (from the same grammar) ---\n" + inline_local_includes(local.read_text(), local.parent) + "\n"
    return re.sub(r'#include "([^"]+)"\n', replace, source)


def payload_through_voidptr(body: str) -> str:
    """The four entry points that take the opaque payload receive it as a
    void* and hand it around as the scanner's own type; the parameter is
    rebound through VoidPtr so that C conversion keeps compiling."""
    pattern = re.compile(r"(static (?:void|bool|unsigned) (?:Destroy|Scan|Serialize|Deserialize)\(\s*void\s*\*\s*)(\w+)(\b[^{]*\{)")
    def replace(match: re.Match) -> str:
        name = match.group(2)
        return f"{match.group(1)}{name}_{match.group(3)}\n    VoidPtr {name}{{{name}_}};"
    return pattern.sub(replace, body)


def port(source: str, language: str, repo: str) -> str:
    includes: list[str] = []
    export = re.search(r"\btree_sitter_(\w+)_external_scanner_create\b", source)
    if export is None:
        sys.exit("no tree_sitter_<name>_external_scanner_create in the source")
    export_name = export.group(1)

    body_lines = []
    for line in source.splitlines():
        header = re.match(r'\s*#\s*include\s*[<"]([^>"]+)[>"]', line)
        if header:
            name = header.group(1)
            if name.startswith("tree_sitter/"):
                continue
            if name in C_TO_CXX_HEADERS:
                cxx = C_TO_CXX_HEADERS[name]
                if cxx is not None:
                    includes.append(f"#include <{cxx}>")
            else:
                includes.append(line.strip())
            continue
        body_lines.append(line)
    body = "\n".join(body_lines)

    for old, new in IDENTIFIER_RENAMES.items():
        body = re.sub(rf"\b{old}\b", new, body)
    for old, new in MEMBER_RENAMES.items():
        body = re.sub(rf"(->|\.)\s*{old}\b", rf"\1{new}", body)
    for suffix, name in ENTRY_POINTS.items():
        body = re.sub(rf"\btree_sitter_{export_name}_external_scanner_{suffix}\b", name, body)
        body = re.sub(rf"^(\s*)(void\s*\*|void|bool|unsigned)(\s+){name}\(", rf"\1static \2\3{name}(", body, flags=re.M)
    body = payload_through_voidptr(body)
    body = re.sub(r"\n{3,}", "\n\n", body).strip("\n")

    ns = f"ned::editor::languages::scanners::{language.replace('-', '_')}"
    out = []
    out.append(f"// The {language} external scanner, ported from {repo} (src/scanner.c, MIT")
    out.append("// license) to ned's scanner interface. The algorithm and its state are the")
    out.append("// upstream grammar's; only the vocabulary is ned's.")
    out.append("")
    out.append('#include "Editor/Parse/Scanner.h"')
    out.append('#include "Editor/Parse/ScannerSupport.h"')
    out.append("")
    for include in sorted(set(includes) | set(ALWAYS_INCLUDED)):
        out.append(include)
    out.append("")
    out.append(f"namespace {ns} {{")
    out.append("")
    out.append("using namespace ned::editor::parse::scanner;")
    out.append("")
    out.append(body)
    out.append("")
    out.append("extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};")
    out.append("")
    out.append(f"}} // namespace {ns}")
    out.append("")
    return "\n".join(out)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("scanner")
    parser.add_argument("language")
    parser.add_argument("output")
    parser.add_argument("--repo", default="its upstream repository")
    args = parser.parse_args()
    path = Path(args.scanner)
    source = inline_local_includes(path.read_text(), path.parent)
    Path(args.output).write_text(port(source, args.language, args.repo))


if __name__ == "__main__":
    main()
