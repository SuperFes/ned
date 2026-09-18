#; Highlights, ned-authored: theHamsta/tree-sitter-commonlisp ships tags
#; only (its highlights live in nvim-treesitter). Literal kinds map
#; straight to classes; a list's head symbol is a call; defun forms are
#; keywords with their names.
(comment) @comment
(block_comment) @comment
(str_lit) @string
(num_lit) @number
(complex_num_lit) @number
(char_lit) @string.special
(kwd_lit) @constant
(nil_lit) @constant.builtin
(package_lit) @namespace
(path_lit) @string.special.path
(dis_expr) @comment

(defun_keyword) @keyword
(defun_header
  function_name: (sym_lit) @function)
(defun_header
  lambda_list: (list_lit
    (sym_lit) @variable.parameter))

(loop_macro
  (for_clause_word) @keyword)
(loop_macro
  (accumulation_verb) @keyword)

(list_lit
  .
  (sym_lit) @function.call)

((sym_lit) @keyword
 (:match? @keyword "^(let|let\\*|if|when|unless|cond|case|progn|prog1|prog2|do|dolist|dotimes|loop|return|return-from|block|lambda|setf|setq|defvar|defparameter|defconstant|defstruct|defclass|deftype|defpackage|in-package|declare|declaim|flet|labels|macrolet|handler-case|handler-bind|unwind-protect|multiple-value-bind|destructuring-bind|the|quote|function|eval-when)$"))

((sym_lit) @constant.builtin
 (:match? @constant.builtin "^(t|nil)$"))

(quoting_lit) @string.special.symbol
(syn_quoting_lit) @string.special.symbol
(unquoting_lit) @punctuation.special
(unquote_splicing_lit) @punctuation.special
(format_directive_type) @string.special
(format_specifier) @string.special

[
  "(" ")"
] @punctuation.bracket
