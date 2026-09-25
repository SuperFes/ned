{:name "groovy"
 :extensions [".groovy" ".gvy" ".gy" ".gsh" ".gradle"]
 :injection-aliases ["gradle"]
 :filenames ["Jenkinsfile"]
 :line-comment "//"
 :lsp-root-markers ["build.gradle" "settings.gradle"]
 :signature-template "def __ned_sig({}) {}"
 :import-resolution {:extensions ["groovy" "gradle" "java"] :source-roots ["src/main/groovy" "src/test/groovy" "src/main/java" "src"]}
}
