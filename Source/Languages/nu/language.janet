{:name "nu"
 :extensions [".nu"]
 :injection-aliases ["nushell"]
 :line-comment "#"
 :capture-classes {"special" :variable-builtin}
 :import-resolution {:extensions ["nu"] :index-basenames ["mod"]}
 :not-applicable {:lsp-root "no project file of its own; the root falls through to the project's"
                  :indents  "blocks and parentheses are its whole indent structure; a statement continues only inside them"}
}
