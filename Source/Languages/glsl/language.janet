{:name "glsl"
 :extensions [".glsl" ".vert" ".frag" ".geom" ".comp" ".tesc" ".tese"]
 :injection-aliases ["frag" "vert"]
 :line-comment "//"
 :line-continuation "\\"
 :first-pattern-wins true
 :queries {:format ["c/format.janet"]
           :locals ["c/locals.janet"]
           :imports ["c/imports.janet"]
           :indents ["c/indents.janet"]
           :signatures ["c/signatures.janet"]
           :calls ["c/calls.janet"]}
 :signature-template "void __ned_sig({}) {}"
 :not-applicable {:injections        "nothing in it is written in another language"
                  :tests             "shaders have no test framework"
                  :style             "no canonical style; projects bring their own .clang-format"
                  :lsp-root          "no project file of its own; the root falls through to the project's"
                  :import-resolution "its specifiers are relative paths the default resolution handles"}
}
