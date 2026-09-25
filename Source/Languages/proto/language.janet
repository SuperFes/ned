{:name "proto"
 :extensions [".proto"]
 :injection-aliases ["protobuf"]
 :line-comment "//"
 :lsp-root-markers ["buf.yaml" "buf.work.yaml"]
 :import-resolution {:extensions ["proto"] :source-roots ["proto" "protos"]}
}
