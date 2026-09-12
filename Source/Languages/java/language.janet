{:name "java"
 :extensions [".java"]
 :line-comment "//"

 # Java/Kotlin share one marker set -- both build with Maven or Gradle. In
 # a multi-module build each module resolves as its own root (the walk is
 # nearest-ancestor-first); jdtls and kotlin-language-server both advertise
 # workspaceFolders, so sibling roots join one connection (LspManager).
 :lsp-root-markers ["pom.xml" "build.gradle" "build.gradle.kts" "settings.gradle" "settings.gradle.kts"]
}
