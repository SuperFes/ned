//
// project-search-hang follow-up. A .gitignore matcher -- ProjectSearch.cpp's
// SearchDirectory and ProjectTree.cpp's BuildProjectTree previously only
// skipped dot-directories (.git, etc.), so a recursive walk from a real
// project root also walked every file under build/, cmake-build-*/,
// node_modules/, and similar generated/dependency directories -- in a real
// repo that's easily gigabytes and tens of thousands of files, scanned
// synchronously on the UI thread with no progress feedback, which felt
// exactly like a hang even though it wasn't a real infinite loop (confirmed
// via investigation, not assumed).
//
// gitignore-correctness follow-up (was root-level-only): now consults the
// same sources real git does, in git's own precedence order (lowest to
// highest):
//
//   1. the global ignore file -- core.excludesFile resolved from
//      ~/.gitconfig / $XDG_CONFIG_HOME/git/config (a minimal INI scan of
//      just that one key, not a full git-config parser), defaulting to
//      $XDG_CONFIG_HOME/git/ignore (~/.config/git/ignore) when unset;
//   2. <gitdir>/info/exclude (a .git *file*'s "gitdir: <path>" worktree/
//      submodule indirection is followed);
//   3. .gitignore files from the root down to the queried path's own parent
//      directory -- each file's patterns are relative to the directory
//      containing it, and a deeper file's rules override a shallower one's.
//
// Within any single file, later lines win over earlier ones (negation
// re-includes). Sources 1 and 2 only apply when the root actually contains
// a .git entry (matching git itself: outside a repository these files mean
// nothing); a bare .gitignore with no repository still works, as before.
// Nested .gitignore files are discovered lazily, only for directories a
// query actually touches -- since both real walkers prune an ignored
// directory without descending, a .gitignore *inside* node_modules/ etc. is
// never even read.
//
// Glob subset supported: literal path segments, '*' (any run of non-'/'
// characters), '?' (one non-'/' character), '**' with git's own semantics
// (a leading "**/" matches in every directory, a trailing "/**" matches
// everything inside, an interior "/**/" matches zero or more directories --
// elsewhere it degrades to '*'), character classes ("[abc]", "[a-z]",
// "[!abc]"/"[^abc]"; an unterminated '[' is literal), a leading or interior
// '/' anchoring the pattern to its own .gitignore's directory (a pattern
// with no '/' at all, other than a trailing one, matches at any depth), a
// trailing '/' marking a directory-only pattern, and a leading '!' negating
// (re-including) a previously-matched path. Not handled: backslash escapes
// ("\#foo") and POSIX named classes ("[[:alnum:]]") -- both rare in real
// .gitignore files.
//
// Like real git (and because the walkers prune), IsIgnored answers for the
// path itself only -- it does not report a file as ignored merely because
// some ancestor directory is; callers are expected to prune ignored
// directories during their walk, which both real call sites do.
//

#ifndef NED_EDITOR_GITIGNORE_H
#define NED_EDITOR_GITIGNORE_H

#include <filesystem>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ned::editor {

class GitIgnoreMatcher {
  public:
    // Resolves and reads the global ignore file and <gitdir>/info/exclude
    // (only when root/".git" exists); root/".gitignore" and nested
    // .gitignore files are loaded lazily on first query touching their
    // directory. A missing file anywhere just contributes no rules, the
    // same "absent means nothing configured" convention this codebase uses
    // elsewhere (e.g. LspServerCommand's own std::nullopt).
    explicit GitIgnoreMatcher(const std::filesystem::path& root);

    // relativePath is root-relative, using '/' separators regardless of
    // platform (matches how a .gitignore pattern is itself always written).
    // isDirectory affects directory-only ("foo/") patterns -- such a rule
    // never matches a plain file.
    [[nodiscard]] bool IsIgnored(const std::filesystem::path& relativePath, bool isDirectory) const;

    // True when any file this matcher consulted -- present or absent at the
    // time -- has since changed, appeared, or vanished (one stat per
    // consulted file), or the root's .git entry itself toggled. What
    // CachedGitIgnoreMatcher uses to decide a cached matcher is stale;
    // deliberately not an incremental reload (a full rebuild is cheap and
    // only happens once per walk, not per file).
    [[nodiscard]] bool AnySourceChanged() const;

  private:
    struct Rule {
        std::regex pattern;
        bool       negated;
        bool       directoryOnly;
    };
    struct RuleSet {
        std::vector<Rule> rules;

        // The last matching rule's verdict (true = ignored), or nullopt if
        // no rule in this set matched at all -- distinct from "matched a
        // negation," which a caller must let override an earlier source.
        [[nodiscard]] std::optional<bool> Match(const std::string& pathText, bool isDirectory) const;
    };

    // Reads and parses one ignore-syntax file into out, recording it (and
    // its current mtime, or absence) in sources_ for AnySourceChanged.
    void LoadRulesFile(const std::filesystem::path& path, RuleSet& out) const;

    // Records a consulted file (rule file, config file, or .git pointer
    // file) in sources_ so AnySourceChanged can detect it changing.
    void RecordSource(const std::filesystem::path& file) const;

    // Lazily loads relativeDir/".gitignore" ("" = the root itself). Caller
    // must hold mutex_.
    const RuleSet& DirectoryRulesLocked(const std::string& relativeDir) const;

    void ResolveGlobalIgnore();
    void ResolveInfoExclude();

    std::filesystem::path root_;
    bool                  wasGitRepo_ = false;
    RuleSet               globalRules_;
    RuleSet               infoExcludeRules_;

    struct Source {
        std::filesystem::path                          file;
        std::optional<std::filesystem::file_time_type> mtime; // nullopt = absent when consulted
    };
    // mutable + mutex: nested .gitignore discovery happens inside const
    // IsIgnored. Both real call sites only query from the main thread (each
    // walks its tree synchronously before any worker fan-out), but the
    // cached matcher is shared process-wide state, so this stays guarded.
    mutable std::mutex                               mutex_;
    mutable std::unordered_map<std::string, RuleSet> directoryRules_;
    mutable std::vector<Source>                      sources_;
};

// project-search-rg-removal follow-up: ProjectSearch.cpp's SearchDirectory
// and ProjectTree.cpp's BuildProjectTree both used to construct a fresh
// GitIgnoreMatcher -- re-reading and re-parsing every ignore file from
// scratch -- on every single call, which was the previous cost ripgrep's
// own persistent process absorbed for free before it was replaced (see
// ProjectSearch.h). Real call sites hit this constantly (every project-wide
// search, every ProjectSidebar tree rebuild), while ignore files only
// change when a user hand-edits one. This caches one matcher per absolute
// root, rebuilding only when AnySourceChanged reports a consulted file
// changed underneath it -- a stat per consulted file to detect staleness is
// far cheaper than unconditionally re-parsing. Mutex-guarded static state,
// mirroring ProjectRoot.h/TabWidth.h's own pattern for process-wide state.
[[nodiscard]] const GitIgnoreMatcher& CachedGitIgnoreMatcher(const std::filesystem::path& root);

} // namespace ned::editor

#endif // NED_EDITOR_GITIGNORE_H
