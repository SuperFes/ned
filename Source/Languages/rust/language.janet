{:name "rust"
 :extensions [".rs"]
 :line-comment "//"
 :lsp-root-markers ["Cargo.toml"]

 # Only ever consulted for Rust's own "mod foo;" file-per-module
 # declaration (@import.moddecl) -- "foo" tried as "foo.rs" or "foo/mod.rs".
 :import-resolution {:extensions ["rs"] :index-basenames ["mod"]}
}
