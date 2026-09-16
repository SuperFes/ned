# ROADMAP.md watch-list entry: repeat_statement's body was never
# reindented. `Editor/Grammar/GrammarImprint.cpp`'s static inference
# covers do/if/while/for/function (each closes on its own trailing "end"
# literal, the last member of its SEQ) but repeat_statement's grammar
# rule is SEQ["repeat", body-field, "until", condition-field] -- "until"
# sits second-to-last, followed by a required condition, so the
# opener/closer-are-the-first/last-member check `MatchKeywordPair` does
# never finds it. Confirmed via grammar.json; not a gap the static
# inference tool can close without a backward search it has no other
# need for. Fixed here instead, same "the imprint can't read it,
# hand-author a query" precedent ruby/indents.janet already follows for
# its own do/end gap: capturing the whole node conditioned on "until"
# being a real child keeps the @indent/@dedent pair from firing
# independently of each other.
(repeat_statement "until") @indent
(repeat_statement "until" @dedent)
