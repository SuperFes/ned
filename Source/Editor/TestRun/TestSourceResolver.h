//
// test-runner-gaps follow-up. Turns a framework-reported source path into a
// real, openable one.
//
// A parser stores `TestResult::file` exactly as the framework printed it
// (TestResult.h says so outright), which for several frameworks is not a
// path anything can open: go's `testing` package prints only
// filepath.Base(file), so a failure in a multi-directory module arrives as a
// bare "foo_test.go"; pytest/cargo print paths relative to the run's own
// working directory, which needn't be the project root. Handed straight to
// BufferList::OpenOrCreateFile (what a "*test results*" line's Enter/click
// jump does) a path like that doesn't exist, so the jump silently creates an
// empty scratch buffer of that name instead of opening the real file.
//
// Resolution order, first hit wins:
//   1. absolute and existing -- taken as-is;
//   2. root-relative and existing;
//   3. relative to the process's own working directory and existing;
//   4. root/<suffix of packagePath>/<basename>, longest suffix first;
//   5. a bounded, .gitignore-aware index of the project tree, matched by
//      basename and then ranked (see below).
//
// Step 4 exists so the go case -- the one that *always* needs resolving,
// since go never prints a directory at all -- costs a handful of stat calls
// instead of a directory walk: an import path's trailing segments are
// exactly the file's directory relative to the module root. Which leading
// segments are the module path rather than directories isn't knowable from
// the output, so every split is tried, most specific first.
//
// Step 5 is the fallback for a layout nothing above explains, and it is
// genuinely ambiguous in a module with several same-named files. Two
// independent signals rank the candidates: how many trailing path
// components of the reported `file` itself the candidate matches, and how
// many trailing segments of `packagePath` the candidate's own directory
// matches. Score ties fall back to the shallowest, then lexicographically
// first candidate, so the answer never depends on directory-iteration
// order. Nothing matching at all is std::nullopt -- callers report that
// rather than opening an empty buffer.
//
// The index is built lazily (only once some path actually falls through
// steps 1-4) and at most once per resolver instance, so a run with many
// failures in one module costs one walk, not one per failure. Instances are
// meant to be short-lived -- one per "*test results*" rebuild -- not cached
// across runs.
//
// This walk runs synchronously on the main thread, which is why its entry
// budget is deliberately modest rather than "however big the tree is": a
// ProjectRoot() pointing somewhere unexpectedly huge (it defaults to the
// process's working directory) must degrade to "didn't find it" quickly,
// not to a visible stall. Found live -- ctest runs with the build tree as
// cwd, and an unbounded walk of it cost ~30s per rebuild under ASan and
// starved unrelated tests in the same parallel run.
//

#ifndef NED_EDITOR_TESTRUN_TESTSOURCERESOLVER_H
#define NED_EDITOR_TESTRUN_TESTSOURCERESOLVER_H

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ned::editor::testrun {

class TestSourceResolver {
  public:
    explicit TestSourceResolver(std::filesystem::path root);

    // packagePath is the optional disambiguation hint described above --
    // pass TestResult::packagePath, which is empty for every parser that
    // doesn't report one.
    [[nodiscard]] std::optional<std::filesystem::path> Resolve(const std::string& file, const std::string& packagePath = "");

    // Upper bound on filesystem entries the index walk will visit before
    // giving up, so an accidentally huge root (a build tree, a home
    // directory, "/") can't turn one unresolvable basename into a visible
    // main-thread stall. Comfortably past any real source tree once
    // .gitignore has pruned build/dependency directories, and small enough
    // that hitting the ceiling costs a fraction of a second rather than
    // tens of seconds -- see the header comment above for the live case
    // that set this.
    static constexpr std::size_t kMaxIndexedEntries = 20000;

  private:
    // One bounded walk of root_, skipping dot-directories and gitignored
    // paths the same way ProjectTree/ProjectSearch's own walks do.
    void EnsureIndex();

    std::filesystem::path                                               root_;
    bool                                                                indexed_ = false;
    std::unordered_map<std::string, std::vector<std::filesystem::path>> byBasename_;
};

} // namespace ned::editor::testrun

#endif // NED_EDITOR_TESTRUN_TESTSOURCERESOLVER_H
