# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "astro"
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
             _html_interpolation_start
             _html_interpolation_end
             frontmatter_js_block
             attribute_js_expr
             attribute_backtick_string
             permissible_text
             _fragment_tag_delim]
 :inline []
 :supertypes []
 :rules
 {document (:seq (:choice frontmatter :blank) (:repeat _node))
  doctype (:seq "<!" (:alias _doctype "doctype") (:pattern "[^>]+") ">")
  _doctype (:pattern "[Dd][Oo][Cc][Tt][Yy][Pp][Ee]")
  _node (:choice
         doctype
         text
         element
         script_element
         style_element
         erroneous_end_tag
         html_interpolation)
  element (:choice
           (:seq start_tag (:repeat _node) (:choice end_tag _implicit_end_tag))
           self_closing_tag)
  script_element (:choice
                  (:seq (:alias script_start_tag start_tag) (:choice raw_text :blank) end_tag)
                  (:alias self_closing_script_tag self_closing_tag))
  style_element (:choice
                 (:seq (:alias style_start_tag start_tag) (:choice raw_text :blank) end_tag)
                 (:alias self_closing_style_tag self_closing_tag))
  start_tag (:seq
             "<"
             (:choice
              (:seq (:alias _start_tag_name tag_name) (:repeat attribute) ">")
              (:alias _fragment_tag_delim ">")))
  script_start_tag (:seq "<" (:alias _script_start_tag_name tag_name) (:repeat attribute) ">")
  style_start_tag (:seq "<" (:alias _style_start_tag_name tag_name) (:repeat attribute) ">")
  self_closing_tag (:seq "<" (:alias _start_tag_name tag_name) (:repeat attribute) "/>")
  end_tag (:seq
           "</"
           (:choice (:seq (:alias _end_tag_name tag_name) ">") (:alias _fragment_tag_delim ">")))
  erroneous_end_tag (:seq "</" erroneous_end_tag_name ">")
  attribute (:choice
             (:seq
              attribute_name
              (:choice (:seq "=" (:choice attribute_value quoted_attribute_value)) :blank))
             (:seq attribute_name "=" (:choice attribute_interpolation attribute_backtick_string))
             attribute_interpolation)
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
  text (:pattern "[^<>{}\\s]([^<>{}]*[^<>{}\\s])?")
  _node_with_permissible_text (:choice
                               doctype
                               element
                               script_element
                               style_element
                               html_interpolation
                               permissible_text)
  frontmatter (:seq (:token (:prec 1 "---")) (:choice frontmatter_js_block :blank) "---")
  attribute_interpolation (:seq (:token (:prec 1 "{")) attribute_js_expr "}")
  html_interpolation (:seq
                      (:alias _html_interpolation_start "{")
                      (:repeat _node_with_permissible_text)
                      (:alias _html_interpolation_end "}"))
  self_closing_script_tag (:seq
                           "<"
                           (:alias _script_start_tag_name tag_name)
                           (:repeat attribute)
                           "/>")
  self_closing_style_tag (:seq "<" (:alias _style_start_tag_name tag_name) (:repeat attribute) "/>")}}
