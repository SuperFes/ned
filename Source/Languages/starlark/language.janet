# Bazel's dialect. BUILD/WORKSPACE files carry no extension; the .bazel
# spelling is the newer convention and .bzl holds the macros.
{:name "starlark"
 :extensions [".bzl" ".bazel" ".star" ".sky"]
 :injection-aliases ["bzl" "bazel" "skylark"]
 :filenames ["BUILD" "WORKSPACE" "MODULE.bazel" "BUILD.bazel" "WORKSPACE.bazel"
             "Tiltfile" "Snakefile"]
 :line-comment "#"
 :line-continuation "\\"
 :lsp-root-markers ["MODULE.bazel" "WORKSPACE" "WORKSPACE.bazel"]
 :import-resolution {:extensions ["bzl"] :bazel-labels true}
 :signature-template "def __ned_sig({}):\n    pass"
 :not-applicable {:indents      "the grammar's block bodies are its whole indent structure; elif/else already align with their if"
                  :continuation "a line continues only inside brackets or after `\\`"}
}
