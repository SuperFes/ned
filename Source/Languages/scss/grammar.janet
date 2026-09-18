# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "scss"
 :extras [(:pattern "\\s") comment js_comment]
 :conflicts []
 :precedences []
 :externals [_descendant_operator _pseudo_class_selector_colon __error_recovery _concat]
 :inline [_top_level_item _block_item]
 :supertypes []
 :rules
 {stylesheet (:repeat _top_level_item)
  _top_level_item (:choice
                   (:choice
                    declaration
                    rule_set
                    import_statement
                    media_statement
                    charset_statement
                    namespace_statement
                    keyframes_statement
                    supports_statement
                    at_rule)
                   postcss_statement
                   use_statement
                   forward_statement
                   mixin_statement
                   include_statement
                   function_statement
                   return_statement
                   extend_statement
                   error_statement
                   warn_statement
                   debug_statement
                   at_root_statement
                   if_statement
                   each_statement
                   for_statement
                   while_statement)
  import_statement (:seq
                    "@import"
                    _value
                    (:choice (:seq _query (:repeat (:seq "," _query))) :blank)
                    ";")
  media_statement (:seq "@media" (:seq _query (:repeat (:seq "," _query))) block)
  charset_statement (:seq "@charset" _value ";")
  namespace_statement (:seq
                       "@namespace"
                       (:choice (:alias identifier namespace_name) :blank)
                       (:choice string_value call_expression)
                       ";")
  keyframes_statement (:seq
                       (:choice "@keyframes" (:alias (:pattern "@[-a-z]+keyframes") at_keyword))
                       (:alias identifier keyframes_name)
                       keyframe_block_list)
  keyframe_block_list (:seq "{" (:repeat keyframe_block) "}")
  keyframe_block (:seq (:choice from to integer_value) block)
  from "from"
  to "to"
  supports_statement (:seq "@supports" _query block)
  postcss_statement (:prec -1 (:seq at_keyword (:repeat _value) ";"))
  at_rule (:seq
           at_keyword
           (:choice (:seq _query (:repeat (:seq "," _query))) :blank)
           (:choice ";" block))
  rule_set (:seq selectors block)
  selectors (:seq _selector (:repeat (:seq "," _selector)))
  block (:seq "{" (:repeat _block_item) (:choice (:alias last_declaration declaration) :blank) "}")
  _block_item (:choice
               (:choice
                declaration
                rule_set
                import_statement
                media_statement
                charset_statement
                namespace_statement
                keyframes_statement
                supports_statement
                postcss_statement
                at_rule)
               mixin_statement
               include_statement
               function_statement
               return_statement
               extend_statement
               error_statement
               warn_statement
               debug_statement
               at_root_statement
               if_statement
               each_statement
               for_statement
               while_statement)
  _selector (:choice
             (:choice
              universal_selector
              (:alias identifier tag_name)
              class_selector
              nesting_selector
              pseudo_class_selector
              pseudo_element_selector
              id_selector
              attribute_selector
              string_value
              child_selector
              descendant_selector
              sibling_selector
              adjacent_sibling_selector
              namespace_selector)
             (:alias _concatenated_identifier tag_name)
             placeholder)
  nesting_selector "&"
  universal_selector "*"
  class_selector (:prec 1
                  (:seq
                   (:choice _selector :blank)
                   (:choice "." nesting_selector)
                   (:alias (:choice identifier _concatenated_identifier) class_name)))
  pseudo_class_selector (:seq
                         (:choice _selector :blank)
                         (:alias _pseudo_class_selector_colon ":")
                         (:alias (:choice identifier _concatenated_identifier) class_name)
                         (:choice (:alias pseudo_class_arguments arguments) :blank))
  pseudo_element_selector (:seq
                           (:choice _selector :blank)
                           "::"
                           (:alias identifier tag_name)
                           (:choice (:alias pseudo_element_arguments arguments) :blank))
  id_selector (:seq (:choice _selector :blank) "#" (:alias identifier id_name))
  attribute_selector (:seq
                      (:choice _selector :blank)
                      "["
                      (:alias (:choice identifier namespace_selector) attribute_name)
                      (:choice (:seq (:choice "=" "~=" "^=" "|=" "*=" "$=") _value) :blank)
                      "]")
  child_selector (:prec-left 0 (:seq _selector ">" _selector))
  descendant_selector (:prec-left 0 (:seq _selector _descendant_operator _selector))
  sibling_selector (:prec-left 0 (:seq _selector "~" _selector))
  adjacent_sibling_selector (:prec-left 0 (:seq _selector "+" _selector))
  namespace_selector (:prec-left 0 (:seq _selector "|" _selector))
  pseudo_class_arguments (:seq
                          (:token-immediate "(")
                          (:choice
                           (:seq
                            (:choice _selector (:repeat1 _value))
                            (:repeat (:seq "," (:choice _selector (:repeat1 _value)))))
                           :blank)
                          ")")
  pseudo_element_arguments (:seq
                            (:token-immediate "(")
                            (:choice
                             (:seq
                              (:choice _selector (:repeat1 _value))
                              (:repeat (:seq "," (:choice _selector (:repeat1 _value)))))
                             :blank)
                            ")")
  declaration (:seq
               (:alias (:choice identifier variable _concatenated_identifier) property_name)
               ":"
               _value
               (:repeat (:seq (:choice "," :blank) _value))
               (:choice important :blank)
               ";")
  last_declaration (:prec 1
                    (:seq
                     (:alias identifier property_name)
                     ":"
                     _value
                     (:repeat (:seq (:choice "," :blank) _value))
                     (:choice important :blank)))
  important "!important"
  _query (:choice
          (:choice
           (:alias identifier keyword_query)
           feature_query
           binary_query
           unary_query
           selector_query
           parenthesized_query)
          (:prec -1 interpolation))
  feature_query (:seq "(" (:alias identifier feature_name) ":" (:repeat1 _value) ")")
  parenthesized_query (:seq "(" _query ")")
  binary_query (:prec-left 0 (:seq _query (:choice "and" "or") _query))
  unary_query (:prec 1 (:seq (:choice "not" "only") _query))
  selector_query (:seq "selector" "(" _selector ")")
  _value (:choice
          (:prec -1
           (:choice
            (:alias identifier plain_value)
            plain_value
            color_value
            integer_value
            float_value
            string_value
            grid_value
            binary_expression
            parenthesized_value
            call_expression
            important))
          (:prec -1 (:choice nesting_selector _concatenated_identifier list_value))
          variable)
  parenthesized_value (:seq "(" _value ")")
  color_value (:seq "#" (:token-immediate (:pattern "[0-9a-fA-F]{3,8}")))
  string_value (:choice
                (:seq "'" (:pattern "([^'\\n]|\\\\(.|\\n))*") "'")
                (:seq "\"" (:pattern "([^\"\\n]|\\\\(.|\\n))*") "\""))
  integer_value (:seq
                 (:token (:seq (:choice (:choice "+" "-") :blank) (:pattern "\\d+")))
                 (:choice unit :blank))
  float_value (:seq
               (:token
                (:seq
                 (:choice (:choice "+" "-") :blank)
                 (:pattern "\\d*")
                 (:choice
                  (:seq "." (:pattern "\\d+"))
                  (:seq (:pattern "[eE]") (:choice "-" :blank) (:pattern "\\d+"))
                  (:seq
                   "."
                   (:pattern "\\d+")
                   (:pattern "[eE]")
                   (:choice "-" :blank)
                   (:pattern "\\d+")))))
               (:choice unit :blank))
  unit (:token-immediate (:pattern "[a-zA-Z%]+"))
  grid_value (:seq "[" (:seq _value (:repeat (:seq "," _value))) "]")
  call_expression (:seq (:alias (:choice identifier plain_value) function_name) arguments)
  binary_expression (:prec-left 0
                     (:seq _value (:choice "+" "-" "*" "/" "==" "<" ">" "!=" "<=" ">=") _value))
  arguments (:seq
             (:token-immediate "(")
             (:choice
              (:seq (:repeat1 _value) (:repeat (:seq (:choice "," ";") (:repeat1 _value))))
              :blank)
             ")")
  identifier (:pattern "(--|-?[a-zA-Z_])[a-zA-Z0-9-_]*")
  at_keyword (:pattern "@[a-zA-Z-_]+")
  js_comment (:token (:prec -1 (:seq "//" (:pattern ".*"))))
  comment (:token (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/"))
  plain_value (:token
               (:seq
                (:repeat (:choice (:pattern "[-_]") (:pattern "\\/[^\\*\\s,;!{}()\\[\\]]")))
                (:pattern "[a-zA-Z]")
                (:repeat
                 (:choice (:pattern "[^/\\s,;!{}()\\[\\]]") (:pattern "\\/[^\\*\\s,;!{}()\\[\\]]")))))
  use_statement (:seq "@use" _value ";")
  forward_statement (:seq "@forward" _value ";")
  mixin_statement (:seq "@mixin" (:field :name identifier) (:choice parameters :blank) block)
  include_statement (:seq
                     "@include"
                     identifier
                     (:choice (:alias _include_arguments arguments) :blank)
                     (:choice block ";"))
  _include_arguments (:seq
                      (:token-immediate "(")
                      (:seq
                       (:alias _include_argument argument)
                       (:repeat (:seq "," (:alias _include_argument argument))))
                      (:token-immediate ")"))
  _include_argument (:seq
                     (:choice (:seq (:field :name variable) ":") :blank)
                     (:field :value _value))
  function_statement (:seq "@function" (:field :name identifier) (:choice parameters :blank) block)
  parameters (:seq "(" (:seq parameter (:repeat (:seq "," parameter))) ")")
  parameter (:seq variable (:choice (:seq ":" (:field :default _value)) :blank))
  return_statement (:seq "@return" _value ";")
  extend_statement (:seq "@extend" (:choice _value class_selector) ";")
  error_statement (:seq "@error" _value ";")
  warn_statement (:seq "@warn" _value ";")
  debug_statement (:seq "@debug" _value ";")
  at_root_statement (:seq "@at-root" _value block)
  if_statement (:seq
                "@if"
                (:field :condition _value)
                block
                (:repeat else_if_clause)
                (:choice else_clause :blank))
  else_if_clause (:seq "@else" "if" (:field :condition _value) block)
  else_clause (:seq "@else" block)
  each_statement (:seq
                  "@each"
                  (:choice (:seq (:field :key variable) ",") :blank)
                  (:field :value variable)
                  "in"
                  _value
                  block)
  for_statement (:seq
                 "@for"
                 variable
                 "from"
                 (:field :from _value)
                 "through"
                 (:field :through _value)
                 block)
  while_statement (:seq "@while" _value block)
  list_value (:seq "(" (:seq _value (:repeat1 (:seq "," _value))) ")")
  interpolation (:seq "#{" _value "}")
  placeholder (:seq "%" identifier)
  _concatenated_identifier (:choice
                            (:seq
                             identifier
                             (:repeat1
                              (:seq
                               _concat
                               (:choice
                                interpolation
                                identifier
                                (:alias (:token-immediate "-") identifier)))))
                            (:seq
                             interpolation
                             (:repeat
                              (:seq
                               _concat
                               (:choice
                                interpolation
                                identifier
                                (:alias (:token-immediate "-") identifier))))))
  variable (:pattern "([a-zA-Z_]+\\.)?\\$[a-zA-Z-_][a-zA-Z0-9-_]*")}}
