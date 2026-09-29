#!/usr/bin/env python3
"""Generates Source/Editor/Grammar/Compile/UnicodeTables.cpp: every Unicode
fact the grammar compiler uses, fixed at one Unicode version so compiled
tables do not depend on the build machine's libraries -- the general
categories and simple case mappings, the four identifier properties
(\\p{XID_Start}, \\p{XID_Continue}, \\p{ID_Start}, \\p{ID_Continue}) and the
six emoji properties (\\p{Emoji}, \\p{Emoji_Presentation}, \\p{Emoji_Modifier},
\\p{Emoji_Modifier_Base}, \\p{Emoji_Component}, \\p{Extended_Pictographic}).

General categories and simple case mappings are read from UnicodeData.txt,
fetched from unicode.org for the same version as Python's unicodedata. XID_*
come from Python's own identifier rules (str.isidentifier is defined by
exactly XID_Start/XID_Continue); ID_* are XID_* plus the small fixed set of
characters NFKC-closure removes from them (DerivedCoreProperties.txt); the
emoji properties are read from Tools/unicode/emoji-data.txt (the UCD file,
vendored -- refresh it from unicode.org/Public/UCD/latest/ucd/emoji/).

Regenerate:  Tools/gen-unicode-tables.py [--ucd-version X.Y.Z]
"""

from __future__ import annotations

import argparse
import sys
import unicodedata
import urllib.request
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "Source" / "Editor" / "Grammar" / "Compile" / "UnicodeTables.cpp"
EMOJI_DATA = Path(__file__).resolve().parent / "unicode" / "emoji-data.txt"
UNICODE_DATA_URL = "https://www.unicode.org/Public/{version}/ucd/UnicodeData.txt"
# Must match unicode::Category's enumerator order (UnicodeTables.h).
CATEGORIES = ["Lu", "Ll", "Lt", "Lm", "Lo", "Mn", "Mc", "Me", "Nd", "Nl", "No", "Pc", "Pd", "Ps", "Pe",
              "Pi", "Pf", "Po", "Sm", "Sc", "Sk", "So", "Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co", "Cn"]
EMOJI_PROPERTIES = ["Emoji", "Emoji_Presentation", "Emoji_Modifier", "Emoji_Modifier_Base", "Emoji_Component", "Extended_Pictographic"]

# ID_Start minus XID_Start, and ID_Continue minus XID_Continue.
ID_START_EXTRA = [0x037A, 0x0E33, 0x0EB3, 0x309B, 0x309C, *range(0xFC5E, 0xFC64), 0xFDFA, 0xFDFB,
                  0xFE70, 0xFE72, 0xFE74, 0xFE76, 0xFE78, 0xFE7A, 0xFE7C, 0xFE7E, 0xFF9E, 0xFF9F]
ID_CONTINUE_EXTRA = [0x037A, 0x309B, 0x309C, *range(0xFC5E, 0xFC64), 0xFDFA, 0xFDFB,
                     0xFE70, 0xFE72, 0xFE74, 0xFE76, 0xFE78, 0xFE7A, 0xFE7C, 0xFE7E, 0xFF9E, 0xFF9F]


def ranges(predicate) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    start = None
    for cp in range(0x110000):
        if predicate(cp):
            if start is None:
                start = cp
        elif start is not None:
            out.append((start, cp))
            start = None
    if start is not None:
        out.append((start, 0x110000))
    return out


def unicode_data(version: str) -> tuple[list[tuple[int, int]], list[tuple[int, int, int, int]]]:
    """General category runs as (start, category index), each running up to
    the next start, and (c, lower, upper, title) for every character with a
    simple case mapping other than itself."""
    with urllib.request.urlopen(UNICODE_DATA_URL.format(version=version)) as response:
        text = response.read().decode("utf-8")
    category = bytearray([CATEGORIES.index("Cn")]) * 0x110000
    mappings: list[tuple[int, int, int, int]] = []
    range_start = None
    for line in text.splitlines():
        fields = line.split(";")
        cp = int(fields[0], 16)
        value = CATEGORIES.index(fields[2])
        # <..., First>/<..., Last> pairs stand for every codepoint between them.
        if fields[1].endswith(", First>"):
            range_start = cp
            continue
        if fields[1].endswith(", Last>"):
            category[range_start:cp + 1] = bytes([value]) * (cp + 1 - range_start)
            continue
        category[cp] = value
        upper = int(fields[12], 16) if fields[12] else cp
        lower = int(fields[13], 16) if fields[13] else cp
        # An empty titlecase mapping is the uppercase mapping (UAX #44).
        title = int(fields[14], 16) if fields[14] else upper
        if (lower, upper, title) != (cp, cp, cp):
            mappings.append((cp, lower, upper, title))
    runs = [(cp, category[cp]) for cp in range(0x110000) if cp == 0 or category[cp] != category[cp - 1]]
    return runs, mappings


def emoji_tables() -> tuple[str, dict[str, list[tuple[int, int]]]]:
    """The emoji properties as half-open range tables, merged where adjacent."""
    version = "?"
    raw: dict[str, list[tuple[int, int]]] = {name: [] for name in EMOJI_PROPERTIES}
    for line in EMOJI_DATA.read_text().splitlines():
        if line.startswith("# Version:"):
            version = line.split(":", 1)[1].strip()
        body = line.split("#", 1)[0].strip()
        if not body:
            continue
        codepoints, prop = (part.strip() for part in body.split(";", 1))
        if prop not in raw:
            continue
        first, _, last = codepoints.partition("..")
        raw[prop].append((int(first, 16), int(last or first, 16) + 1))
    tables: dict[str, list[tuple[int, int]]] = {}
    for prop, spans in raw.items():
        merged: list[tuple[int, int]] = []
        for start, end in sorted(spans):
            if merged and merged[-1][1] >= start:
                merged[-1] = (merged[-1][0], max(merged[-1][1], end))
            else:
                merged.append((start, end))
        tables[prop] = merged
    return version, tables


def emit(lines: list[str], ctype: str, name: str, values: list[int], per_row: int, count: int) -> None:
    lines.append(f"const {ctype} {name}[] = {{")
    for i in range(0, len(values), per_row):
        lines.append("    " + " ".join(f"0x{v:X}," for v in values[i:i + per_row]))
    lines.append("};")
    lines.append(f"const std::size_t {name}Count = {count};")
    lines.append("")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ucd-version", default=unicodedata.unidata_version,
                        help="UnicodeData.txt version (default: Python's unicodedata, which the XID tables come from)")
    args = parser.parse_args()

    category_runs, case_mappings = unicode_data(args.ucd_version)
    emoji_version, emoji = emoji_tables()
    xid_start = ranges(lambda cp: chr(cp).isidentifier())
    xid_continue = ranges(lambda cp: ("a" + chr(cp)).isidentifier())
    extra_start = set(ID_START_EXTRA)
    extra_continue = set(ID_CONTINUE_EXTRA)
    id_start = ranges(lambda cp: chr(cp).isidentifier() or cp in extra_start)
    id_continue = ranges(lambda cp: ("a" + chr(cp)).isidentifier() or cp in extra_continue)

    lines = [
        "// GENERATED by Tools/gen-unicode-tables.py -- do not edit by hand.",
        f"// UnicodeData.txt {args.ucd_version}; XID/ID Unicode {unicodedata.unidata_version} (Python {sys.version.split()[0]});"
        f" emoji-data.txt {emoji_version}.",
        "",
        '#include "Editor/Grammar/Compile/UnicodeTables.h"',
        "",
        "namespace ned::editor::grammar::compile::unicode {",
        "",
    ]
    emit(lines, "std::uint32_t", "kCategoryRunStart", [start for start, _ in category_runs], 8, len(category_runs))
    emit(lines, "std::uint8_t", "kCategoryRunValue", [value for _, value in category_runs], 16, len(category_runs))
    emit(lines, "std::uint32_t", "kCaseMappings", [v for mapping in case_mappings for v in mapping], 8, len(case_mappings))
    tables = [("kXidStart", xid_start), ("kXidContinue", xid_continue), ("kIdStart", id_start), ("kIdContinue", id_continue)]
    tables += [("k" + prop.replace("_", ""), emoji[prop]) for prop in EMOJI_PROPERTIES]
    for name, table in tables:
        lines.append(f"const std::uint32_t {name}[] = {{")
        row = []
        for start, end in table:
            row.append(f"0x{start:X}, 0x{end:X},")
            if len(row) == 6:
                lines.append("    " + " ".join(row))
                row = []
        if row:
            lines.append("    " + " ".join(row))
        lines.append("};")
        lines.append(f"const std::size_t {name}Count = {len(table)};")
        lines.append("")
    lines.append("} // namespace ned::editor::grammar::compile::unicode")
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT.relative_to(OUT.parents[4])}: {len(category_runs)} category runs, {len(case_mappings)} case mappings, XID_Start {len(xid_start)} ranges, XID_Continue {len(xid_continue)} ranges, Emoji {len(emoji['Emoji'])} ranges")


if __name__ == "__main__":
    main()
