# ned-authored (cmake/gitrebase precedent): upstream's highlights.scm wraps
# each multi-sibling group in an extra layer of parens before attaching its
# predicate -- `(( (command) (label) (message)? ) (:match? ...))` -- which is
# one level deeper than QueryMatcher's census-measured scope allows ("a
# multi-pattern group is only supported at the top level"). Flattened here so
# the predicate sits inside the same top-level group as the siblings it
# constrains; every capture/predicate mapping otherwise matches upstream
# exactly. See the ROADMAP watch-list entry for the general shape.

# a rough translation:
# * constant.builtin - git hash
# * constant - a git label
# * keyword - command that acts on commits commits
# * function - command that acts only on labels
# * comment - discarded commentary on a command, has no effect on the rebase
# * string - text used in the rebase operation
# * operator - a 'switch' (used in fixup and merge), either -c or -C at time of writing

((command) @keyword
 (label) @constant.builtin
 (message)? @comment
 (:match? @keyword "^(p|pick|r|reword|e|edit|s|squash|d|drop)$"))

((command) @function
 (label) @constant
 (message)? @comment
 (:match? @function "^(l|label|t|reset)$"))

((command) @keyword
 (:match? @keyword "^(x|exec|b|break)$"))

((command) @attribute
 (label) @constant.builtin
 (message)? @comment
 (:match? @attribute "^(f|fixup)$"))

((command) @keyword
 (label) @constant.builtin
 (label) @constant
 (message) @string
 (:match? @keyword "^(m|merge)$"))

(option) @operator

(comment) @comment
