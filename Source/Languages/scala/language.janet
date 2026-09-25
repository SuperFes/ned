{:name "scala"
 :extensions [".scala" ".sbt" ".sc"]
 :injection-aliases ["sc"]
 :line-comment "//"
 :lsp-root-markers ["build.sbt" "build.sc" "build.mill"]
 :signature-template "class __ned_sig({})"
 :import-resolution {:extensions ["scala" "sc" "java"] :source-roots ["src/main/scala" "src/test/scala" "src/main/java" "src"]}
}
