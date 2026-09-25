{:name "hlsl"
 :extensions [".hlsl" ".hlsli" ".fx" ".fxh"]
 :line-comment "//"
 :line-continuation "\\"
 # The grammar extends cpp's and upstream ships no queries, so cpp's are
 # named here. Not :queries-from: cpp's tests, imports, signatures and format
 # queries mean nothing for a shader, and tags.janet stays HLSL's own.
 :queries {:format ["cpp/format.janet"]
           :imports ["c/imports.janet"]
           :highlights ["cpp/highlights.janet"]
           :locals ["cpp/locals.janet"]
           :indents ["cpp/indents.janet"]
           :signatures ["cpp/signatures.janet"]
           :calls ["cpp/calls.janet"]}
 :signature-template "void __ned_sig({}) {}"
 :not-applicable {:injections        "nothing in it is written in another language"
                  :tests             "shaders have no test framework"
                  :style             "no canonical style; projects bring their own .clang-format"
                  :lsp-root          "no project file of its own; the root falls through to the project's"
                  :import-resolution "its specifiers are relative paths the default resolution handles"}
}
