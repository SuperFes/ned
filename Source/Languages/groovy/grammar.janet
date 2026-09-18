# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "groovy"
 :word identifier
 :extras [(:pattern "\\s") comment groovy_doc]
 :conflicts [[_callable_expression juxt_function_call]
             [_callable_expression _juxt_argument_list]
             [_juxtable_expression _juxt_argument_list]]
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {source_file (:seq (:choice shebang :blank) (:repeat _statement) (:choice pipeline :blank))
  shebang (:seq "#!" (:pattern "[^\\n]*"))
  _statement (:prec-left 17
              (:seq
               (:choice label :blank)
               (:choice
                assertion
                groovy_import
                groovy_package
                assignment
                class_definition
                declaration
                do_while_loop
                for_in_loop
                for_loop
                function_call
                function_declaration
                function_definition
                if_statement
                juxt_function_call
                return
                switch_statement
                try_statement
                while_loop
                closure
                (:alias "break" break)
                (:alias "continue" continue)
                _expression)
               (:choice ";" :blank)))
  label (:seq (:field :name identifier) ":")
  access_op (:choice
             (:prec-left 16 (:seq _expression ".&" _expression))
             (:prec-left 16 (:seq _expression ".@" _expression))
             (:prec-left 16 (:seq _expression "?." _expression))
             (:prec-left 16 (:seq _expression "*." _expression))
             (:prec-left 16 (:seq "*" _expression))
             (:prec-left 16 (:seq "*:" _expression)))
  dotted_identifier (:prec-left 1
                     (:seq
                      (:choice _primary_expression _type_identifier)
                      (:repeat1
                       (:seq
                        "."
                        (:choice
                         identifier
                         quoted_identifier
                         _type_identifier
                         parenthesized_expression)))))
  _import_name (:choice
                identifier
                _type_identifier
                (:seq _import_name "." (:choice identifier _type_identifier)))
  groovy_import (:seq
                 "import"
                 (:choice modifier :blank)
                 (:field :import (:alias _import_name qualified_name))
                 (:choice
                  (:choice
                   (:seq "." (:alias (:token-immediate "*") wildcard_import))
                   (:seq "as" (:field :import_alias (:choice identifier _type_identifier))))
                  :blank))
  groovy_package (:seq "package" (:alias _import_name qualified_name))
  annotation (:prec-right 0
              (:seq
               "@"
               (:alias
                (:token-immediate
                 (:pattern "(?:((?:([$_a-z\\u00C0-\\u00D6\\u00D8-\\u00F6\\u00F8-\\u00F8\\u0100-\\uFFFE][$_0-9a-zA-Z\\u00C0-\\u00D6\\u00D8-\\u00F6\\u00F8-\\u00F8\\u0100-\\uFFFE]*))|(?:([_A-Z][$_0-9A-Z]*))))|(?:([A-Z][$_0-9a-zA-Z\\u00C0-\\u00D6\\u00D8-\\u00F6\\u00F8-\\u00F8\\u0100-\\uFFFE]*))"))
                identifier)
               (:choice argument_list :blank)))
  assertion (:seq "assert" _expression)
  assignment (:prec -1
              (:choice
               (:seq
                (:choice _juxtable_expression parenthesized_expression)
                (:choice "=" "**=" "*=" "/=" "%=" "+=" "-=" "<<=" ">>=" ">>>=" "&=" "^=" "|=" "?=")
                _expression)
               increment_op))
  increment_op (:prec 2
                (:choice
                 (:prec-left 14 (:seq _primary_expression "++"))
                 (:prec-left 14 (:seq _primary_expression "--"))
                 (:prec-right 14 (:seq "++" _primary_expression))
                 (:prec-right 14 (:seq "--" _primary_expression))))
  binary_op (:choice
             (:prec-left 13 (:seq _expression "%" _expression))
             (:prec-left 13 (:seq _expression "*" _expression))
             (:prec-left 13 (:seq _expression "/" _expression))
             (:prec-left 12 (:seq _expression "+" _expression))
             (:prec-left 12 (:seq _expression "-" _expression))
             (:prec-left 11 (:seq _expression "<<" _expression))
             (:prec-left 11 (:seq _expression ">>" _expression))
             (:prec-left 11 (:seq _expression ">>>" _expression))
             (:prec-left 11 (:seq _expression ".." _expression))
             (:prec-left 11 (:seq _expression "..<" _expression))
             (:prec-left 11 (:seq _expression "<..<" _expression))
             (:prec-left 11 (:seq _expression "<.." _expression))
             (:prec-left 10 (:seq _expression "<" _expression))
             (:prec-left 10 (:seq _expression "<=" _expression))
             (:prec-left 10 (:seq _expression ">" _expression))
             (:prec-left 10 (:seq _expression ">=" _expression))
             (:prec-left 10 (:seq _expression "in" _expression))
             (:prec-left 10 (:seq _expression "!in" _expression))
             (:prec-left 10 (:seq _expression "instanceof" _expression))
             (:prec-left 10 (:seq _expression "!instanceof" _expression))
             (:prec-left 10 (:seq _expression "as" _expression))
             (:prec-left 9 (:seq _expression "==" _expression))
             (:prec-left 9 (:seq _expression "!=" _expression))
             (:prec-left 9 (:seq _expression "<=>" _expression))
             (:prec-left 9 (:seq _expression "===" _expression))
             (:prec-left 9 (:seq _expression "!==" _expression))
             (:prec-left 9 (:seq _expression "=~" _expression))
             (:prec-left 9 (:seq _expression "==~" _expression))
             (:prec-left 8 (:seq _expression "&" _expression))
             (:prec-left 7 (:seq _expression "^" _expression))
             (:prec-left 6 (:seq _expression "|" _expression))
             (:prec-left 5 (:seq _expression "&&" _expression))
             (:prec-left 4 (:seq _expression "||" _expression))
             (:prec-left 3 (:seq _expression "?:" _expression))
             (:prec-right 15 (:seq _expression "**" _expression)))
  boolean_literal (:choice "true" "false")
  class_definition (:seq
                    (:repeat annotation)
                    (:choice access_modifier :blank)
                    (:repeat modifier)
                    (:choice "@interface" "interface" "class")
                    (:field :name (:choice identifier _type_identifier))
                    (:choice (:field :generics generic_parameters) :blank)
                    (:choice (:seq "extends" (:field :superclass _primary_expression)) :blank)
                    (:field :body closure))
  generic_parameters (:seq
                      "<"
                      (:seq
                       (:repeat (:prec-left 0 (:seq generic_param ",")))
                       (:seq generic_param (:choice "," :blank)))
                      ">")
  generic_param (:seq
                 (:field :name identifier)
                 (:choice (:seq "extends" (:field :superclass _type)) :blank))
  closure (:seq
           "{"
           (:choice (:choice "->" (:seq (:alias _param_list parameter_list) "->")) :blank)
           (:repeat _statement)
           (:choice _expression :blank)
           "}")
  comment (:choice
           (:pattern "\\/\\/[^\\n]*")
           (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*\\/")))
  groovy_doc (:seq
              "/**"
              (:token-immediate (:pattern "[*\\n\\s]+"))
              (:alias (:token-immediate (:pattern "[^\\n\\.]+[\\.]?")) first_line)
              (:repeat
               (:choice
                groovy_doc_param
                groovy_doc_throws
                groovy_doc_tag
                groovy_doc_at_text
                (:pattern "([^@*]|\\*[^/])([^*\\s@]|[^\\s\\n]@|\\*[^/])+")))
              "*/")
  groovy_doc_param (:seq "@param" identifier)
  groovy_doc_throws (:seq "@throws" identifier)
  groovy_doc_tag (:pattern "@[a-z]+")
  groovy_doc_at_text (:pattern "@[^@\\s*]*")
  declaration (:seq
               (:repeat annotation)
               (:choice access_modifier :blank)
               (:choice
                (:seq
                 (:repeat modifier)
                 (:choice
                  "_"
                  (:seq
                   (:choice (:field :type _type) "def")
                   (:field :name identifier)
                   (:choice (:seq "=" (:field :value _expression)) :blank))))
                (:seq
                 (:repeat1 modifier)
                 (:choice
                  "_"
                  (:seq
                   (:choice (:choice (:field :type _type) "def") :blank)
                   (:field :name identifier)
                   (:choice (:seq "=" (:field :value _expression)) :blank))))))
  parenthesized_expression (:prec 2
                            (:choice
                             (:seq "(" (:choice _expression _immediately_invoked_closure) ")")))
  _expression (:prec 1
               (:choice
                _primary_expression
                increment_op
                binary_op
                ternary_op
                unary_op
                access_op
                closure
                (:alias "null" null)))
  _primary_expression (:prec-left 1
                       (:choice number_literal boolean_literal string list map _callable_expression))
  _callable_expression (:choice
                        "this"
                        function_call
                        parenthesized_expression
                        _juxtable_expression
                        _type_identifier)
  _juxtable_expression (:choice dotted_identifier identifier index)
  do_while_loop (:seq
                 "do"
                 (:field :body (:choice _statement closure))
                 "while"
                 (:field :condition parenthesized_expression))
  for_parameters (:seq
                  "("
                  (:field :initializer
                   (:choice (:seq declaration (:repeat (:seq "," assignment))) :blank))
                  ";"
                  (:field :condition (:choice _expression :blank))
                  ";"
                  (:field :increment
                   (:choice (:seq _statement (:repeat (:seq "," _statement))) :blank))
                  ")")
  for_loop (:seq "for" for_parameters (:field :body (:choice _statement closure)))
  for_in_loop (:prec 1
               (:seq
                "for"
                "("
                (:field :variable identifier)
                "in"
                (:field :collection _expression)
                ")"
                (:field :body (:choice _statement closure))))
  function_call (:prec-left 2
                 (:seq (:field :function _callable_expression) (:field :args argument_list)))
  __immediately_invoked_closure (:prec-left 2
                                 (:seq (:field :function closure) (:field :args argument_list)))
  _immediately_invoked_closure (:alias __immediately_invoked_closure function_call)
  argument_list (:prec-right 1
                 (:seq
                  (:prec-left 0
                   (:seq
                    "("
                    (:choice
                     (:seq
                      (:repeat (:prec-left 0 (:seq (:choice map_item _expression) ",")))
                      (:seq (:choice map_item _expression) (:choice "," :blank)))
                     :blank)
                    ")"))
                  (:choice closure :blank)))
  _param_list (:prec 1
               (:seq
                (:repeat (:prec-left 0 (:seq parameter ",")))
                (:seq parameter (:choice "," :blank))))
  parameter_list (:prec 1
                  (:seq
                   "("
                   (:choice
                    (:seq
                     (:repeat (:prec-left 0 (:seq parameter ",")))
                     (:seq parameter (:choice "," :blank)))
                    :blank)
                   ")"))
  parameter (:prec -1
             (:seq
              (:choice (:field :type (:choice _type "def")) :blank)
              (:field :name identifier)
              (:choice (:seq "=" (:field :value _expression)) :blank)))
  function_declaration (:prec 2
                        (:seq
                         (:repeat annotation)
                         (:choice access_modifier :blank)
                         (:repeat modifier)
                         (:field :type (:choice _type "def"))
                         (:field :function (:choice identifier quoted_identifier))
                         (:field :parameters parameter_list)))
  function_definition (:prec 3
                       (:seq
                        (:repeat annotation)
                        (:choice access_modifier :blank)
                        (:repeat modifier)
                        (:field :type (:choice _type "def"))
                        (:field :function (:choice identifier quoted_identifier))
                        (:field :parameters parameter_list)
                        (:field :body closure)))
  identifier (:pattern "(?:([$_a-z\\u00C0-\\u00D6\\u00D8-\\u00F6\\u00F8-\\u00F8\\u0100-\\uFFFE][$_0-9a-zA-Z\\u00C0-\\u00D6\\u00D8-\\u00F6\\u00F8-\\u00F8\\u0100-\\uFFFE]*))|(?:([_A-Z][$_0-9A-Z]*))")
  _type_identifier (:alias
                    (:pattern "[A-Z][$_0-9a-zA-Z\\u00C0-\\u00D6\\u00D8-\\u00F6\\u00F8-\\u00F8\\u0100-\\uFFFE]*")
                    identifier)
  quoted_identifier (:choice _plain_string _interpolate_string)
  if_statement (:prec-left 0
                (:seq
                 "if"
                 (:field :condition parenthesized_expression)
                 (:field :body (:choice _statement closure))
                 (:choice (:seq "else" (:field :else_body (:choice _statement closure))) :blank)))
  index (:prec 16 (:seq _primary_expression "[" _expression "]"))
  juxt_function_call (:seq
                      (:field :function _juxtable_expression)
                      (:field :args (:alias _juxt_argument_list argument_list)))
  _juxt_argument_list (:prec-left 2
                       (:seq
                        (:choice
                         map_item
                         increment_op
                         binary_op
                         ternary_op
                         unary_op
                         access_op
                         closure
                         (:alias "null" null)
                         number_literal
                         boolean_literal
                         string
                         list
                         map
                         "this"
                         function_call
                         dotted_identifier
                         identifier
                         index)
                        (:repeat
                         (:seq
                          ","
                          (:choice
                           map_item
                           increment_op
                           binary_op
                           ternary_op
                           unary_op
                           access_op
                           closure
                           (:alias "null" null)
                           number_literal
                           boolean_literal
                           string
                           list
                           map
                           "this"
                           function_call
                           dotted_identifier
                           identifier
                           index)))))
  list (:prec 1
        (:seq
         "["
         (:repeat (:prec-left 0 (:seq _expression ",")))
         (:choice (:seq _expression (:choice "," :blank)) :blank)
         "]"))
  map_item (:seq
            (:field :key
             (:choice identifier _type_identifier number_literal string parenthesized_expression))
            ":"
            (:field :value _expression))
  map (:choice
       (:seq "[" (:repeat (:prec-left 0 (:seq map_item ","))) map_item (:choice "," :blank) "]")
       (:seq "[" ":" "]"))
  number_literal (:choice
                  (:pattern "-?[0-9]+(_[0-9]+)*[DFGILdfgil]?")
                  (:pattern "-?0x[0-9a-fA-F]+(_[0-9a-fA-F]+)*[DFGILdfgil]?")
                  (:pattern "-?0b[0-1]+(_[0-1]+)*[DFGILdfgil]?")
                  (:pattern "-?0[0-7]+(_[0-7]+)*[DFGILdfgil]?")
                  (:pattern "-?[0-9]+(_[0-9]+)*\\.[0-9]+(_[0-9]+)*([eE][0-9]+)?[DFGILdfgil]?"))
  pipeline (:seq "pipeline" closure)
  return (:prec-right 1 (:seq "return" (:choice _expression :blank)))
  string (:choice _plain_string _interpolate_string)
  _plain_string (:choice
                 (:seq
                  "'"
                  (:repeat
                   (:choice
                    (:alias (:token-immediate (:prec 1 (:pattern "[^\\\\'\\n]+"))) string_content)
                    escape_sequence))
                  "'")
                 (:seq
                  "'''"
                  (:repeat
                   (:seq
                    (:choice
                     (:alias
                      (:token-immediate (:prec 0 (:pattern "[']{1,2}")))
                      string_internal_quote)
                     :blank)
                    (:choice
                     (:alias
                      (:token-immediate (:prec 1 (:pattern "([^\\\\']|[']{1,2}[^'\\\\])+")))
                      string_content)
                     (:seq (:choice (:pattern "[']{1,2}") :blank) escape_sequence))))
                  "'''"))
  _interpolate_string (:choice
                       (:seq
                        "\""
                        (:repeat
                         (:choice
                          (:alias
                           (:token-immediate (:prec 1 (:pattern "[^$\\\\\"\\n]+")))
                           string_content)
                          escape_sequence
                          interpolation))
                        "\"")
                       (:seq
                        "\"\"\""
                        (:repeat
                         (:seq
                          (:choice
                           (:alias
                            (:token-immediate
                             (:prec 1 (:pattern "([^$\\\\\"]|[\"]{1,2}[^\"$\\\\])+")))
                            string_content)
                           (:seq (:choice (:pattern "[\"]{1,2}") :blank) escape_sequence)
                           (:seq (:choice (:pattern "[\"]{1,2}") :blank) interpolation))))
                        "\"\"\"")
                       (:seq
                        "/"
                        (:repeat1
                         (:choice
                          (:alias
                           (:token-immediate (:prec 1 (:pattern "[^$\\\\\\/]+")))
                           string_content)
                          (:alias "\\/" escape_sequence)
                          (:alias (:pattern "\\\\[^\\/]") string_content)
                          interpolation))
                        "/")
                       (:seq
                        "$/"
                        (:repeat
                         (:choice
                          (:alias
                           (:token-immediate
                            (:prec 1 (:pattern "([^$\\/]|\\/[^$]|\\$[^\\/$a-zA-Z{])+")))
                           string_content)
                          (:alias "$/" escape_sequence)
                          (:alias "$$" escape_sequence)
                          interpolation))
                        "/$"))
  escape_sequence (:token
                   (:prec 1
                    (:seq
                     "\\"
                     (:choice (:pattern "[$bfnrst\\\\'\"\\n]") (:pattern "u[0-9a-fA-F]{4}")))))
  interpolation (:seq
                 "$"
                 (:choice
                  (:seq "{" _expression "}")
                  (:alias
                   (:token-immediate (:pattern "[a-zA-Z0-9_]+(\\.[a-zA-Z0-9_]+)*"))
                   identifier)))
  switch_statement (:seq
                    "switch"
                    (:field :value parenthesized_expression)
                    (:field :body switch_block))
  switch_block (:seq "{" (:repeat case) "}")
  case (:seq
        (:choice (:seq "case" (:field :value _expression) ":") (:seq "default" ":"))
        (:repeat _statement))
  ternary_op (:prec-right 0
              (:seq
               (:field :condition _expression)
               "?"
               (:field :then _expression)
               ":"
               (:field :else _expression)))
  try_statement (:prec-left 0
                 (:seq
                  "try"
                  (:field :body (:choice _statement closure))
                  (:choice
                   (:seq
                    "catch"
                    "("
                    (:field :catch_exception (:choice declaration _expression))
                    ")"
                    (:field :catch_body closure))
                   :blank)
                  (:choice (:seq "finally" (:field :finally_body closure)) :blank)))
  builtintype (:choice "int" "boolean" "char" "short" "int" "long" "float" "double" "void")
  _type (:prec 2 (:choice builtintype array_type type_with_generics _type_identifier))
  array_type (:seq _type "[]")
  access_modifier (:choice "public" "protected" "private")
  modifier (:choice "static" "final" "synchronized")
  type_with_generics (:seq _type generics)
  generics (:seq
            "<"
            (:seq (:repeat (:prec-left 0 (:seq _type ","))) (:seq _type (:choice "," :blank)))
            ">")
  unary_op (:choice
            (:prec-left 14 (:seq "+" _expression))
            (:prec-left 14 (:seq "-" _expression))
            (:prec-left 16 (:seq "~" _expression))
            (:prec-left 16 (:seq "!" _expression))
            (:prec-left 16 (:seq "new" _expression)))
  while_loop (:seq
              "while"
              (:field :condition parenthesized_expression)
              (:field :body (:choice _statement closure)))}}
