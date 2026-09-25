{:name "elm"
 :extensions [".elm"]
 :preserve-indent true
 :line-comment "--"
 :lsp-root-markers ["elm.json"]
 # Upstream names its captures after TextMate scopes.
 :capture-classes {"local.function" :variable
                   "meta.import" :keyword
                   "storage.type" :type
                   "union" :constructor
                   "char" :string
                   "source.glsl" :default}
 :import-resolution {:extensions ["elm"] :source-roots ["src"]}
 :not-applicable {:continuation "indentation is syntax (`:preserve-indent`)"}
}
