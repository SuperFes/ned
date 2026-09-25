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
}
