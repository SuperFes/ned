# A heredoc is written in the language its delimiter names (`<<~SQL`,
# `<<-BASH`); a delimiter naming no language (`EOS`) injects nothing.
(heredoc_body
  (heredoc_content) @injection.content
  (heredoc_end) @injection.language)
