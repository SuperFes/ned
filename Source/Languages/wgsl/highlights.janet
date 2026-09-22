#; Highlight query. tree-sitter-wgsl-bevy ships no queries, so this is ned's
#; own, written against grammar.janet's node names. Bevy's preprocessor
#; directives are part of the grammar rather than an injection, and read as
#; keywords of their own.

[
  (line_comment)
  (block_comment)
] @comment @spell

[
  "fn"
  "let"
  "var"
  "override"
  "struct"
  "type"
  "enable"
  "virtual"
  "bitcast"
  "as"
] @keyword

[
  "#import"
  "#define_import_path"
  "#ifdef"
  "#ifndef"
  "#else"
  "#endif"
] @keyword.directive

[
  "if"
  "else"
  "switch"
  "case"
  "default"
  "fallthrough"
] @keyword.conditional

[
  "loop"
  "for"
  "while"
  "continuing"
  "break"
  "continue"
] @keyword.repeat

[
  "return"
  "discard"
] @keyword.return

[
  "function"
  "private"
  "workgroup"
  "uniform"
  "storage"
  "read"
  "write"
  "read_write"
] @keyword.modifier

[
  "bool"
  "f16"
  "f32"
  "i32"
  "u32"
  "array"
  "ptr"
  "sampler"
  "sampler_comparison"
  "vec2"
  "vec3"
  "vec4"
  "mat2x2"
  "mat2x3"
  "mat2x4"
  "mat3x2"
  "mat3x3"
  "mat3x4"
  "mat4x2"
  "mat4x3"
  "mat4x4"
  "texture_1d"
  "texture_2d"
  "texture_2d_array"
  "texture_3d"
  "texture_cube"
  "texture_cube_array"
  "texture_depth_2d"
  "texture_depth_2d_array"
  "texture_depth_cube"
  "texture_depth_cube_array"
  "texture_depth_multisampled_2d"
  "texture_multisampled_2d"
  "texture_storage_1d"
  "texture_storage_2d"
  "texture_storage_2d_array"
  "texture_storage_3d"
] @type.builtin

(texel_format) @type.builtin

[
  "true"
  "false"
] @boolean

(int_literal) @number
(float_literal) @number.float

#; The generic identifier rule comes first: a later pattern wins, so every
#; rule below narrows it.
(identifier) @variable

(attribute) @attribute

(type_declaration
  (identifier) @type)

(parameter
  (variable_identifier_declaration
    (identifier) @variable.parameter))

(struct_declaration
  name: (identifier) @type)

(function_declaration
  name: (identifier) @function)

[
  "!"
  "!="
  "%"
  "%="
  "&"
  "&&"
  "&="
  "*"
  "*="
  "+"
  "++"
  "+="
  "-"
  "--"
  "-="
  "->"
  "/"
  "/="
  "<"
  "<<"
  "<="
  "="
  "=="
  ">"
  ">="
  ">>"
  "^"
  "^="
  "|"
  "|="
  "||"
  "~"
] @operator

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

[
  ","
  ";"
  ":"
  "::"
  "."
] @punctuation.delimiter
