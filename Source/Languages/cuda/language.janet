{:name "cuda"
 :extensions [".cu" ".cuh"]
 :injection-aliases ["cu"]
 :line-comment "//"
 :lsp-root-markers ["compile_commands.json" ".clangd" "CMakeLists.txt"]
 :line-continuation "\\"
 # The upstream query is a delta whose first line says `inherits: cpp`;
 # discovery doesn't read that, so the base is named here.
 # c.test-body widens an unexpanded TEST_CASE macro over its sibling body.
 :escapes ["c.test-body"]
 :queries {:format ["cpp/format.janet"]
           :tests ["cpp/tests.janet"]
           :locals ["cpp/locals.janet"]
           :imports ["c/imports.janet"]
           :highlights ["cpp/highlights.janet"
                        "cuda/upstream/highlights.janet"]
           :indents ["cpp/indents.janet"]
           :signatures ["cpp/signatures.janet"]
           :calls ["cpp/calls.janet"]}
 :signature-template "void __ned_sig({}) {}"
 :not-applicable {:injections        "nothing in it is written in another language"
                  :style             "no canonical style; projects bring their own .clang-format"
                  :import-resolution "includes resolve through the toolchain's include paths"}
}
