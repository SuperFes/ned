# .kts is a Kotlin *script* (a Gradle build file, most often) -- the same
# grammar and the same mode, no separate dialect.

{:name "kotlin"
 :extensions [".kt" ".kts"]
 :injection-aliases ["kt" "kts"]
 :line-comment "//"

 # Java/Kotlin share one marker set -- both build with Maven or Gradle. In
 # a multi-module build each module resolves as its own root (the walk is
 # nearest-ancestor-first); jdtls and kotlin-language-server both advertise
 # workspaceFolders, so sibling roots join one connection (LspManager).
 :lsp-root-markers ["pom.xml" "build.gradle" "build.gradle.kts" "settings.gradle" "settings.gradle.kts"]
 # A class parameter list also takes plain function parameters.
 :signature-template "class __ned_sig({})"
 :import-resolution {:extensions ["kt" "kts" "java"] :source-roots ["src/main/kotlin" "src/test/kotlin" "src/commonMain/kotlin" "src/jvmMain/kotlin" "src/main/java" "src/test/java" "src"] :import-statement "import {}"}
 :not-applicable {:injections "nothing in it is written in another language"}
}
