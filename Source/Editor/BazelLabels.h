//
// Bazel labels as paths: "//pkg:file.bzl" is file.bzl in the package
// directory pkg under the workspace root, ":file.bzl" is in the loading
// file's own package, and a package is the nearest directory holding a
// BUILD file. Import resolution's `:bazel-labels` key
// (ImportResolutionConfig.h).
//

#ifndef NED_EDITOR_BAZELLABELS_H
#define NED_EDITOR_BAZELLABELS_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ned::editor {

// Where a label's target is counted from: `root` joined with `remainder`.
// `prefix` is everything up to and including the colon, kept as written by
// anything that rewrites the target. An empty `root` means nothing on disk
// answers the label: another repository's ("@rules_cc//cc:defs.bzl"), or a
// label with no target part.
struct BazelImportRoot {
    std::string           prefix;
    std::string           remainder;
    std::filesystem::path root;
};

[[nodiscard]] BazelImportRoot BazelImportRootFor(std::string_view label, const std::filesystem::path& importingFile);

// The label `file` has as seen from `importingFile`, in the form `original`
// was written in: ":name" when the original was package-relative and the
// two share a package, else "//pkg:name" (with the original's "@//" or
// "@@//" kept). nullopt when no package holds `file`.
[[nodiscard]] std::optional<std::string> BazelLabelFor(const std::filesystem::path& file,
                                                       const std::filesystem::path& importingFile,
                                                       std::string_view             original);

} // namespace ned::editor

#endif // NED_EDITOR_BAZELLABELS_H
