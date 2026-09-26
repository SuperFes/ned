{:name "thrift"
 :extensions [".thrift"]
 :line-comment "//"
 :import-resolution {:extensions ["thrift"]}
 :not-applicable {:indents    "the grammar's delimited bodies are its whole indent structure"
                  :tests      "no test framework runs tests written in it"
                  :signatures "methods are called from generated code in other languages"
                  :lsp-root   "no project file of its own; the root falls through to the project's"
                  :style      "no canonical formatter or official style rule for the braces and blank lines ned's rules cover"}
}
