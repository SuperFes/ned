; go-to-file-at-point resolver gaps follow-up: checked against
; tree-sitter-rust's own node-types.json. Only a body-less "mod foo;"
; declaration is matched -- Rust's own file-per-module convention (a
; declared-but-not-inline module resolves to a real source file, "foo.rs" or
; "foo/mod.rs"). A real "use foo::bar::Baz;" path is deliberately NOT
; matched: resolving one to a file needs semantic knowledge of the whole
; crate's own module tree (which file declared which "mod", "pub use"
; re-exports, external-crate dependencies living in Cargo's own registry)
; that a single-file, syntax-only query has no way to reconstruct -- exactly
; the job rust-analyzer's own textDocument/definition already does
; correctly via lsp-goto-definition (Cargo.toml is this language's own
; LspRootMarkers entry, see LspRootResolver.cpp). "!body" is a real, negated
; -field tree-sitter query anchor (not a custom predicate) -- already
; proven working against this exact grammar in its own tags.scm ("!trait" on
; impl_item).
(mod_item
  name: (identifier) @import.moddecl
  !body) @import.statement
