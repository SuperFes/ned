#!/usr/bin/env python3
"""Tier 0 trait inference, measured against the hand-written fold corpus.

Phase 1 of Docs/ParsingEngine.md rests on one claim: a delimited body is
derivable from grammar.json with no per-language rules written. This probe is
what makes that claim falsifiable. It infers the `Delimited` trait from each
bundled grammar and scores it against the 55 fold nodes currently hand-written
across Source/Editor/TreeSitter/queries/*-folds.scm.

    python3 Tools/TraitInferenceProbe.py            # score against the corpus
    python3 Tools/TraitInferenceProbe.py --extras   # also list over-inference

Requires a populated build/_deps (the grammars are FetchContent'd).

Result as of 2026-09-11: 55/55 recall, plus 211 nodes beyond the hand-written
list. See this file's own FINDINGS section and Docs/ParsingEngine.md.
"""
import json
import os
import re
import sys
import glob
import collections

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEPS = os.path.join(REPO, "build", "_deps")

# Language -> grammar.json, for the languages that have a hand-written folds.scm.
GRAMMARS = {
    "c": "tree-sitter-c-src/src/grammar.json",
    "cpp": "tree-sitter-cpp-src/src/grammar.json",
    "csharp": "tree-sitter-c-sharp-src/src/grammar.json",
    "go": "tree-sitter-go-src/src/grammar.json",
    "java": "tree-sitter-java-src/src/grammar.json",
    "javascript": "tree-sitter-javascript-src/src/grammar.json",
    "json": "tree-sitter-json-src/src/grammar.json",
    "kotlin": "tree-sitter-kotlin-src/src/grammar.json",
    "python": "tree-sitter-python-src/src/grammar.json",
    "rust": "tree-sitter-rust-src/src/grammar.json",
    "typescript": "tree-sitter-typescript-src-src/typescript/src/grammar.json",
    "clojure": "tree-sitter-clojure-src/src/grammar.json",
}

# Wrappers that carry no structure of their own; tree-sitter sees through them.
WRAPPERS = {"PREC", "PREC_LEFT", "PREC_RIGHT", "PREC_DYNAMIC",
            "FIELD", "ALIAS", "TOKEN", "IMMEDIATE_TOKEN"}
BRACKETS = {"{": "}", "(": ")", "[": "]"}


class Grammar:
    def __init__(self, path):
        raw = json.load(open(path))
        self.rules = raw["rules"]
        self.externals = {e.get("name") for e in raw.get("externals", [])
                          if isinstance(e, dict) and e.get("name")}

    def unwrap(self, rule):
        while isinstance(rule, dict) and rule.get("type") in WRAPPERS and "content" in rule:
            rule = rule["content"]
        return rule

    def is_optional(self, rule):
        """A member that may match nothing, and so may trail a real closer."""
        rule = self.unwrap(rule)
        if not isinstance(rule, dict):
            return False
        if rule.get("type") in ("REPEAT", "BLANK"):
            return True
        if rule.get("type") == "CHOICE":
            return any(isinstance(self.unwrap(m), dict) and self.unwrap(m).get("type") == "BLANK"
                       for m in rule["members"])
        return False

    def flatten(self, rule, depth=0, seen=frozenset()):
        """A SEQ's members with hidden (_-prefixed) rules inlined.

        Load-bearing: tree-sitter inlines hidden rules rather than making them
        nodes, so a grammar is free to put a node's real delimiters inside one.
        Clojure does exactly that -- list_lit is SEQ[REPEAT(_metadata_lit),
        _bare_list_lit], and the parens live in _bare_list_lit. Without
        inlining, all six Clojure fold nodes are invisible.
        """
        rule = self.unwrap(rule)
        if not (isinstance(rule, dict) and rule.get("type") == "SEQ"):
            return None
        out = []
        for member in rule["members"]:
            member = self.unwrap(member)
            if (isinstance(member, dict) and member.get("type") == "SYMBOL"
                    and member["name"].startswith("_") and member["name"] in self.rules
                    and depth < 4 and member["name"] not in seen):
                inner = self.flatten(self.rules[member["name"]], depth + 1, seen | {member["name"]})
                if inner:
                    out.extend(inner)
                    continue
            out.append(member)
        return out

    def literal(self, member):
        if isinstance(member, dict) and member.get("type") == "STRING":
            return member.get("value")
        return None

    def is_external(self, member):
        return (isinstance(member, dict) and member.get("type") == "SYMBOL"
                and member["name"] in self.externals)

    def delimited(self, rule):
        """The Tier 0 `Delimited` trait: 'bracket', 'indent', or None.

        NOT 'foldable'. A parenthesized_expression and an argument_list are
        both genuinely delimited and neither is something to fold -- that is a
        Tier 1 policy sitting on top of this fact, not a failure of it.
        """
        members = self.flatten(rule)
        if not members:
            return None

        # A real closer can be followed by optional members -- JavaScript's
        # statement_block is SEQ['{', REPEAT(statement), '}', <optional>].
        core = list(members)
        while len(core) > 1 and self.is_optional(core[-1]):
            core.pop()
        if len(core) < 2:
            core = list(members)

        closer = self.literal(core[-1])
        if closer in BRACKETS.values():
            opener = next(o for o, c in BRACKETS.items() if c == closer)
            if any(self.literal(m) == opener for m in core[:-1]):
                return "bracket"

        # Indentation languages close with an external token and have no
        # opener of their own: Python's block is SEQ[REPEAT(_statement),
        # _dedent], with the _indent consumed by the parent.
        if core and self.is_external(core[-1]):
            return "indent"
        return None

    def infer(self):
        return {name for name, rule in self.rules.items()
                if not name.startswith("_") and self.delimited(rule)}


def hand_written_folds():
    """The @fold node names currently written by hand, as ground truth."""
    truth = {}
    pattern = os.path.join(REPO, "Source", "Editor", "TreeSitter", "queries", "*-folds.scm")
    for path in sorted(glob.glob(pattern)):
        lang = os.path.basename(path)[: -len("-folds.scm")]
        text = re.sub(r";[^\n]*", "", open(path).read())
        nodes = set()
        for m in re.finditer(r"\(\s*([a-z_][a-z_0-9]*)\b[^()]*@fold", text):
            nodes.add(m.group(1))
        for m in re.finditer(r"\(\s*([a-z_][a-z_0-9]*)\s*\)\s*@fold", text):
            nodes.add(m.group(1))
        truth[lang] = nodes
    return truth


def main():
    show_extras = "--extras" in sys.argv
    if not os.path.isdir(DEPS):
        sys.exit(f"no {DEPS} -- configure and build first (grammars are FetchContent'd)")

    truth = hand_written_folds()
    hits = misses = 0
    extras = {}

    print(f"{'language':12s} {'recall':>8s}   missed")
    print("-" * 58)
    for lang, expected in sorted(truth.items()):
        rel = GRAMMARS.get(lang)
        if not rel or not os.path.exists(os.path.join(DEPS, rel)):
            print(f"  {lang:10s} {'skip':>8s}   (no grammar.json)")
            continue
        inferred = Grammar(os.path.join(DEPS, rel)).infer()
        hit, miss = expected & inferred, expected - inferred
        hits += len(hit)
        misses += len(miss)
        extras[lang] = sorted(inferred - expected)
        print(f"  {lang:10s} {len(hit):3d}/{len(expected):<4d}   {sorted(miss) if miss else '-'}")

    total = hits + misses
    print("-" * 58)
    print(f"  recall {hits}/{total} = {100 * hits / total:.0f}%")
    over = sum(len(v) for v in extras.values())
    print(f"  {over} nodes inferred beyond the hand-written list "
          f"(Delimited is not Foldable -- see delimited()'s docstring)")

    if show_extras:
        counts = collections.Counter(n for v in extras.values() for n in v)
        print("\n  most common over-inferences across languages:")
        for name, count in counts.most_common(15):
            print(f"    {count}x  {name}")

    return 0 if misses == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
