{:name "java"
 :extensions [".java"]
 :line-comment "//"

 # Java/Kotlin share one marker set -- both build with Maven or Gradle. In
 # a multi-module build each module resolves as its own root (the walk is
 # nearest-ancestor-first); jdtls and kotlin-language-server both advertise
 # workspaceFolders, so sibling roots join one connection (LspManager).
 :lsp-root-markers ["pom.xml" "build.gradle" "build.gradle.kts" "settings.gradle" "settings.gradle.kts"]
 :signature-template "class __Ned { void __ned_sig({}) {} }"
 :import-resolution {:extensions ["java" "kt"] :source-roots ["src/main/java" "src/test/java" "src/main/kotlin" "src/test/kotlin" "src"] :import-statement "import {};"}
 :not-applicable {:injections "nothing in it is written in another language"}
}
