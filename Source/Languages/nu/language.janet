{:name "nu"
 :extensions [".nu"]
 :injection-aliases ["nushell"]
 :line-comment "#"
 :signature-template "def __ned_sig [{}] {}"
 :braces-on-header-line true
 :capture-classes {"special" :variable-builtin}
 :import-resolution {:extensions ["nu"] :index-basenames ["mod"]}
 :not-applicable {:lsp-root "no project file of its own; the root falls through to the project's"
                  :indents  "blocks and parentheses are its whole indent structure; a statement continues only inside them"
                  :style    "no canonical formatter or official style rule for the braces and blank lines ned's rules cover"}
}
