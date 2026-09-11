//
// file-rename-propagation follow-up (ROADMAP.md, "File rename/move
// propagates, in both directions"): recomputing the import/include
// specifiers a file move invalidates, with no language server involved.
//
// Two directions, one piece of arithmetic:
//
//   * The imports written INSIDE the moved file, every relative one of
//     which now resolves from a different directory.
//   * The imports in OTHER files that named the moved file, each of which
//     now points at nothing.
//
// RewriteSpec below is the pure half -- string and path arithmetic over one
// specifier at a time, the same buffer-free split Editor/RenameReview.h and
// Editor/LocalScopes.h take, so it is unit-testable with no Buffer, no
// Parser and no Screen. PlanImportFixups is the half that touches the
// world: it parses each candidate with that file's own Mode::importTargets
// query, resolves every import through Editor/ImportResolve.h (the exact
// resolver go-to-file-at-point uses, so the two can never disagree about
// what an import names) and turns the result into byte-range edits.
//
// PLANNING HAPPENS BEFORE THE MOVE. Resolution is an on-disk question --
// "which file does this specifier name?" -- and once the rename has run,
// every import worth fixing is precisely the one that no longer resolves.
// That is the same ordering LSP's own willRenameFiles takes, and for the
// same reason. Offsets stay valid either way: a move changes where a file
// is, never what it says.
//
// The governing rule throughout: REWRITE THE SPECIFIER THE AUTHOR WROTE,
// never re-derive one from scratch. A rewritten specifier keeps the style
// of the one it replaces -- its "./" prefix or absence of one, its
// extension or absence of one, its dotted-vs-relative module form, and
// above all the root it was counted from (a root-relative "Editor/Mode.h"
// stays root-relative rather than becoming "../Editor/Mode.h") -- because a
// fixup that also restyles every import it touches reads as a reformatting
// pass and stops being reviewable. Anything the arithmetic cannot express
// in the original's own style is DECLINED rather than approximated: a bare
// package specifier, a PHP namespace, a Rust `mod` declaration, an
// angle-form system include, a target that moved out of the root its
// specifier counts from. Declines are counted and reported, never silently
// rewritten.
//

#ifndef NED_EDITOR_IMPORTFIXUP_H
#define NED_EDITOR_IMPORTFIXUP_H

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ned::editor::importfix {

// The shape of a specifier's text, which is what decides how it is
// recomputed. Derived from Mode.h's own ImportTarget flags plus the
// resolution root Editor/ImportResolve.h reports -- never from any
// per-language branch.
enum class SpecKind {
    // A path relative to the importing file's own directory: JS/TS's
    // "./foo", a C #include found beside its includer. Recomputed against
    // the importer's (possibly new) directory.
    RelativePath,
    // A path counted from the project root or an include path, not from the
    // importing file: ned's own "Editor/Mode.h". Recomputed against that
    // same root, which is what keeps it bare.
    RootRelativePath,
    // A dotted module path counted from a root that is not the importing
    // file's directory: Python's "import pkg.mod" / "from pkg.mod import x".
    DottedModule,
    // Python's leading-dot relative import ("from .mod import x", "from
    // ..pkg.mod import x") -- a dot count plus an optional dotted suffix.
    RelativeModule,
    // Everything this module declines to touch. Kept as a named kind rather
    // than an absent value so a caller can report what it skipped.
    Unsupported,
};

// One specifier to recompute. Every path is absolute; `spec` is the
// specifier's own text with any quotes/angle brackets already stripped
// (Mode.h's ImportTarget::target, which Mode.cpp already delimits).
struct RewriteRequest {
    SpecKind    kind = SpecKind::Unsupported;
    std::string spec;
    // The directory the specifier resolves from AFTER the move -- the
    // importing file's own new parent directory. (Fixing the moved file's
    // own imports and fixing its importers are the same call with different
    // arguments: here it is the moved file's new directory, there it is an
    // untouched importer's unchanged one.)
    std::filesystem::path importerDirectory;
    // RootRelativePath/DottedModule: the root the ORIGINAL specifier was
    // counted from, as reported by the resolver that found it.
    std::filesystem::path resolutionRoot;
    // Where the imported file lives now.
    std::filesystem::path newTarget;
};

// The recomputed specifier text, or nullopt when the original's style
// cannot express the new location (see the header comment: declined, never
// approximated). The returned text is delimiter-free, exactly like
// RewriteRequest::spec -- a caller splices it into the byte range the
// original occupied, so whatever quoted or bracketed it is untouched.
[[nodiscard]] std::optional<std::string> RewriteSpec(const RewriteRequest& request);

// Path -> "pkg.mod" against a root, dropping the extension, and collapsing
// a package index file ("pkg/__init__.py" -> "pkg"). nullopt when file does
// not live under root at all. Exposed because it is the one rule a
// language's package layout could invalidate, and the cheapest thing to
// pin in a test.
[[nodiscard]] std::optional<std::string> DottedModuleFor(const std::filesystem::path& file,
                                                         const std::filesystem::path& root,
                                                         const std::string&           indexBasename);

// One file's move: where it was, where it is (or is about to be).
struct MovedFile {
    std::filesystem::path from;
    std::filesystem::path to;
};

// One specifier rewrite inside one file. [startByte, endByte) is the
// specifier's own text (Mode.h's ImportTarget::targetStartByte), so
// delimiters are never part of an edit.
struct SpecEdit {
    std::size_t startByte = 0;
    std::size_t endByte   = 0;
    std::string newText;
};

// Every rewrite in one file. `file` is that file's POST-move path, which is
// where the edits are to be applied; `text` is the content the offsets
// index into, which the move itself never changed.
struct FileFixup {
    std::filesystem::path file;
    std::string           text;
    std::vector<SpecEdit> edits; // ascending, non-overlapping
};

struct FixupPlan {
    std::vector<FileFixup> files;
    // Imports that named a moved file (or sat inside one) but whose own
    // style could not express the new location. Reported rather than
    // guessed at -- see the header comment.
    std::size_t declined = 0;
    // Candidate files actually parsed, for the same "say what was covered"
    // reason the multibuffer excerpt cap reports its own total.
    std::size_t scanned = 0;
};

// Reads a file's current text. The caller's hook so an open buffer's LIVE
// content is used in place of its on-disk copy -- the rule every
// project-wide operation in this codebase follows (project search, project
// replace, the rename review). nullopt means "skip this file": unreadable,
// binary, or too big to hold.
using TextReader = std::function<std::optional<std::string>(const std::filesystem::path&)>;

// Plans every import rewrite `moved` implies, over `candidates` (each at
// its PRE-move path; a candidate that is itself moving is recognized and
// gets its own relative imports fixed). Must run before the move -- see the
// header comment.
[[nodiscard]] FixupPlan PlanImportFixups(const std::vector<MovedFile>&             moved,
                                         const std::vector<std::filesystem::path>& candidates,
                                         const TextReader&                         readText);

// An RE2 alternation matching the name every import of a moved file has to
// spell ("widget|Pane"), for feeding Editor/Project/Search.h's own threaded,
// .gitignore-aware, live-buffer-first scan -- which is what turns "which
// files might import this?" from a project-wide read on one thread into the
// search this editor already does well. A file the search does not match
// cannot import a moved file, so it is never parsed.
//
// An index file is named by its own directory ("./widget" for
// "widget/index.js"), so its parent is the word that goes in the pattern.
// Empty when nothing moved.
[[nodiscard]] std::string CandidatePattern(const std::vector<MovedFile>& moved);

// The text `fixup` describes, with every edit applied. Applied back to
// front so an earlier edit's offsets survive a later one.
[[nodiscard]] std::string ApplyFixup(const FileFixup& fixup);

} // namespace ned::editor::importfix

#endif // NED_EDITOR_IMPORTFIXUP_H
