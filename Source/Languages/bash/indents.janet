# smart-indentation follow-up. See c-indents.scm's own header comment for the
# general convention and for what the delimiter imprint contributes without a
# capture. Checked against tree-sitter-bash's own node-types.json plus a real
# parse dump.
#
# Bash's bracket bodies (compound_statement, subshell, array, subscript,
# ...) AND its keyword-delimited ones (`if ... fi`, `do ... done`, `case ...
# esac`) indent from the imprint: a matched keyword pair around a list is a
# delimited body the same as a bracket pair (Editor/Imprint.h's
# DelimiterKind::Keyword), and `fi` dedents the way `}` does. What structure
# cannot say is where a clause's own HEADER goes: elif_clause/else_clause are
# siblings of the commands they follow, not bodies (`elif ... then` is a
# keyword pair around a single condition -- a phrase, which inference
# declines), and need to align back to the `if` line. Mirrors Python's own
# elif_clause/else_clause exactly.
(elif_clause) @dedent
(else_clause) @dedent
