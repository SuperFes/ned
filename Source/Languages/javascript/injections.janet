# Tagged template literals written in another language: html`...`,
# css`...`, sql`...`, svg`...`, and styled-components/emotion's
# styled.div`...` / styled(Button)`...` / keyframes`...` bodies, which are
# CSS declarations. The offset trims the backticks; a `${...}` substitution
# is parsed as part of the embedded text.
((call_expression
   function: (identifier) @injection.language
   arguments: (template_string) @injection.content)
 (:match? @injection.language "^(html|css|sql|svg|SQL)$")
 (:offset! @injection.content 0 1 0 -1))

((call_expression
   function: [(member_expression object: (identifier) @_styled)
              (call_expression function: (identifier) @_styled)
              (call_expression function: (member_expression object: (identifier) @_styled))
              (call_expression function: (member_expression object: (member_expression object: (identifier) @_styled)))]
   arguments: (template_string) @injection.content)
 (:eq? @_styled "styled")
 (:offset! @injection.content 0 1 0 -1)
 (:set! injection.language "css"))

((call_expression
   function: (identifier) @_fn
   arguments: (template_string) @injection.content)
 (:any-of? @_fn "keyframes" "createGlobalStyle" "injectGlobal")
 (:offset! @injection.content 0 1 0 -1)
 (:set! injection.language "css"))
