{:name "dart"
 :extensions [".dart"]
 :line-comment "//"
 :lsp-root-markers ["pubspec.yaml"]
 :capture-classes {"identifier.constant" :constant
                   "identifier.parameter" :parameter}
 :signature-template "void __ned_sig({}) {}"
 :import-resolution {:extensions ["dart"] :package-scheme "package:"}
}
