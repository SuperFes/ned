# ned-authored: mirrors upstream's mappings in supported constructs;
# @diff.plus/@diff.minus/@diff.delta resolve to ned's own
# DiffAdded/DiffRemoved/DiffChanged classes. The forward:/reverse: binary
# hunk patterns below were dropped for a time -- upstream's highlights.scm
# ends with top-level field-prefixed patterns, a construct QueryMatcher's
# census-measured scope excluded -- see the ROADMAP watch-list entry;
# restored once the matcher gained support.

(comment) @comment

[
  (addition)
  (new_file)
] @diff.plus

[
  (deletion)
  (old_file)
] @diff.minus

(change) @diff.delta

(commit) @constant

(location) @attribute

(command
  "diff" @function
  (argument) @variable.parameter)

(filename) @string.special.path

(special) @string.special

(mode) @number

(index
  "index" @keyword)

(similarity
  (score) @number)

(dissimilarity
  (score) @number)

[
  (binary_change)
  (similarity)
  (dissimilarity)
  (file_change)
] @label

forward: (binary_hunk
  (payload) @diff.plus)

reverse: (binary_hunk
  (payload) @diff.minus)
