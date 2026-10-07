{:name "gleam"
 :icon "" # nf-dev-gleam
 :icon-color "#ffaff3"
 :extensions [".gleam"]
 :line-comment "//"
 :lsp-root-markers ["gleam.toml"]
 :import-resolution {:extensions ["gleam"] :source-roots ["src" "test"]}
 :signature-template "fn ned_sig({}) { Nil }"
}
