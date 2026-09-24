{:name "hlsl"
 :extensions [".hlsl" ".hlsli" ".fx" ".fxh"]
 :line-comment "//"
 # The grammar extends cpp's and upstream ships no queries, so cpp's are
 # named here. Not :queries-from: cpp's tests, imports, signatures and format
 # queries mean nothing for a shader, and tags.janet stays HLSL's own.
 :queries {:highlights ["cpp/highlights.janet"]
           :locals ["cpp/locals.janet"]
           :indents ["cpp/indents.janet"]
           :signatures ["cpp/signatures.janet"]
           :calls ["cpp/calls.janet"]}
 :signature-template "void __ned_sig({}) {}"
}
