#; Highlights, ned-authored: PrestonKnopp/tree-sitter-gdscript ships no
#; queries. Python's shape.
(comment) @comment
(string) @string
(string_name) @string.special
(integer) @number
(float) @number
(type) @type
(inferred_type) @type
(annotation
  (identifier) @attribute)
(function_definition
  name: (name) @function)
(constructor_definition) @function
(class_definition
  name: (name) @type)
(class_name_statement
  name: (name) @type)
(signal_statement
  name: (name) @function)
(const_statement
  name: (name) @constant)
(enum_definition
  name: (name) @type)
(enumerator
  (identifier) @constant)
(call
  (identifier) @function.call)
(attribute_call
  (identifier) @function.method)
(typed_parameter
  (identifier) @variable.parameter)
(typed_default_parameter
  (identifier) @variable.parameter)
(binary_operator) @operator
[
  (true) (false) (null)
] @constant.builtin
[
  "and" "or" "not" "in" "is" "as"
] @keyword.operator
[
  "class" "class_name" "extends" "func" "var" "const" "signal" "enum"
  "if" "elif" "else" "for" "while" "match" "when" "break" "continue" "pass"
  "return" "await"
] @keyword
(static_keyword) @keyword
(remote_keyword) @keyword
