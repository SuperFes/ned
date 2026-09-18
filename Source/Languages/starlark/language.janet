# Bazel's dialect. BUILD/WORKSPACE files carry no extension; the .bazel
# spelling is the newer convention and .bzl holds the macros.
{:name "starlark"
 :extensions [".bzl" ".bazel" ".star" ".sky"]
 :filenames ["BUILD" "WORKSPACE" "MODULE.bazel" "BUILD.bazel" "WORKSPACE.bazel"
             "Tiltfile" "Snakefile"]
 :line-comment "#"
 :lsp-root-markers ["MODULE.bazel" "WORKSPACE" "WORKSPACE.bazel"]
}
