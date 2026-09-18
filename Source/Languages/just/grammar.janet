# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "just"
 :word identifier
 :extras [comment (:pattern "\\\\(\\n|\\r\\n)\\s*") (:pattern "\\s")]
 :conflicts []
 :precedences []
 :externals [_indent _dedent _newline text error_recovery]
 :inline [_string _string_indented _raw_string_indented _expression_recurse]
 :supertypes []
 :rules
 {source_file (:seq (:choice (:seq shebang _newline) :blank) (:repeat _item))
  _item (:choice recipe alias assignment export import module setting)
  alias (:seq (:repeat attribute) "alias" (:field :left identifier) ":=" (:field :right identifier))
  assignment (:seq (:field :left identifier) ":=" (:field :right expression) _newline)
  export (:seq "export" assignment)
  import (:seq "import" (:choice "?" :blank) string)
  module (:seq "mod" (:choice "?" :blank) (:field :name identifier) (:choice string :blank))
  setting (:choice
           (:seq
            "set"
            (:field :left identifier)
            (:field :right
             (:choice
              (:seq
               ":="
               (:choice
                boolean
                string
                (:field :array
                 (:seq
                  "["
                  (:choice
                   (:field :content
                    (:seq
                     (:seq (:field :element string) (:repeat (:seq "," (:field :element string))))
                     (:choice (:field :element string) :blank)))
                   :blank)
                  "]"))))
              :blank))
            _newline)
           (:seq
            "set"
            "shell"
            ":="
            (:field :right
             (:field :array
              (:seq
               "["
               (:choice
                (:field :content
                 (:seq
                  (:seq (:field :element string) (:repeat (:seq "," (:field :element string))))
                  (:choice (:field :element string) :blank)))
                :blank)
               "]")))
            _newline))
  boolean (:choice "true" "false")
  expression (:seq (:choice "/" :blank) _expression_inner)
  _expression_inner (:choice
                     if_expression
                     (:prec-left 2 (:seq _expression_recurse "+" _expression_recurse))
                     (:prec-left 1 (:seq _expression_recurse "/" _expression_recurse))
                     value)
  _expression_recurse (:alias _expression_inner "expression")
  if_expression (:seq
                 "if"
                 condition
                 (:field :consequence _braced_expr)
                 (:repeat (:field :alternative else_if_clause))
                 (:choice (:field :alternative else_clause) :blank))
  else_if_clause (:seq "else" "if" condition _braced_expr)
  else_clause (:seq "else" _braced_expr)
  _braced_expr (:seq "{" (:field :body expression) "}")
  condition (:choice
             (:seq expression "==" expression)
             (:seq expression "!=" expression)
             (:seq expression "=~" (:choice regex_literal expression))
             expression)
  regex_literal (:prec 1 string)
  value (:prec-left 0
         (:choice
          function_call
          external_command
          identifier
          string
          numeric_error
          (:seq "(" expression ")")))
  function_call (:seq
                 (:field :name identifier)
                 "("
                 (:choice (:field :arguments sequence) :blank)
                 ")")
  external_command (:choice (:seq _backticked) (:seq _indented_backticked))
  sequence (:seq expression (:repeat (:seq "," expression)))
  attribute_kv_argument (:seq (:field :key identifier) "=" (:field :value string))
  attribute (:seq
             "["
             (:seq
              (:choice
               identifier
               (:seq
                identifier
                "("
                (:field :argument
                 (:seq
                  (:choice string identifier attribute_kv_argument)
                  (:repeat (:seq "," (:choice string identifier attribute_kv_argument)))))
                ")")
               (:seq identifier ":" (:field :argument string)))
              (:repeat
               (:seq
                ","
                (:choice
                 identifier
                 (:seq
                  identifier
                  "("
                  (:field :argument
                   (:seq
                    (:choice string identifier attribute_kv_argument)
                    (:repeat (:seq "," (:choice string identifier attribute_kv_argument)))))
                  ")")
                 (:seq identifier ":" (:field :argument string))))))
             "]"
             _newline)
  recipe (:seq (:repeat attribute) recipe_header _newline (:choice recipe_body :blank))
  recipe_header (:seq
                 (:choice "@" :blank)
                 (:field :name (:choice identifier (:alias "import" identifier)))
                 (:choice parameters :blank)
                 ":"
                 (:choice dependencies :blank))
  parameters (:seq (:repeat parameter) (:choice parameter variadic_parameter))
  parameter (:seq
             (:choice "$" :blank)
             (:field :name identifier)
             (:choice (:seq "=" (:field :default value)) :blank))
  variadic_parameter (:seq (:field :kleene (:choice "*" "+")) parameter)
  dependencies (:repeat1 (:seq (:choice "&&" :blank) dependency))
  dependency (:choice (:field :name identifier) dependency_expression)
  dependency_expression (:seq "(" (:field :name identifier) (:repeat expression) ")")
  recipe_body (:seq
               _indent
               (:choice (:seq (:field :shebang shebang) _newline) :blank)
               (:repeat (:choice (:seq recipe_line _newline) _newline))
               _dedent)
  recipe_line (:seq (:choice recipe_line_prefix :blank) (:repeat1 (:choice text interpolation)))
  recipe_line_prefix (:choice "@-" "-@" "@" "-")
  shebang (:seq (:pattern "#![ \\t]*") (:choice _shebang_with_lang _opaque_shebang))
  _shebang_with_lang (:seq
                      (:pattern "\\S*\\/")
                      (:choice (:seq "env" (:repeat (:token (:pattern "-\\S*")))) :blank)
                      (:alias identifier language)
                      (:pattern ".*"))
  _opaque_shebang (:pattern "[^/\\n]+")
  string (:choice _string_indented _raw_string_indented _string (:pattern "'[^']*'"))
  _raw_string_indented (:seq "'''" (:repeat (:pattern ".")) "'''")
  _string (:seq "\"" (:repeat (:choice escape_sequence (:pattern "[^\\\\\"]+"))) "\"")
  _string_indented (:seq
                    "\"\"\""
                    (:repeat (:choice escape_sequence (:pattern "[^\\\\]?[^\\\\\"]+")))
                    "\"\"\"")
  escape_sequence (:token (:pattern "\\\\([nrt\"\\\\]|(\\r?\\n))"))
  _backticked (:seq "`" (:choice command_body :blank) "`")
  _indented_backticked (:seq "```" (:choice command_body :blank) "```")
  command_body (:repeat1 (:choice interpolation (:pattern ".")))
  interpolation (:seq "{{" expression "}}")
  identifier (:pattern "[a-zA-Z_][a-zA-Z0-9_-]*")
  numeric_error (:pattern "(\\d+\\.\\d*|\\d+)")
  comment (:token (:prec -1 (:pattern "#.*")))}}
