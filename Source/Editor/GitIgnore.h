//
// project-search-hang follow-up. A minimal .gitignore pattern matcher --
// ProjectSearch.cpp's SearchDirectory and ProjectTree.cpp's BuildProjectTree
// previously only skipped dot-directories (.git, etc.), so a recursive walk
// from a real project root also walked every file under build/,
// cmake-build-*/, node_modules/, and similar generated/dependency
// directories -- in a real repo that's easily gigabytes and tens of
// thousands of files, scanned synchronously on the UI thread with no
// progress feedback, which felt exactly like a hang even though it wasn't a
// real infinite loop (confirmed via investigation, not assumed).
//
// Deliberately root-level only: reads a single .gitignore from the root
// directory passed to the constructor (in every real call site today, that's
// editor::ProjectRoot()) -- matches this codebase's existing single-root
// model (Editor/ProjectRoot.h has no per-directory concept either). Nested
// .gitignore files in subdirectories are not read; a documented v1 scope
// cut, not an oversight.
//
// Glob subset supported (covers what real .gitignore files actually use in
// practice): literal path segments, '*' (any run of non-'/' characters),
// '?' (one non-'/' character), a leading or interior '/' anchors the
// pattern to the root (matching real git's own rule -- a pattern with no
// '/' at all, other than a trailing one, matches at any depth), a trailing
// '/' marks a directory-only pattern, and a leading '!' negates (re-
// includes) a previously-matched path, later rules winning over earlier
// ones. Character classes ("[abc]") and a "**" appearing in the middle of a
// pattern (not just leading/trailing) are not specially handled -- a
// literal "**" degrades to matching like a single '*' segment, a rare
// pattern shape in real .gitignore files.
//

#ifndef NED_EDITOR_GITIGNORE_H
#define NED_EDITOR_GITIGNORE_H

#include <filesystem>
#include <regex>
#include <vector>

namespace ned::editor {

class GitIgnoreMatcher {
  public:
    // Reads and compiles root/".gitignore" if it exists; a missing file
    // leaves this matcher with no rules (IsIgnored always false), the same
    // "absent means nothing configured" convention this codebase uses
    // elsewhere (e.g. LspServerCommand's own std::nullopt).
    explicit GitIgnoreMatcher(const std::filesystem::path& root);

    // relativePath is root-relative, using '/' separators regardless of
    // platform (matches how a .gitignore pattern is itself always written).
    // isDirectory affects directory-only ("foo/") patterns -- such a rule
    // never matches a plain file.
    [[nodiscard]] bool IsIgnored(const std::filesystem::path& relativePath, bool isDirectory) const;

  private:
    struct Rule {
        std::regex pattern;
        bool       negated;
        bool       directoryOnly;
    };
    std::vector<Rule> rules_;
};

} // namespace ned::editor

#endif // NED_EDITOR_GITIGNORE_H
