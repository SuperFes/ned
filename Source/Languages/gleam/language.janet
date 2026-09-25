{:name "gleam"
 :extensions [".gleam"]
 :line-comment "//"
 :lsp-root-markers ["gleam.toml"]
 :import-resolution {:extensions ["gleam"] :source-roots ["src" "test"]}
 :signature-template "fn ned_sig({}) { Nil }"
}
