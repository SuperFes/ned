#; Upstream's queries/highlights.scm (sourcegraph/tree-sitter-jsonnet) with
#; two constructs ned's query engine does not carry removed, which is why this
#; is ned's file and not a vendored upstream one:
#;
#;   - the `#is? @x parameter/function/var` patterns, nvim directives that ask
#;     locals.scm what a name resolved to. An unrecognized predicate is inert
#;     here, so keeping them would have painted every identifier a reference.
#;   - `"}"? @text.danger` inside `implicit_plus` -- a quantifier on an
#;     anonymous token, outside the measured query surface (QueryMatcherTest's
#;     construct census).
#;
#; Everything else is upstream's, unmodified.

(id) @variable
(comment) @comment

# Literals
(null) @constant.builtin
(string) @string
(number) @number
[
  (true)
  (false)
] @boolean

# Keywords
"for" @repeat
"in" @keyword.operator
"function" @keyword.function
[
  "if"
  "then"
  "else"
] @conditional
[
  (local)
  (tailstrict)
  "function"
  "assert"
  "error"
] @keyword

[
  (dollar)
  (self)
  (super)
] @variable.builtin
((id) @variable.builtin
 (:eq? @variable.builtin "std"))

# Operators
[
  (multiplicative)
  (additive)
  (bitshift)
  (comparison)
  (equality)
  (bitand)
  (bitxor)
  (bitor)
  (and)
  (or)
  (unaryop)
] @operator

# Punctuation
[
  "["
  "]"
  "{"
  "}"
  "("
  ")"
] @punctuation.bracket

[
  "."
  ","
  ";"
  ":"
] @punctuation.delimiter

[
  "::"
  ":::"
] @punctuation.special

(field
  (fieldname) "+" @punctuation.special)

# Imports
[
  (import)
  (importstr)
] @include

# References

# References do not apply to static field IDs
# Workaround for `(#is-not? local)` not supported
(fieldname (id) @field)
(fieldname (string
             (string_start) @text.strong
             (string_content) @field
             (string_end) @text.strong
           ))

# Functions
(field
  function: (fieldname (id) @function))
(field
  function: (fieldname
              (string
                (string_start) @text.strong
                (string_content) @function
                (string_end) @text.strong
              )))
(param
  identifier: (id) @parameter)

(bind (id) @define)
(bind function: (id) @function)

# Function call
(functioncall
  (fieldaccess
    last: (id) @function.call
  )?
  (fieldaccess_super
    (id) @function.call
  )?
  (id)? @function.call
  "("
  (args
    (named_argument
      (id) @parameter
    )
  )?
  ")"
)

# ERROR
(ERROR) @error
