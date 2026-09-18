# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "html"
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
             comment]
 :inline []
 :supertypes []
 :rules
 {document (:repeat _node)
  doctype (:seq "<!" (:alias _doctype "doctype") (:pattern "[^>]+") ">")
  _doctype (:pattern "[Dd][Oo][Cc][Tt][Yy][Pp][Ee]")
  _node (:choice doctype entity text element script_element style_element erroneous_end_tag)
  element (:choice
           (:seq start_tag (:repeat _node) (:choice end_tag _implicit_end_tag))
           self_closing_tag)
  script_element (:seq (:alias script_start_tag start_tag) (:choice raw_text :blank) end_tag)
  style_element (:seq (:alias style_start_tag start_tag) (:choice raw_text :blank) end_tag)
  start_tag (:seq "<" (:alias _start_tag_name tag_name) (:repeat attribute) ">")
  script_start_tag (:seq "<" (:alias _script_start_tag_name tag_name) (:repeat attribute) ">")
  style_start_tag (:seq "<" (:alias _style_start_tag_name tag_name) (:repeat attribute) ">")
  self_closing_tag (:seq "<" (:alias _start_tag_name tag_name) (:repeat attribute) "/>")
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
  text (:pattern "[^<>&\\s]([^<>&]*[^<>&\\s])?")}}
