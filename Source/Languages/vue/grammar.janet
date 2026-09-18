# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "vue"
 :inherits "html"
 :extras [comment (:pattern "\\s+")]
 :conflicts []
 :precedences []
 :externals [_start_tag_name
             _script_start_tag_name
             _style_start_tag_name
             _end_tag_name
             erroneous_end_tag_name
             "/>"
             _implicit_end_tag
             raw_text
             comment
             _template_start_tag_name
             _text_fragment
             _interpolation_text]
 :inline []
 :supertypes []
 :rules
 {document (:repeat _node)
  doctype (:seq "<!" (:alias _doctype "doctype") (:pattern "[^>]+") ">")
  _doctype (:pattern "[Dd][Oo][Cc][Tt][Yy][Pp][Ee]")
  _node (:choice
         (:choice doctype entity text element script_element style_element erroneous_end_tag)
         template_element
         interpolation)
  element (:choice
           (:seq start_tag (:repeat _node) (:choice end_tag _implicit_end_tag))
           self_closing_tag)
  script_element (:seq (:alias script_start_tag start_tag) (:choice raw_text :blank) end_tag)
  style_element (:seq (:alias style_start_tag start_tag) (:choice raw_text :blank) end_tag)
  start_tag (:seq "<" (:alias _start_tag_name tag_name) (:repeat _attribute) ">")
  script_start_tag (:seq "<" (:alias _script_start_tag_name tag_name) (:repeat _attribute) ">")
  style_start_tag (:seq "<" (:alias _style_start_tag_name tag_name) (:repeat _attribute) ">")
  self_closing_tag (:seq
                    "<"
                    (:alias (:choice _start_tag_name _template_start_tag_name) tag_name)
                    (:repeat _attribute)
                    "/>")
  end_tag (:seq "</" (:alias _end_tag_name tag_name) ">")
  erroneous_end_tag (:seq "</" erroneous_end_tag_name ">")
  attribute (:seq
             attribute_name
             (:choice (:seq "=" (:choice attribute_value quoted_attribute_value)) :blank))
  attribute_name (:pattern "[^<>\"'/=\\s]+")
  attribute_value (:pattern "[^<>\"'=\\s]+")
  entity (:pattern "&(#([xX][0-9a-fA-F]{1,6}|[0-9]{1,5})|[A-Za-z]{1,30});?")
  quoted_attribute_value (:choice
                          (:seq
                           "'"
                           (:choice (:alias (:pattern "[^']+") attribute_value) :blank)
                           "'")
                          (:seq
                           "\""
                           (:choice (:alias (:pattern "[^\"]+") attribute_value) :blank)
                           "\""))
  text _text_fragment
  template_element (:seq (:alias template_start_tag start_tag) (:repeat _node) end_tag)
  template_start_tag (:seq "<" (:alias _template_start_tag_name tag_name) (:repeat _attribute) ">")
  interpolation (:seq "{{" (:choice (:alias _interpolation_text raw_text) :blank) "}}")
  _attribute (:choice attribute directive_attribute)
  directive_attribute (:prec-right 0
                       (:seq
                        (:choice
                         (:seq
                          directive_name
                          (:choice
                           (:seq
                            (:choice (:token-immediate (:prec 1 ":")))
                            (:choice directive_value dynamic_directive_value))
                           :blank))
                         (:repeat1
                          (:seq
                           (:choice
                            (:token (:prec 1 ":"))
                            (:token (:prec 1 "."))
                            (:token (:prec 1 "@"))
                            (:token (:prec 1 "#")))
                           (:choice directive_value dynamic_directive_value))))
                        (:choice directive_modifiers :blank)
                        (:choice (:seq "=" (:choice attribute_value quoted_attribute_value)) :blank)))
  directive_name (:token (:prec 1 (:pattern "v-[^<>'\"=/\\s:.]+")))
  directive_value (:pattern "[^<>\"'/=\\s:.]+")
  dynamic_directive_value (:seq
                           (:token-immediate (:prec 1 "["))
                           (:choice dynamic_directive_inner_value :blank)
                           (:token-immediate "]"))
  dynamic_directive_inner_value (:token-immediate (:pattern "[^<>\"'/=\\s\\]]+"))
  directive_modifiers (:repeat1 (:seq (:token-immediate (:prec 1 ".")) directive_modifier))
  directive_modifier (:token-immediate (:pattern "[^<>\"'/=\\s.]+"))}}
