{:name "cuda"
 :extensions [".cu" ".cuh"]
 :injection-aliases ["cu"]
 :line-comment "//"
 # The upstream query is a delta whose first line says `inherits: cpp`;
 # discovery doesn't read that, so the base is named here.
 :queries {:highlights ["cpp/highlights.janet"
                        "cuda/upstream/highlights.janet"]
           :signatures ["cpp/signatures.janet"]
           :calls ["cpp/calls.janet"]}
 :signature-template "void __ned_sig({}) {}"
}
