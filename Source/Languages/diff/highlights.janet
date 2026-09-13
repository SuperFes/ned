# ned-authored: upstream's highlights.scm ends with top-level
# field-prefixed patterns (forward:/reverse: binary hunks), a construct
# QueryMatcher's census-measured scope excludes -- see the ROADMAP
# watch-list entry. Everything else here mirrors upstream's mappings in
# supported constructs; @diff.plus/@diff.minus/@diff.delta resolve to
# ned's own DiffAdded/DiffRemoved/DiffChanged classes.

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
