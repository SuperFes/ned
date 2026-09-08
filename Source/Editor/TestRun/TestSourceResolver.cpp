#include "TestSourceResolver.h"

#include <algorithm>
#include <system_error>
#include <utility>

#include "Editor/GitIgnore.h"

namespace ned::editor::testrun {

namespace {

    // Path components, outermost first ("a/b/c.go" -> {"a","b","c.go"}).
    std::vector<std::string> Components(const std::filesystem::path& path) {
        std::vector<std::string> parts;
        for (const std::filesystem::path& part : path) {
            const std::string text = part.string();
            if (!text.empty() && text != "/") {
                parts.push_back(text);
            }
        }
        return parts;
    }

    // How many trailing components the two sequences share.
    std::size_t CommonSuffixLength(const std::vector<std::string>& a, const std::vector<std::string>& b) {
        std::size_t shared = 0;
        while (shared < a.size() && shared < b.size() && a[a.size() - 1 - shared] == b[b.size() - 1 - shared]) {
            ++shared;
        }
        return shared;
    }

    // Import paths use '/' on every platform; a package's trailing segments
    // are the file's own directory relative to the module root.
    std::vector<std::string> SplitImportPath(const std::string& importPath) {
        std::vector<std::string> parts;
        std::size_t              start = 0;
        while (start <= importPath.size()) {
            const std::size_t slash   = importPath.find('/', start);
            const std::size_t end     = slash == std::string::npos ? importPath.size() : slash;
            const std::string segment = importPath.substr(start, end - start);
            if (!segment.empty()) {
                parts.push_back(segment);
            }
            if (slash == std::string::npos) {
                break;
            }
            start = slash + 1;
        }
        return parts;
    }

    bool IsDotDirectory(const std::filesystem::directory_entry& entry) {
        const std::string name = entry.path().filename().string();
        return !name.empty() && name.front() == '.';
    }

    bool ExistsAsFile(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec);
    }

} // namespace

TestSourceResolver::TestSourceResolver(std::filesystem::path root) : root_(std::move(root)) {
}

std::optional<std::filesystem::path> TestSourceResolver::Resolve(const std::string& file, const std::string& packagePath) {
    if (file.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path reported(file);
    if (reported.is_absolute()) {
        return ExistsAsFile(reported) ? std::optional(reported.lexically_normal()) : std::nullopt;
    }

    if (!root_.empty()) {
        const std::filesystem::path underRoot = (root_ / reported).lexically_normal();
        if (ExistsAsFile(underRoot)) {
            return underRoot;
        }
    }

    std::error_code             ec;
    const std::filesystem::path underCwd = std::filesystem::absolute(reported, ec).lexically_normal();
    if (!ec && ExistsAsFile(underCwd)) {
        return underCwd;
    }

    // The go case -- which is the one that *always* needs resolving, since
    // go never prints a directory at all -- doesn't need a walk: an import
    // path's trailing segments are exactly the file's directory relative to
    // the module root, so probing root/<suffix>/<basename> for each suffix,
    // longest (most specific) first, resolves it in a handful of stats.
    // Which leading segments are the module path and which are directories
    // isn't knowable from the output, hence trying every split rather than
    // guessing one.
    if (!root_.empty() && !packagePath.empty()) {
        const std::vector<std::string> packageParts = SplitImportPath(packagePath);
        const std::filesystem::path    basename     = reported.filename();
        for (std::size_t skip = 0; skip < packageParts.size(); ++skip) {
            std::filesystem::path candidate = root_;
            for (std::size_t i = skip; i < packageParts.size(); ++i) {
                candidate /= packageParts[i];
            }
            candidate /= basename;
            if (ExistsAsFile(candidate)) {
                return candidate.lexically_normal();
            }
        }
    }

    EnsureIndex();
    const auto it = byBasename_.find(reported.filename().string());
    if (it == byBasename_.end() || it->second.empty()) {
        return std::nullopt;
    }

    // Rank by the two independent signals this header documents; ties go to
    // the shallowest then lexicographically first candidate, so the answer
    // never depends on directory-iteration order.
    const std::vector<std::string> reportedParts = Components(reported);
    const std::vector<std::string> packageParts  = SplitImportPath(packagePath);

    const std::filesystem::path* best      = nullptr;
    std::size_t                  bestScore = 0;
    std::size_t                  bestDepth = 0;
    for (const std::filesystem::path& candidate : it->second) {
        const std::vector<std::string> candidateParts = Components(candidate);
        std::vector<std::string>       candidateDir   = candidateParts;
        if (!candidateDir.empty()) {
            candidateDir.pop_back(); // the basename itself is matched by construction
        }
        const std::size_t score =
            CommonSuffixLength(reportedParts, candidateParts) + CommonSuffixLength(packageParts, candidateDir);
        const std::size_t depth = candidateParts.size();
        if (best == nullptr || score > bestScore || (score == bestScore && depth < bestDepth) ||
            (score == bestScore && depth == bestDepth && candidate < *best)) {
            best      = &candidate;
            bestScore = score;
            bestDepth = depth;
        }
    }
    return best != nullptr ? std::optional(*best) : std::nullopt;
}

void TestSourceResolver::EnsureIndex() {
    if (indexed_) {
        return;
    }
    indexed_ = true;
    if (root_.empty()) {
        return;
    }

    std::error_code             ec;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(root_, ec).lexically_normal();
    if (ec || !std::filesystem::is_directory(absoluteRoot, ec)) {
        return;
    }

    const GitIgnoreMatcher&            gitIgnore = CachedGitIgnoreMatcher(absoluteRoot);
    std::vector<std::filesystem::path> pending{absoluteRoot};
    std::size_t                        visited = 0;

    while (!pending.empty() && visited < kMaxIndexedEntries) {
        const std::filesystem::path directory = std::move(pending.back());
        pending.pop_back();

        std::error_code iterationError;
        for (const auto& entry : std::filesystem::directory_iterator(
                 directory, std::filesystem::directory_options::skip_permission_denied, iterationError)) {
            if (++visited >= kMaxIndexedEntries) {
                return; // an accidentally huge root -- abandon rather than walk on
            }
            const std::filesystem::path relative = std::filesystem::relative(entry.path(), absoluteRoot, ec);
            if (ec) {
                continue;
            }
            if (entry.is_directory()) {
                if (!IsDotDirectory(entry) && !gitIgnore.IsIgnored(relative, /*isDirectory=*/true)) {
                    pending.push_back(entry.path());
                }
            }
            else if (entry.is_regular_file() && !gitIgnore.IsIgnored(relative, /*isDirectory=*/false)) {
                byBasename_[entry.path().filename().string()].push_back(entry.path());
            }
        }
    }

    // Deterministic candidate order regardless of how the walk enumerated
    // directories -- Resolve's own tie-break relies on it only as a final
    // fallback, but a stable index keeps repeated runs identical.
    for (auto& [basename, candidates] : byBasename_) {
        std::sort(candidates.begin(), candidates.end());
    }
}

} // namespace ned::editor::testrun
