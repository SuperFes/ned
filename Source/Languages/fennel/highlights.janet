#; Highlights, ned-authored: TravonteD/tree-sitter-fennel ships no
#; queries. The special forms are anonymous tokens of their own nodes;
#; everything else is a symbol.
(comment) @comment
(string) @string
(escape_sequence) @string.escape
(number) @number
(boolean) @constant.builtin
(nil) @constant.builtin
(vararg) @variable.parameter

[
  "fn" "lambda" "λ" "hashfn"
  "let" "local" "var" "global" "set"
  "each" "for" "match"
  "collect" "icollect" "accumulate" "quote"
] @keyword

# The special forms the grammar reads as plain symbols.
((list
   .
   (symbol) @keyword)
  (:match? @keyword "^(do|if|when|while|case|tset|macro|macros|import-macros|require-macros|eval-compiler|values|include|doto|->|->>|-\\?>|-\\?>>|\\?\\.|comment|lua|pick-values|pick-args|with-open)$"))

(fn
  name: (symbol) @function)
(fn
  name: (multi_symbol
    (symbol) @function .))
(lambda
  name: (symbol) @function)
(list
  .
  (symbol) @function.call)
(list
  .
  (multi_symbol
    (symbol) @function.call .))
(multi_symbol_method
  (symbol) @function.method .)

(parameters
  (binding
    (symbol) @variable.parameter))
(binding
  (symbol) @variable)

(table_pair
  (string) @property)

[
  "(" ")" "[" "]" "{" "}"
] @punctuation.bracket
