# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "wgsl_bevy"
 :word identifier
 :inherits "wgsl"
 :extras [line_comment block_comment (:pattern "[\\s\\uFEFF\\u2060\\u200B\\u00A0]")]
 :conflicts []
 :precedences []
 :externals [block_comment]
 :inline []
 :supertypes []
 :rules
 {source_file (:seq (:repeat enable_directive) (:repeat _declaration))
  line_comment (:token (:seq "//" (:pattern ".*")))
  _declaration (:choice
                preproc_import
                define_import_path
                preproc_ifdef
                (:choice
                 ";"
                 (:seq global_variable_declaration ";")
                 (:seq global_constant_declaration ";")
                 (:seq type_alias_declaration ";")
                 struct_declaration
                 function_declaration))
  global_variable_declaration (:seq
                               (:repeat attribute)
                               variable_declaration
                               (:choice (:seq "=" const_expression) :blank))
  global_constant_declaration (:choice
                               (:seq
                                "let"
                                (:choice identifier variable_identifier_declaration)
                                "="
                                const_expression)
                               (:seq
                                (:repeat attribute)
                                "override"
                                (:choice identifier variable_identifier_declaration)
                                (:choice (:seq "=" _expression) :blank)))
  const_expresssion (:choice
                     (:seq
                      type_declaration
                      "("
                      (:choice
                       (:seq
                        (:repeat (:seq const_expression ","))
                        const_expression
                        (:choice "," :blank))
                       :blank)
                      ")")
                     const_literal)
  type_alias_declaration (:seq "type" identifier "=" type_declaration)
  const_expression (:prec-left 0
                    (:choice
                     (:seq
                      type_declaration
                      "("
                      (:choice
                       (:seq
                        (:repeat (:seq const_expression ","))
                        const_expression
                        (:choice "," :blank))
                       :blank)
                      ")")
                     const_literal))
  function_declaration (:choice
                        (:seq
                         (:choice "virtual" :blank)
                         (:seq
                          (:repeat attribute)
                          "fn"
                          (:field :name identifier)
                          "("
                          (:field :parameters (:choice parameter_list :blank))
                          ")"
                          (:field :type (:choice function_return_type_declaration :blank))
                          (:field :body compound_statement)))
                        (:seq
                         "override"
                         (:repeat attribute)
                         "fn"
                         (:field :name import_path)
                         "("
                         (:field :parameters (:choice parameter_list :blank))
                         ")"
                         (:field :type (:choice function_return_type_declaration :blank))
                         (:field :body compound_statement)))
  function_return_type_declaration (:seq "->" (:repeat attribute) type_declaration)
  struct_declaration (:seq "struct" (:field :name identifier) "{" _struct_declaration_content "}")
  struct_member (:seq (:repeat attribute) variable_identifier_declaration)
  enable_directive (:seq "enable" identifier ";")
  attribute (:seq
             "@"
             identifier
             (:choice
              (:seq
               "("
               (:repeat (:seq _literal_or_identifier ","))
               _literal_or_identifier
               (:choice "," :blank)
               ")")
              :blank))
  _literal_or_identifier (:choice float_literal int_literal identifier)
  identifier (:pattern "([a-zA-Z_][0-9a-zA-Z][0-9a-zA-Z_]*)|([a-zA-Z][0-9a-zA-Z_]*)")
  parameter_list (:seq (:repeat (:seq parameter ",")) parameter (:choice "," :blank))
  parameter (:seq (:repeat attribute) variable_identifier_declaration)
  _statement (:choice
              preproc_import
              (:alias preproc_ifdef_in_statement preproc_ifdef)
              (:choice
               compound_statement
               (:seq assignment_statement ";")
               if_statement
               switch_statement
               loop_statement
               for_statement
               while_statement
               break_statement
               continue_statement
               discard_statement
               (:seq return_statement ";")
               (:seq variable_statement ";")
               increment_statement
               decrement_statement))
  compound_statement (:seq "{" (:repeat _statement) "}")
  assignment_statement (:choice
                        (:seq
                         (:field :left lhs_expression)
                         (:choice "=" compound_assignment_operator)
                         (:field :right _expression))
                        (:seq (:field :left "_") "=" (:field :right _expression)))
  compound_assignment_operator (:choice "+=" "-=" "*=" "/=" "%=" "&=" "|=" "^=")
  if_statement (:seq
                "if"
                (:field :condition _expression)
                (:field :consequence compound_statement)
                (:choice (:seq "else" (:field :alternative else_statement)) :blank))
  else_statement (:choice compound_statement if_statement)
  switch_statement (:seq "switch" _expression "{" (:repeat1 switch_body) "}")
  switch_body (:choice
               (:seq "case" case_selectors (:choice ":" :blank) case_compound_statement)
               (:seq "default" (:choice ":" :blank) case_compound_statement))
  case_selectors (:seq const_literal (:repeat (:seq "," const_literal)) (:choice "," :blank))
  case_compound_statement (:seq "{" (:repeat _statement) (:choice fallthrough_statement :blank) "}")
  fallthrough_statement (:seq "fallthrough" ";")
  loop_statement (:seq "loop" "{" (:repeat _statement) (:choice continuing_statement :blank) "}")
  for_statement (:seq "for" "(" for_header ")" compound_statement)
  for_header (:seq
              (:choice
               (:choice
                variable_statement
                assignment_statement
                type_constructor_or_function_call_expression
                increment_statement
                decrement_statement)
               :blank)
              ";"
              (:choice _expression :blank)
              ";"
              (:choice
               (:choice
                increment_statement
                decrement_statement
                assignment_statement
                type_constructor_or_function_call_expression)
               :blank))
  while_statement (:seq "while" (:field :condition _expression) compound_statement)
  break_statement (:seq "break" ";")
  break_if_statement (:seq "break" "if" _expression ";")
  continue_statement (:seq "continue" ";")
  continuing_statement (:seq "continuing" continuing_compound_statement)
  continuing_compound_statement (:seq
                                 "{"
                                 (:repeat _statement)
                                 (:choice break_if_statement :blank)
                                 "}")
  return_statement (:seq "return" (:choice _expression :blank))
  discard_statement (:seq "discard" ";")
  variable_statement (:choice
                      variable_declaration
                      (:seq variable_declaration "=" _expression)
                      (:seq
                       "let"
                       (:choice identifier variable_identifier_declaration)
                       "="
                       _expression))
  variable_declaration (:seq
                        "var"
                        (:choice variable_qualifier :blank)
                        (:choice identifier variable_identifier_declaration))
  variable_qualifier (:seq "<" address_space (:choice (:seq "," access_mode) :blank) ">")
  variable_identifier_declaration (:seq
                                   (:field :name identifier)
                                   ":"
                                   (:field :type type_declaration))
  increment_statement (:seq lhs_expression "++")
  decrement_statement (:seq lhs_expression "--")
  _expression (:choice
               const_literal
               parenthesized_expression
               type_constructor_or_function_call_expression
               composite_value_decomposition_expression
               bitcast_expression
               binary_expression
               unary_expression
               subscript_expression
               identifier)
  const_literal (:choice int_literal float_literal bool_literal)
  int_literal (:pattern "(-?0[xX][0-9a-fA-F]+|0|-?[1-9][0-9]*)[iu]?")
  float_literal (:choice
                 (:pattern "(-?(([0-9]*\\.[0-9]+|[0-9]+\\.[0-9]*)([eE](\\+|-)?[0-9]+)?)|([0-9]+[eE](\\+|-)?[0-9]+))f?|0f|-?[1-9][0-9]*f")
                 (:pattern "-?0[xX]((([0-9a-fA-F]*\\.[0-9a-fA-F]+|[0-9a-fA-F]+\\.[0-9a-fA-F]*)([pP](\\+|-)?[0-9]+f?)?)|([0-9a-fA-F]+[pP](\\+|-)?[0-9]+f?))"))
  bool_literal (:choice "true" "false")
  parenthesized_expression (:seq "(" _expression ")")
  type_constructor_or_function_call_expression (:seq
                                                (:choice type_declaration _vec_prefix _mat_prefix)
                                                argument_list_expression)
  type_declaration (:choice
                    "bool"
                    "u32"
                    "i32"
                    "f32"
                    "f16"
                    (:seq _vec_prefix "<" type_declaration ">")
                    (:seq _mat_prefix "<" type_declaration ">")
                    (:seq
                     "array"
                     "<"
                     type_declaration
                     (:choice (:seq "," (:choice int_literal identifier)) :blank)
                     ">")
                    (:seq
                     "ptr"
                     "<"
                     address_space
                     ","
                     type_declaration
                     (:choice (:seq "," access_mode) :blank)
                     ">")
                    "sampler"
                    "sampler_comparison"
                    "texture_depth_2d"
                    "texture_depth_2d_array"
                    "texture_depth_cube"
                    "texture_depth_cube_array"
                    "texture_depth_multisampled_2d"
                    (:seq "texture_1d" "<" (:choice "f16" "f32" "i32" "u32") ">")
                    (:seq "texture_2d" "<" (:choice "f16" "f32" "i32" "u32") ">")
                    (:seq "texture_2d_array" "<" (:choice "f16" "f32" "i32" "u32") ">")
                    (:seq "texture_3d" "<" (:choice "f16" "f32" "i32" "u32") ">")
                    (:seq "texture_cube" "<" (:choice "f16" "f32" "i32" "u32") ">")
                    (:seq "texture_cube_array" "<" (:choice "f16" "f32" "i32" "u32") ">")
                    (:seq "texture_multisampled_2d" "<" (:choice "f16" "f32" "i32" "u32") ">")
                    (:seq "texture_storage_1d" "<" texel_format "," access_mode ">")
                    (:seq "texture_storage_2d" "<" texel_format "," access_mode ">")
                    (:seq "texture_storage_2d_array" "<" texel_format "," access_mode ">")
                    (:seq "texture_storage_3d" "<" texel_format "," access_mode ">")
                    identifier)
  _vec_prefix (:choice "vec2" "vec3" "vec4")
  _mat_prefix (:choice
               "mat2x2"
               "mat2x3"
               "mat2x4"
               "mat3x2"
               "mat3x3"
               "mat3x4"
               "mat4x2"
               "mat4x3"
               "mat4x4")
  texel_format (:choice
                "rgba8unorm"
                "rgba8snorm"
                "rgba8uint"
                "rgba8sint"
                "rgba16uint"
                "rgba16sint"
                "rgba16float"
                "r32uint"
                "r32sint"
                "r32float"
                "rg32uint"
                "rg32sint"
                "rg32float"
                "rgba32uint"
                "rgba32sint"
                "rgba32float")
  address_space (:choice "function" "private" "workgroup" "uniform" "storage")
  access_mode (:choice "read" "write" "read_write")
  argument_list_expression (:seq
                            "("
                            (:choice
                             (:seq
                              (:repeat (:seq _expression ","))
                              _expression
                              (:choice "," :blank))
                             :blank)
                            ")")
  bitcast_expression (:seq "bitcast" "<" type_declaration ">" parenthesized_expression)
  binary_expression (:choice
                     (:prec-left 1
                      (:seq (:field :left _expression) "||" (:field :right _expression)))
                     (:prec-left 2
                      (:seq (:field :left _expression) "&&" (:field :right _expression)))
                     (:prec-left 3
                      (:seq (:field :left _expression) "|" (:field :right _expression)))
                     (:prec-left 4
                      (:seq (:field :left _expression) "^" (:field :right _expression)))
                     (:prec-left 5
                      (:seq (:field :left _expression) "&" (:field :right _expression)))
                     (:prec-left 6
                      (:seq (:field :left _expression) "==" (:field :right _expression)))
                     (:prec-left 6
                      (:seq (:field :left _expression) "!=" (:field :right _expression)))
                     (:prec-left 7
                      (:seq (:field :left _expression) "<" (:field :right _expression)))
                     (:prec-left 7
                      (:seq (:field :left _expression) ">" (:field :right _expression)))
                     (:prec-left 7
                      (:seq (:field :left _expression) "<=" (:field :right _expression)))
                     (:prec-left 7
                      (:seq (:field :left _expression) ">=" (:field :right _expression)))
                     (:prec-left 8
                      (:seq (:field :left _expression) "<<" (:field :right _expression)))
                     (:prec-left 8
                      (:seq (:field :left _expression) ">>" (:field :right _expression)))
                     (:prec-left 9
                      (:seq (:field :left _expression) "+" (:field :right _expression)))
                     (:prec-left 9
                      (:seq (:field :left _expression) "-" (:field :right _expression)))
                     (:prec-left 10
                      (:seq (:field :left _expression) "*" (:field :right _expression)))
                     (:prec-left 10
                      (:seq (:field :left _expression) "/" (:field :right _expression)))
                     (:prec-left 10
                      (:seq (:field :left _expression) "%" (:field :right _expression))))
  unary_expression (:prec-left 11
                    (:seq (:choice "-" "!" "~" "*" "&") (:field :argument _expression)))
  postfix_expression (:prec-left 11
                      (:seq
                       (:choice
                        (:seq "[" _expression "]" (:choice postfix_expression :blank))
                        (:seq "." identifier (:choice postfix_expression :blank)))
                       (:choice postfix_expression :blank)))
  subscript_expression (:prec 11
                        (:seq (:field :value _expression) "[" (:field :subscript _expression) "]"))
  lhs_expression (:seq
                  (:repeat (:choice "*" "&"))
                  (:choice identifier (:seq "(" lhs_expression ")"))
                  (:choice postfix_expression :blank))
  composite_value_decomposition_expression (:prec 11
                                            (:seq
                                             (:field :value _expression)
                                             "."
                                             (:field :accessor identifier)))
  _struct_declaration_content (:seq
                               (:repeat
                                (:choice
                                 (:seq struct_member ",")
                                 preproc_import
                                 (:alias preproc_ifdef_in_struct_declaration preproc_ifdef)))
                               (:choice
                                struct_member
                                preproc_import
                                (:alias preproc_ifdef_in_struct_declaration preproc_ifdef))
                               (:choice "," :blank))
  preproc_import (:seq
                  (:alias (:pattern "#[ \t]*import") "#import")
                  (:field :path (:choice import_path))
                  (:choice
                   (:choice (:seq identifier (:repeat (:seq "," identifier))) (:field :alias alias))
                   :blank)
                  "\n")
  define_import_path (:seq
                      (:alias (:pattern "#[ \t]*define_import_path") "#define_import_path")
                      (:field :path (:choice import_path))
                      "\n")
  import_path (:seq identifier (:repeat (:seq "::" identifier)))
  alias (:seq "as" identifier)
  preproc_ifdef (:seq
                 (:choice
                  (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                  (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                 (:field :name identifier)
                 (:repeat _declaration)
                 (:field :alternative (:choice (:choice preproc_else) :blank))
                 (:alias (:pattern "#[ \t]*endif") "#endif"))
  preproc_else (:seq (:alias (:pattern "#[ \t]*else") "#else") (:repeat _declaration))
  preproc_ifdef_in_statement (:seq
                              (:choice
                               (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                               (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                              (:field :name identifier)
                              (:repeat _statement)
                              (:field :alternative
                               (:choice
                                (:choice (:alias preproc_else_in_statement preproc_else))
                                :blank))
                              (:alias (:pattern "#[ \t]*endif") "#endif"))
  preproc_else_in_statement (:seq (:alias (:pattern "#[ \t]*else") "#else") (:repeat _statement))
  preproc_ifdef_in_struct_declaration (:seq
                                       (:choice
                                        (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                        (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                                       (:field :name identifier)
                                       (:repeat
                                        (:seq
                                         (:choice
                                          struct_member
                                          preproc_import
                                          (:alias preproc_ifdef_in_struct_declaration preproc_ifdef))
                                         (:choice "," :blank)))
                                       (:field :alternative
                                        (:choice
                                         (:choice
                                          (:alias preproc_else_in_struct_declaration preproc_else))
                                         :blank))
                                       (:alias (:pattern "#[ \t]*endif") "#endif"))
  preproc_else_in_struct_declaration (:seq
                                      (:alias (:pattern "#[ \t]*else") "#else")
                                      (:repeat
                                       (:seq
                                        (:choice
                                         struct_member
                                         preproc_import
                                         (:alias preproc_ifdef_in_struct_declaration preproc_ifdef))
                                        (:choice "," :blank))))}}
