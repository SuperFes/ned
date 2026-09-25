#include "ImportFixup.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string_view>
#include <vector>

#include "Editor/ImportResolve.h"
#include "Editor/Link.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"

namespace ned::editor::importfix {

namespace {

    // Every component of a relative path, in order, with any "." dropped.
    std::vector<std::string> Components(const std::filesystem::path& relative) {
        std::vector<std::string> parts;
        for (const std::filesystem::path& part : relative) {
            const std::string text = part.string();
            if (text.empty() || text == ".") {
                continue;
            }
            parts.push_back(text);
        }
        return parts;
    }

    // The last component's extension, dropped in place. A leading-dot
    // component ("..") is left alone -- stem()/extension() would read it as
    // an extension-only filename.
    void DropExtension(std::vector<std::string>& parts) {
        if (parts.empty() || parts.back() == "..") {
            return;
        }
        const std::filesystem::path last = parts.back();
        if (!last.extension().empty() && !last.stem().empty()) {
            parts.back() = last.stem().string();
        }
    }

    bool StartsWith(std::string_view text, std::string_view prefix) {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    // `part` with or without its extension: "CMakeLists.txt" is an index
    // whole, and "CMakeLists" once DropExtension has run.
    bool IsIndexBasename(const std::string& part, const ImportResolutionConfig& resolution) {
        return std::any_of(resolution.indexBasenames.begin(), resolution.indexBasenames.end(),
                           [&part](const std::string& basename) {
                               return basename == part || std::filesystem::path(basename).stem() == part;
                           });
    }

    // One path step spelled the way a module path writes it -- the reverse
    // of ModulePathToFilePath's substitutions (Editor/ImportResolve.h).
    std::string ModuleStep(std::string step, const ImportResolutionConfig& resolution) {
        for (const auto& [module, path] : resolution.moduleSubstitutions) {
            if (path.empty()) {
                continue;
            }
            for (std::size_t at = step.find(path); at != std::string::npos; at = step.find(path, at + module.size())) {
                step.replace(at, path.size(), module);
            }
        }
        return step;
    }

    std::string JoinModule(const std::vector<std::string>& parts, const ImportResolutionConfig& resolution) {
        std::string joined;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                joined += resolution.moduleSeparator;
            }
            joined += ModuleStep(parts[i], resolution);
        }
        return joined;
    }

    // A path's last step as the specifier it replaces spelled it: a Sass
    // partial's "_" and a package's index file are left unwritten when the
    // original left them unwritten ("base/vars", "./widget").
    void MatchSpecStyle(std::vector<std::string>& parts, const std::string& spec,
                        const ImportResolutionConfig& resolution) {
        if (parts.empty()) {
            return;
        }
        const std::string written = std::filesystem::path(spec).filename().string();
        if (parts.size() > 1 && IsIndexBasename(parts.back(), resolution) && !IsIndexBasename(written, resolution)) {
            parts.pop_back();
            return;
        }
        const std::string& prefix = resolution.partialPrefix;
        if (!prefix.empty() && StartsWith(parts.back(), prefix) && !StartsWith(written, prefix)) {
            parts.back().erase(0, prefix.size());
        }
    }

    std::filesystem::path RelativeBetween(const std::filesystem::path& target, const std::filesystem::path& from) {
        return target.lexically_normal().lexically_relative(from.lexically_normal());
    }

} // namespace

std::optional<std::string> DottedModuleFor(const std::filesystem::path& file, const std::filesystem::path& root,
                                           const ImportResolutionConfig& resolution) {
    const std::filesystem::path relative = RelativeBetween(file, root);
    if (relative.empty()) {
        return std::nullopt;
    }
    std::vector<std::string> parts = Components(relative);
    if (parts.empty() || parts.front() == "..") {
        return std::nullopt; // outside the root a dotted path is counted from
    }
    DropExtension(parts);
    if (IsIndexBasename(parts.back(), resolution)) {
        parts.pop_back(); // "pkg/__init__.py" is the module "pkg", not "pkg.__init__"
    }
    if (parts.empty()) {
        return std::nullopt;
    }
    return JoinModule(parts, resolution);
}

std::optional<std::string> RewriteSpec(const RewriteRequest& request) {
    if (request.newTarget.empty()) {
        return std::nullopt;
    }

    switch (request.kind) {
        case SpecKind::Unsupported:
            return std::nullopt;

        case SpecKind::RelativePath: {
            // An absolute specifier names the same file wherever it is read
            // from, so a move never invalidates it -- and rewriting one into
            // a relative path would restyle it, which this deliberately
            // never does.
            if (request.spec.empty() || request.spec.front() == '/') {
                return std::nullopt;
            }
            if (request.importerDirectory.empty()) {
                return std::nullopt;
            }
            const std::filesystem::path relative = RelativeBetween(request.newTarget, request.importerDirectory);
            std::vector<std::string>    parts    = Components(relative);
            if (parts.empty()) {
                return std::nullopt;
            }
            // The author wrote no extension (a JS/TS "./foo", a Python-style
            // path), so the rewrite writes none either -- the same
            // extension widening that resolved the original will resolve
            // this one (Editor/ImportResolutionConfig.h).
            if (std::filesystem::path(request.spec).extension().empty()) {
                DropExtension(parts);
                MatchSpecStyle(parts, request.spec, request.resolution);
            }
            std::string rewritten;
            for (const std::string& part : parts) {
                rewritten += rewritten.empty() ? part : "/" + part;
            }
            if (rewritten.empty()) {
                return std::nullopt;
            }
            // "./" is a style choice in JS/TS and absent by convention in a
            // C include; preserved either way. A path that has to ascend
            // says so with ".." regardless of what the original looked like,
            // since there is no other way to write it.
            const bool explicitlyRelative = StartsWith(request.spec, "./") || StartsWith(request.spec, "../");
            if (explicitlyRelative && !StartsWith(rewritten, "../")) {
                rewritten = "./" + rewritten;
            }
            return rewritten;
        }

        case SpecKind::RootRelativePath: {
            // Counted from a root the importing file's own location has no
            // say in, so where the importer went is irrelevant -- only
            // whether the target is still under that same root.
            if (request.resolutionRoot.empty()) {
                return std::nullopt;
            }
            const std::filesystem::path relative = RelativeBetween(request.newTarget, request.resolutionRoot);
            std::vector<std::string>    parts    = Components(relative);
            if (parts.empty() || parts.front() == "..") {
                return std::nullopt; // moved out of the root it was counted from
            }
            if (std::filesystem::path(request.spec).extension().empty()) {
                DropExtension(parts);
                MatchSpecStyle(parts, request.spec, request.resolution);
            }
            std::string rewritten;
            for (const std::string& part : parts) {
                rewritten += rewritten.empty() ? part : "/" + part;
            }
            return rewritten.empty() ? std::nullopt : std::optional<std::string>(rewritten);
        }

        case SpecKind::DottedModule: {
            if (request.resolutionRoot.empty()) {
                return std::nullopt;
            }
            return DottedModuleFor(request.newTarget, request.resolutionRoot, request.resolution);
        }

        case SpecKind::RelativeModule: {
            if (request.importerDirectory.empty()) {
                return std::nullopt;
            }
            const std::filesystem::path relative = RelativeBetween(request.newTarget, request.importerDirectory);
            std::vector<std::string>    parts    = Components(relative);
            if (parts.empty()) {
                return std::nullopt;
            }
            // One leading dot means "this directory", each further dot one
            // more level up (Mode.h's ImportTarget::relativeLevel), so the
            // dot count is the number of ".." components plus one.
            int level = 1;
            while (!parts.empty() && parts.front() == "..") {
                ++level;
                parts.erase(parts.begin());
            }
            DropExtension(parts);
            if (!parts.empty() && IsIndexBasename(parts.back(), request.resolution)) {
                parts.pop_back();
            }
            return std::string(static_cast<std::size_t>(level), '.') + JoinModule(parts, request.resolution);
        }
    }
    return std::nullopt;
}

namespace {

    std::filesystem::path Canonical(const std::filesystem::path& path) {
        std::error_code             ec;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        return ec ? path.lexically_normal() : canonical;
    }

    // Resolving one file per distinct extension instead of per file: every
    // Mode built here carries its own tree-sitter Parser and compiled
    // queries, which is far too much to pay once per project file. Reused
    // sequentially on one thread, which is the only way a shared Parser is
    // ever safe (see ROADMAP's dynamic-mode race).
    class ModeCache {
      public:
        const Mode& For(const std::filesystem::path& path) {
            const std::string filenameKey = path.filename().string();
            if (const auto it = byFilename_.find(filenameKey); it != byFilename_.end()) {
                return it->second;
            }
            const std::string extensionKey = path.extension().string();
            if (const auto it = byExtension_.find(extensionKey); it != byExtension_.end()) {
                return it->second;
            }
            // A whole-filename override wins over an extension one and is
            // cached under the name it matched, matching ModeForPath's own
            // precedence exactly.
            if (std::optional<Mode> overridden = ModeForFileOverride(path)) {
                return byFilename_.emplace(filenameKey, std::move(*overridden)).first->second;
            }
            return byExtension_.emplace(extensionKey, ModeForPath(path)).first->second;
        }

      private:
        std::map<std::string, Mode> byFilename_;
        std::map<std::string, Mode> byExtension_;
    };

    // What a moved file may be called in the text of an import that names
    // it: its stem; for an index file also its directory ("./widget" for
    // "widget/index.js"); and a snake_case name's kebab-case spelling
    // (Clojure's "my-app" for "my_app"). A word too many only costs a parse.
    std::vector<std::string> SearchStemsFor(const std::filesystem::path& path) {
        static constexpr std::string_view kIndexStems[] = {"index", "__init__", "mod", "init",
                                                           "package", "default", "CMakeLists", "_index"};
        std::vector<std::string>          stems;
        const std::string                 stem = path.stem().string();
        stems.push_back(stem);
        if (std::find(std::begin(kIndexStems), std::end(kIndexStems), stem) != std::end(kIndexStems)) {
            const std::string parent = path.parent_path().filename().string();
            if (!parent.empty()) {
                stems.push_back(parent);
            }
        }
        if (stem.size() > 1 && stem.front() == '_') {
            stems.push_back(stem.substr(1)); // a Sass partial, imported without its "_"
        }
        for (std::size_t i = 0, count = stems.size(); i < count; ++i) {
            if (stems[i].find('_') != std::string::npos) {
                std::string kebab = stems[i];
                std::replace(kebab.begin(), kebab.end(), '_', '-');
                stems.push_back(std::move(kebab));
            }
        }
        return stems;
    }

    bool MentionsAny(std::string_view text, const std::vector<std::string>& stems) {
        return std::any_of(stems.begin(), stems.end(), [text](const std::string& stem) {
            return !stem.empty() && text.find(stem) != std::string_view::npos;
        });
    }

    // An angle-form include and an absolute path both name a file the
    // importing one has no positional relationship with, so neither a move
    // of the importer nor this module's arithmetic has anything to say
    // about them.
    bool IsAngleForm(std::string_view text, const ImportTarget& target) {
        return target.targetStartByte > 0 && target.targetStartByte <= text.size() &&
               text[target.targetStartByte - 1] == '<';
    }

    SpecKind SpecKindFor(const ImportTarget& target, std::string_view text, const std::filesystem::path& resolutionBase,
                         const std::filesystem::path& importerDirectory) {
        if (target.isNamespacePath || target.isModDeclaration) {
            return SpecKind::Unsupported; // PSR-4 and Rust's file-per-module layout, neither a path
        }
        if (target.isModulePath) {
            return target.relativeLevel > 0 ? SpecKind::RelativeModule : SpecKind::DottedModule;
        }
        if (IsAngleForm(text, target) || target.target.empty() || target.target.front() == '/') {
            return SpecKind::Unsupported;
        }
        if (resolutionBase.empty()) {
            return SpecKind::Unsupported;
        }
        return Canonical(resolutionBase) == Canonical(importerDirectory) ? SpecKind::RelativePath
                                                                         : SpecKind::RootRelativePath;
    }

    // Does `candidate` name `file`, allowing for the two widenings
    // ResolveFileLink itself allows: a specifier written without the real
    // extension, and a directory named for its own index file.
    bool NamesFile(const std::filesystem::path& candidate, const std::filesystem::path& file) {
        const std::filesystem::path normalized = candidate.lexically_normal();
        if (normalized == file) {
            return true;
        }
        if (std::filesystem::path(normalized.string() + file.extension().string()).lexically_normal() == file) {
            return true;
        }
        return (normalized / file.filename()).lexically_normal() == file;
    }

    // Resolution for a world where the move already happened -- an
    // externally detected one (a git mv, a file manager), where the file a
    // specifier named is gone before ned ever hears about it, so
    // ResolveImportLink can only report "nothing there".
    //
    // The inverse question answers it without needing the old file back:
    // could this specifier have named one of the files that moved? It could
    // exactly when joining it onto some directory yields that file -- and
    // the directory that does is, by construction, the root the specifier
    // was counted from, which is the other thing a rewrite needs. Tried
    // importer-first and then root-ward, the same order ResolveFileLink
    // tries them in, so the two agree about which base wins.
    std::optional<ResolvedImport> MatchMovedTarget(const std::string&                                            specPath,
                                                   const std::vector<std::filesystem::path>&                     roots,
                                                   const std::map<std::filesystem::path, std::filesystem::path>& moves) {
        if (specPath.empty() || specPath.front() == '/') {
            return std::nullopt;
        }
        // Roots in the order a resolver would have tried them, so the base
        // this reports is the one that would actually have won.
        for (const std::filesystem::path& root : roots) {
            if (root.empty()) {
                continue;
            }
            for (const auto& [from, to] : moves) {
                if (NamesFile(root / specPath, from)) {
                    return ResolvedImport{.path = from, .base = root};
                }
            }
        }
        return std::nullopt;
    }

    // Whether moving the file this import is WRITTEN IN invalidates it --
    // true for anything counted from that file's own location, false for
    // anything counted from a root. What decides whether a failed rewrite
    // is worth reporting as a decline.
    bool DependsOnImporterLocation(const ImportTarget& target, SpecKind kind) {
        return kind == SpecKind::RelativePath || kind == SpecKind::RelativeModule || target.isModDeclaration ||
               target.isNamespacePath;
    }

} // namespace

FixupPlan PlanImportFixups(const std::vector<MovedFile>& moved, const std::vector<std::filesystem::path>& candidates,
                           const TextReader& readText) {
    FixupPlan plan;
    if (moved.empty() || !readText) {
        return plan;
    }

    std::map<std::filesystem::path, std::filesystem::path> moves;
    std::vector<std::string>                               stems;
    for (const MovedFile& move : moved) {
        moves[Canonical(move.from)] = move.to;
        for (std::string& stem : SearchStemsFor(move.from)) {
            stems.push_back(std::move(stem));
        }
    }

    // A moved file is always its own candidate, whether or not the caller's
    // walk found it -- fixing the relative imports it wrote itself is half
    // of what this does, and a file moving out of (or into) the project root
    // is exactly the case a walk of that root misses.
    std::vector<std::filesystem::path> scanList = candidates;
    {
        std::set<std::filesystem::path> known;
        for (const std::filesystem::path& candidate : candidates) {
            known.insert(Canonical(candidate));
        }
        for (const MovedFile& move : moved) {
            if (known.insert(Canonical(move.from)).second) {
                scanList.push_back(move.from);
            }
        }
    }

    ModeCache modes;
    for (const std::filesystem::path& candidate : scanList) {
        const auto                  movedIt       = moves.find(Canonical(candidate));
        const bool                  importerMoved = movedIt != moves.end();
        const std::filesystem::path newCandidate  = importerMoved ? movedIt->second : candidate;

        // Whichever end of the move actually exists: the old path before a
        // rename ned is about to make, the new one after a move it is only
        // hearing about. The content is the same either way -- a move
        // changes where a file is, never what it says.
        std::filesystem::path      readFrom = candidate;
        std::optional<std::string> text     = readText(candidate);
        if (!text && importerMoved) {
            readFrom = newCandidate;
            text     = readText(newCandidate);
        }
        if (!text) {
            continue;
        }
        const Mode& mode = modes.For(readFrom);
        if (!mode.importTargets) {
            continue;
        }
        // A file that never spells the moved file's name cannot import it.
        // The moved file itself is exempt: its own imports name everything
        // BUT itself, and they are exactly the other half of this feature.
        if (!importerMoved && !MentionsAny(*text, stems)) {
            continue;
        }
        ++plan.scanned;

        std::vector<ImportTarget> targets;
        try {
            targets = mode.importTargets(*text);
        }
        catch (const std::exception&) {
            continue; // a query that cannot run is a file with no imports, not an error to raise
        }

        FileFixup                    fixup;
        const ImportResolutionConfig resolution = ImportResolutionConfigFor(mode);
        fixup.file                              = newCandidate;
        fixup.text                              = *text;
        for (const ImportTarget& target : targets) {
            if (target.targetEndByte > text->size() || target.targetEndByte < target.targetStartByte) {
                continue;
            }
            const link::DetectedLink      detected = ImportLinkFor(target, resolution);
            const auto                    prefixed = MatchImportPrefix(detected.target, candidate, mode);
            std::optional<ResolvedImport> resolved = ResolveImportLink(detected, candidate, mode);
            if (!resolved) {
                // Nothing on disk answers this specifier. Either it was
                // already broken -- not this move's doing, and not this
                // feature's business -- or the move already happened and
                // took the answer with it.
                resolved = MatchMovedTarget(prefixed ? prefixed->remainder : detected.target,
                                            ImportSearchRoots(detected, candidate, mode), moves);
            }
            if (!resolved) {
                continue;
            }

            const auto                  targetMovedIt = moves.find(Canonical(resolved->path));
            const bool                  targetMoved   = targetMovedIt != moves.end();
            const std::filesystem::path newTarget     = targetMoved ? targetMovedIt->second : resolved->path;

            // A prefixed specifier is counted from its prefix's root however
            // close to it the importing file sits.
            const SpecKind kind = prefixed ? SpecKind::RootRelativePath
                                           : SpecKindFor(target, *text, resolved->base, candidate.parent_path());
            if (!targetMoved && !(importerMoved && DependsOnImporterLocation(target, kind))) {
                continue; // nothing about this import changed
            }
            if (kind == SpecKind::Unsupported) {
                ++plan.declined;
                continue;
            }

            // A specifier counted from the importing file's own directory
            // follows that file when it moves; one counted from a root does
            // not.
            const bool rootIsImporter =
                !prefixed && Canonical(resolved->base) == Canonical(candidate.parent_path());
            const std::filesystem::path resolutionRoot =
                rootIsImporter ? newCandidate.parent_path() : resolved->base;

            const std::optional<std::string> rewritten =
                RewriteSpec(RewriteRequest{.kind              = kind,
                                           .spec              = prefixed ? prefixed->remainder : target.target,
                                           .importerDirectory = newCandidate.parent_path(),
                                           .resolutionRoot    = resolutionRoot,
                                           .newTarget         = newTarget,
                                           .resolution        = resolution});
            if (!rewritten) {
                ++plan.declined;
                continue;
            }
            const std::string newText = prefixed ? prefixed->prefix + *rewritten : *rewritten;
            const std::size_t length  = target.targetEndByte - target.targetStartByte;
            if (newText == text->substr(target.targetStartByte, length)) {
                continue; // already says the right thing
            }
            fixup.edits.push_back(SpecEdit{target.targetStartByte, target.targetEndByte, newText});
        }

        if (!fixup.edits.empty()) {
            std::sort(fixup.edits.begin(), fixup.edits.end(),
                      [](const SpecEdit& a, const SpecEdit& b) { return a.startByte < b.startByte; });
            plan.files.push_back(std::move(fixup));
        }
    }
    return plan;
}

std::string CandidatePattern(const std::vector<MovedFile>& moved) {
    std::set<std::string> stems;
    for (const MovedFile& move : moved) {
        for (std::string& stem : SearchStemsFor(move.from)) {
            if (!stem.empty()) {
                stems.insert(std::move(stem));
            }
        }
    }
    std::string pattern;
    for (const std::string& stem : stems) {
        if (!pattern.empty()) {
            pattern += '|';
        }
        // Word-bounded: an import of "Mode.h" spells Mode, and a file that
        // only ever says ModeLine cannot be importing it. Worth the two
        // characters -- everything this matches gets a real tree-sitter
        // parse, which is the expensive half.
        pattern += "\\b";
        for (const char c : stem) {
            // Escaped by hand rather than via RE2::QuoteMeta so this header
            // stays free of a dependency it otherwise has no use for.
            if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
                pattern += '\\';
            }
            pattern += c;
        }
        pattern += "\\b";
    }
    return pattern;
}

std::string ApplyFixup(const FileFixup& fixup) {
    std::string text = fixup.text;
    for (auto it = fixup.edits.rbegin(); it != fixup.edits.rend(); ++it) {
        if (it->startByte > text.size() || it->endByte > text.size() || it->endByte < it->startByte) {
            continue; // a stale offset never becomes an edit
        }
        text.replace(it->startByte, it->endByte - it->startByte, it->newText);
    }
    return text;
}

} // namespace ned::editor::importfix
