# smart-indentation follow-up. See c-indents.scm's own header comment for the
# general convention and for what the delimiter imprint contributes without a
# capture. Checked against tree-sitter-yaml's own node-types.json plus a real
# parse dump.
#
# YAML's block_mapping/block_sequence have no closing delimiter of any kind
# (pure indentation-defined structure) -- same shape as Python's own "block",
# and the imprint reports them the same way: an indentation body counts when
# it has a header above it, shallower (Editor/ImprintIndent.h). That is what
# keeps the document's ROOT mapping or sequence from counting as a level of
# its own -- YAML uses the exact same node type for the root as for a nested
# body, and the old query needed #has-ancestor? to tell them apart; the
# imprint reads it off the text instead. A nested block_mapping is therefore
# fully covered and is not restated here.
#
# block_sequence is different, and this file exists for it. YAML allows a
# sequence under a key to be written either indented or flush with the key:
#
#     key:          key:
#       - a         - a
#       - b         - b
#
# Both are valid, and the imprint -- reading indentation off the text --
# honours whichever the author wrote, which is right for a reindent of an
# existing file but leaves TAB on a flush `- a` with nothing to do. This
# capture states the convention: a sequence nested under a mapping or
# another sequence indents one level (yamllint's `indent-sequences: true`),
# so the interactive path always has an answer. That is a layout convention
# rather than a structural fact, which is exactly what an indents.scm is for
# now. Multi-line flow collections ("{...}"/"[...]" spanning several lines)
# are ordinary bracket bodies and come from the imprint.
((block_sequence) @indent (:has-ancestor? @indent block_mapping))
((block_sequence) @indent (:has-ancestor? @indent block_sequence))
