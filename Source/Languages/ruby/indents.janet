# indent-engine follow-up (ROADMAP.md's own watch-list entry): method/
# singleton_method/while/until bodies are never reindented -- confirmed
# live via a baseline `ned --format` pass with no format.janet rules
# configured at all. `Editor/Grammar/GrammarImprint.cpp`'s static
# inference over grammar.json can't find any of these four on its own,
# for two DIFFERENT reasons, neither of which the generated
# `Editor/ImprintTables.cpp` can be hand-patched to work around (that
# file is fully regenerated from live inference on every
# `NED_BLESS_IMPRINT=1` run, so a hand-added row would be silently
# discarded the next time anyone regenerates it):
#
# - `method`/`singleton_method`: `_method_rest`'s own grammar rule is
#   `SEQ["def", name-field, CHOICE[params-branch, endless-body-branch]]`
#   -- the closing "end" is buried inside a CHOICE that sits as an
#   ORDINARY mid-sequence member, not the rule's own top-level type, so
#   the inference tool's flattener (which only descends into a CHOICE at
#   a rule's own outermost position) never reaches it. Confirmed via a
#   real compiled parse: the runtime tree fully inlines this regardless
#   of the CHOICE nesting (unlike the static flattener), so `method` has
#   real, DIRECT anonymous "def"/"end" children at parse time.
# - `while`/`until`: the "do" wrapper node IS already in `ImprintTables.
#   cpp`'s `kRuby[]` as `Keyword("do", "end")`, correctly inferred and
#   correctly used for the EXPLICIT `while x do ... end` form. But the
#   far more common, idiomatic style (`while x\n ... end`, no literal
#   "do" written at all) produces the SAME "do"-typed wrapper node with
#   NO anonymous "do" child at all -- confirmed via a real parse dump.
#   `Editor/ImprintBracket.cpp`'s `DelimitersOf` requires the literal
#   opener token to be PHYSICALLY PRESENT among the node's children
#   before it will report a delimiter pair at all, so the imprint
#   contributes nothing for this instance -- a runtime limitation of
#   `DelimiterKind::Keyword` itself (there is no "closer-only, opener
#   sometimes elided" variant anywhere in this model), not a gap the
#   static inference tool could close.
#
# Fixed here instead, the same "the imprint can't read it, hand-author a
# query" precedent every other bundled language's own indents.janet
# already follows for its own gaps (Python's if/elif/else, bash's own
# elif_clause/else_clause). `(do)` needs no discriminator between the
# explicit/elided forms -- capturing the WHOLE node (rather than
# anchoring on the literal "do" token the way the imprint does) covers
# both uniformly, verified live via a real `ned --format` pass on both
# shapes side by side. Each pair below is deliberately written as ONE
# structural condition ("end" must be a real child) shared by both the
# @indent and @dedent pattern, rather than an unconditional `(method)
# @indent` -- Ruby's own "endless method" (`def f = 1`) has NO "end"
# token at all, and an unconditional @indent would open a container that
# nothing ever dedents, over-indenting everything after it in the file.
# Requiring "end" as a (structural, unnamed) child of the SAME pattern
# keeps the two conditions identical, so they can never fire independently
# of each other.
(method "end") @indent
(method "end" @dedent)
(singleton_method "end") @indent
(singleton_method "end" @dedent)
(do "end") @indent
(do "end" @dedent)
