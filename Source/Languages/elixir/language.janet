{:name "elixir"
 :extensions [".ex" ".exs"]
 :injection-aliases ["ex" "exs"]
 :line-comment "#"
 :signature-template "def ned_sig({}), do: nil"
 :signature-clauses true
 :lsp-root-markers ["mix.exs"]
 :import-resolution {:extensions ["ex" "exs"] :snake-case-steps true :declared-names true :source-roots ["lib" "test/support"]}
}
