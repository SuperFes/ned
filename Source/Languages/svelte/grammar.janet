# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "svelte"
 :inherits "html"
 :extras [comment (:pattern "\\s+")]
 :conflicts [[else_if_block] [else_block] [then_block] [catch_block]]
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
             svelte_raw_text
             svelte_raw_text_each
             svelte_raw_text_snippet_arguments
             "@"
             "#"
             "/"
             ":"]
 :inline []
 :supertypes [_node]
 :rules
 {document (:repeat _node)
  doctype (:seq "<!" (:alias _doctype "doctype") (:pattern "[^>]+") ">")
  _doctype (:pattern "[Dd][Oo][Cc][Tt][Yy][Pp][Ee]")
  _node (:choice
         (:choice doctype entity text element script_element style_element erroneous_end_tag)
         if_statement
         each_statement
         await_statement
         key_statement
         snippet_statement
         expression
         html_tag
         const_tag
         debug_tag
         render_tag)
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
             (:choice
              (:seq
               attribute_name
               (:choice
                (:seq "=" (:choice attribute_value quoted_attribute_value expression))
                :blank))
              (:alias expression attribute_name)))
  attribute_name (:pattern "[^<>{}\"'/=\\s]+")
  attribute_value (:pattern "[^<>{}\"'=\\s]+")
  entity (:pattern "&(#([xX][0-9a-fA-F]{1,6}|[0-9]{1,5})|[A-Za-z]{1,30});?")
  quoted_attribute_value (:choice
                          (:seq
                           "'"
                           (:choice (:alias _single_quoted_attribute_value attribute_value) :blank)
                           "'")
                          (:seq
                           "\""
                           (:choice (:alias _double_quoted_attribute_value attribute_value) :blank)
                           "\""))
  text (:pattern "[^<>{}&\\s]([^<>{}&]*[^<>{}&\\s])?")
  _single_quoted_attribute_value (:repeat1 (:choice (:pattern "[^{']+") expression))
  _double_quoted_attribute_value (:repeat1 (:choice (:pattern "[^{\"]+") expression))
  if_statement (:seq
                if_start
                (:repeat _node)
                (:repeat else_if_block)
                (:choice else_block :blank)
                if_end)
  _if_start_tag (:seq "#" (:field :tag (:token-immediate "if")))
  if_start (:seq "{" (:alias _if_start_tag block_start_tag) (:field :condition svelte_raw_text) "}")
  _else_if_tag (:seq ":" (:field :tag (:token-immediate "else if")))
  else_if_start (:seq "{" (:alias _else_if_tag block_tag) (:field :condition svelte_raw_text) "}")
  else_if_block (:seq else_if_start (:repeat _node))
  _else_tag (:seq ":" (:field :tag (:token-immediate "else")))
  else_start (:seq "{" (:alias _else_tag block_tag) "}")
  else_block (:seq else_start (:repeat _node))
  _if_end_tag (:seq "/" (:field :tag (:token-immediate "if")))
  if_end (:seq "{" (:alias _if_end_tag block_end_tag) "}")
  each_statement (:seq
                  each_start
                  (:repeat _node)
                  (:choice (:seq else_block (:repeat _node)) :blank)
                  each_end)
  _each_start_tag (:seq "#" (:field :tag (:token-immediate "each")))
  each_start (:seq
              "{"
              (:alias _each_start_tag block_start_tag)
              (:choice
               (:field :identifier svelte_raw_text)
               (:seq
                (:field :identifier (:alias svelte_raw_text_each svelte_raw_text))
                "as"
                (:field :parameter svelte_raw_text)))
              "}")
  _each_end_tag (:seq "/" (:field :tag (:token-immediate "each")))
  each_end (:seq "{" (:alias _each_end_tag block_end_tag) "}")
  await_statement (:seq
                   await_start
                   (:repeat _node)
                   (:choice then_block :blank)
                   (:choice catch_block :blank)
                   await_end)
  _await_start_tag (:seq "#" (:field :tag (:token-immediate "await")))
  await_start (:seq "{" (:alias _await_start_tag block_start_tag) svelte_raw_text "}")
  _then_tag (:seq ":" (:field :tag (:token-immediate "then")))
  then_start (:seq "{" (:alias _then_tag block_tag) (:choice svelte_raw_text :blank) "}")
  then_block (:seq then_start (:repeat _node))
  _catch_tag (:seq ":" (:field :tag (:token-immediate "catch")))
  catch_start (:seq "{" (:alias _catch_tag block_tag) (:choice svelte_raw_text :blank) "}")
  catch_block (:seq catch_start (:repeat _node))
  _await_end_tag (:seq "/" (:field :tag (:token-immediate "await")))
  await_end (:seq "{" (:alias _await_end_tag block_end_tag) "}")
  key_statement (:seq key_start (:repeat _node) key_end)
  _key_start_tag (:seq "#" (:field :tag (:token-immediate "key")))
  key_start (:seq "{" (:alias _key_start_tag block_start_tag) svelte_raw_text "}")
  _key_end_tag (:seq "/" (:field :tag (:token-immediate "key")))
  key_end (:seq "{" (:alias _key_end_tag block_end_tag) "}")
  snippet_statement (:seq snippet_start (:repeat _node) snippet_end)
  _snippet_start_tag (:seq "#" (:field :tag (:token-immediate "snippet")))
  snippet_start (:seq
                 "{"
                 (:alias _snippet_start_tag block_start_tag)
                 (:alias (:pattern "[a-zA-Z$_][a-zA-Z0-9_]*") snippet_name)
                 "("
                 (:choice (:alias svelte_raw_text_snippet_arguments svelte_raw_text) :blank)
                 ")"
                 "}")
  _snippet_end_tag (:seq "/" (:field :tag (:token-immediate "snippet")))
  snippet_end (:seq "{" (:alias _snippet_end_tag block_end_tag) "}")
  expression (:seq "{" svelte_raw_text "}")
  _tag_value (:seq (:pattern "\\s+") svelte_raw_text)
  _html_tag (:seq "@" (:field :tag (:token-immediate "html")))
  html_tag (:seq "{" (:alias _html_tag expression_tag) _tag_value "}")
  _const_tag (:seq "@" (:field :tag (:token-immediate "const")))
  const_tag (:seq "{" (:alias _const_tag expression_tag) _tag_value "}")
  _debug_tag (:seq "@" (:field :tag (:token-immediate "debug")))
  debug_tag (:seq "{" (:alias _debug_tag expression_tag) _tag_value "}")
  _render_tag (:seq "@" (:field :tag (:token-immediate "render")))
  render_tag (:seq "{" (:alias _render_tag expression_tag) _tag_value "}")}}
