{:name "proto"
 :extensions [".proto"]
 :injection-aliases ["protobuf"]
 :line-comment "//"
 :lsp-root-markers ["buf.yaml" "buf.work.yaml"]
 :import-resolution {:extensions ["proto"] :source-roots ["proto" "protos"]}
 :not-applicable {:continuation "no operator expressions to continue"
                  :locals       "names are package-global across files"
                  :tests        "no test framework runs tests written in it"
                  :signatures   "an rpc takes one message; there is nothing to reorder"}
}
