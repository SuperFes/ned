{:name "rust"
 :extensions [".rs"]
 :injection-aliases ["rs"]
 :line-comment "//"
 :lsp-root-markers ["Cargo.toml"]

 # Only ever consulted for Rust's own "mod foo;" file-per-module
 # declaration (@import.moddecl) -- "foo" tried as "foo.rs" or "foo/mod.rs".
 :import-resolution {:extensions ["rs"] :index-basenames ["mod"]}
 :signature-template "fn __ned_sig({}) {}"
 :not-applicable {:injections "nothing in it is written in another language"}
}
