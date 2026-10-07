{:name "crystal"
 :icon "" # nf-seti-crystal
 :icon-color "#c8c8c8"
 :extensions [".cr"]
 :injection-aliases ["cr"]
 :line-comment "#"
 :lsp-root-markers ["shard.yml"]
 :signature-template "def __ned_sig({})\nend"
 :import-resolution {:extensions ["cr"] :source-roots ["src" "lib"]}
}
