{:name "glsl"
 :extensions [".glsl" ".vert" ".frag" ".geom" ".comp" ".tesc" ".tese"]
 :injection-aliases ["frag" "vert"]
 :line-comment "//"
 :line-continuation "\\"
 :first-pattern-wins true
 :queries {:imports ["c/imports.janet"]
           :indents ["c/indents.janet"]
           :signatures ["c/signatures.janet"]
           :calls ["c/calls.janet"]}
 :signature-template "void __ned_sig({}) {}"
}
