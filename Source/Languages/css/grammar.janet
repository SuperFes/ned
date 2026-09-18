# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "css"
 :extras [(:pattern "\\s") comment js_comment]
 :conflicts []
 :precedences []
 :externals [_descendant_operator _pseudo_class_selector_colon __error_recovery]
 :inline [_top_level_item _block_item]
 :supertypes []
 :rules
 {stylesheet (:repeat _top_level_item)
  _top_level_item (:choice
                   declaration
                   rule_set
                   import_statement
                   media_statement
                   charset_statement
                   namespace_statement
                   keyframes_statement
                   supports_statement
                   scope_statement
                   at_rule)
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
  keyframe_block (:seq
                  (:seq
                   (:choice from to integer_value)
                   (:repeat (:seq "," (:choice from to integer_value))))
                  block)
  from "from"
  to "to"
  supports_statement (:seq "@supports" _query block)
  scope_statement (:seq
                   "@scope"
                   (:choice
                    (:seq "(" _selector ")" (:choice (:seq "to" "(" _selector ")") :blank))
                    :blank)
                   block)
  postcss_statement (:prec -1 (:seq at_keyword (:repeat (:choice _value important_value)) ";"))
  at_rule (:seq
           at_keyword
           (:choice (:seq _query (:repeat (:seq "," _query))) :blank)
           (:choice ";" block))
  rule_set (:seq selectors block)
  selectors (:seq _selector (:repeat (:seq "," _selector)))
  block (:seq "{" (:repeat _block_item) (:choice (:alias last_declaration declaration) :blank) "}")
  _block_item (:choice
               declaration
               rule_set
               import_statement
               media_statement
               charset_statement
               namespace_statement
               keyframes_statement
               supports_statement
               scope_statement
               postcss_statement
               at_rule)
  _selector (:choice
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
  nesting_selector "&"
  universal_selector "*"
  class_selector (:prec 1 (:seq (:choice _selector :blank) "." class_name))
  pseudo_class_selector (:seq
                         (:choice _selector :blank)
                         (:alias _pseudo_class_selector_colon ":")
                         (:choice
                          (:seq
                           (:alias
                            (:choice "has" "not" "is" "where" "host" "host-context")
                            class_name)
                           (:alias pseudo_class_with_selector_arguments arguments))
                          _nth_child_pseudo_class_selector
                          (:seq
                           class_name
                           (:choice (:alias pseudo_class_arguments arguments) :blank))
                          (:alias "host" class_name)))
  _nth_child_pseudo_class_selector (:seq
                                    (:alias (:choice "nth-child" "nth-last-child") class_name)
                                    (:alias pseudo_class_nth_child_arguments arguments))
  pseudo_element_selector (:seq
                           (:choice _selector :blank)
                           "::"
                           (:alias identifier tag_name)
                           (:choice (:alias pseudo_element_arguments arguments) :blank))
  id_selector (:seq (:choice _selector :blank) "#" (:alias identifier id_name))
  attribute_selector (:seq
                      (:choice _selector :blank)
                      (:token (:prec 1 "["))
                      (:alias (:choice identifier namespace_selector) attribute_name)
                      (:choice (:seq (:choice "=" "~=" "^=" "|=" "*=" "$=") _value) :blank)
                      "]")
  child_selector (:prec-left 0 (:seq (:choice _selector :blank) ">" _selector))
  descendant_selector (:prec-left 0 (:seq _selector _descendant_operator _selector))
  sibling_selector (:prec-left 0 (:seq (:choice _selector :blank) "~" _selector))
  adjacent_sibling_selector (:prec-left 0 (:seq (:choice _selector :blank) "+" _selector))
  namespace_selector (:prec-left 0 (:seq (:choice _selector :blank) "|" _selector))
  pseudo_class_arguments (:seq
                          (:token-immediate "(")
                          (:choice
                           (:seq
                            (:choice _selector (:repeat1 _value))
                            (:repeat (:seq "," (:choice _selector (:repeat1 _value)))))
                           :blank)
                          ")")
  pseudo_class_with_selector_arguments (:seq
                                        (:token-immediate "(")
                                        (:choice
                                         (:seq _selector (:repeat (:seq "," _selector)))
                                         :blank)
                                        ")")
  pseudo_class_nth_child_arguments (:prec -1
                                    (:seq
                                     (:token-immediate "(")
                                     (:choice
                                      (:alias "even" plain_value)
                                      (:alias "odd" plain_value)
                                      integer_value
                                      (:alias _nth_functional_notation plain_value))
                                     (:choice (:seq "of" _selector) :blank)
                                     ")"))
  _nth_functional_notation (:pattern "-?(\\d)*n\\s*(\\+\\s*\\d+)?")
  pseudo_element_arguments (:seq
                            (:token-immediate "(")
                            (:choice
                             (:seq
                              (:choice _selector (:repeat1 _value))
                              (:repeat (:seq "," (:choice _selector (:repeat1 _value)))))
                             :blank)
                            ")")
  declaration (:seq
               (:alias identifier property_name)
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
          (:alias identifier keyword_query)
          feature_query
          binary_query
          unary_query
          selector_query
          parenthesized_query)
  feature_query (:seq "(" (:alias identifier feature_name) ":" (:repeat1 _value) ")")
  parenthesized_query (:seq "(" _query ")")
  binary_query (:prec-left 0 (:seq _query (:choice "and" "or") _query))
  unary_query (:prec 1 (:seq (:choice "not" "only") _query))
  selector_query (:seq "selector" "(" _selector ")")
  _value (:prec -1
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
  parenthesized_value (:seq "(" _value ")")
  color_value (:seq "#" (:token-immediate (:pattern "[0-9a-fA-F]{3,8}")))
  string_value (:choice
                (:seq
                 "'"
                 (:repeat
                  (:choice (:alias (:pattern "[^\\\\'\\n]+") string_content) escape_sequence))
                 "'")
                (:seq
                 "\""
                 (:repeat
                  (:choice (:alias (:pattern "[^\\\\\"\\n]+") string_content) escape_sequence))
                 "\""))
  escape_sequence (:token
                   (:seq
                    "\\"
                    (:choice (:pattern "[0-9a-fA-F]{1,6}\\s?") (:pattern "[^0-9a-fA-F\\n\\r]"))))
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
  call_expression (:seq (:alias identifier function_name) arguments)
  binary_expression (:prec-left 0 (:seq _value (:choice "+" "-" "*" "/") _value))
  arguments (:seq
             (:token-immediate "(")
             (:choice
              (:seq (:repeat1 _value) (:repeat (:seq (:choice "," ";") (:repeat1 _value))))
              :blank)
             ")")
  class_name (:seq
              (:choice identifier escape_sequence)
              (:repeat
               (:choice (:alias (:pattern "[a-zA-Z0-9-_\\xA0-\\xFF]+") identifier) escape_sequence)))
  identifier (:pattern "(--|-?[a-zA-Z_\\xA0-\\xFF])[a-zA-Z0-9-_\\xA0-\\xFF]*")
  at_keyword (:pattern "@[a-zA-Z-_]+")
  js_comment (:token (:prec -1 (:seq "//" (:pattern ".*"))))
  comment (:token (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/"))
  plain_value (:token
               (:seq
                (:repeat (:choice (:pattern "[-_]") (:pattern "\\/[^\\*\\s,;!{}()\\[\\]]")))
                (:pattern "[a-zA-Z]")
                (:repeat
                 (:choice (:pattern "[^/\\s,;!{}()\\[\\]]") (:pattern "\\/[^\\*\\s,;!{}()\\[\\]]")))))
  important_value (:token (:seq "!" (:pattern "[a-zA-Z]") (:repeat (:pattern "[a-zA-Z0-9-_]"))))}}
