{:name "glsl"
 :extensions [".glsl" ".vert" ".frag" ".geom" ".comp" ".tesc" ".tese"]
 :line-comment "//"
 :first-pattern-wins true
 :queries {:signatures ["c/signatures.janet"]
           :calls ["c/calls.janet"]}
 :signature-template "void __ned_sig({}) {}"
}
