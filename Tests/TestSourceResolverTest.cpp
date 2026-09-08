#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/TestRun/TestSourceResolver.h"

using ned::editor::testrun::TestSourceResolver;

namespace {

// One disposable tree per case, ProjectSearchTest.cpp's own fixture shape.
struct TempTree {
    std::filesystem::path root;

    explicit TempTree(const std::string& name) : root(std::filesystem::temp_directory_path() / ("ned_test_source_resolver_" + name)) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~TempTree() {
        std::filesystem::remove_all(root);
    }

    void Write(const std::string& relative, const std::string& contents = "x\n") const {
        const std::filesystem::path path = root / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path) << contents;
    }
};

} // namespace

TEST_CASE("TestSourceResolver takes an existing absolute path as-is", "[TestSourceResolver]") {
    const TempTree tree("absolute");
    tree.Write("pkg/foo_test.go");

    TestSourceResolver resolver(tree.root);
    const auto         resolved = resolver.Resolve((tree.root / "pkg" / "foo_test.go").string(), "");

    REQUIRE(resolved.has_value());
    REQUIRE(*resolved == (tree.root / "pkg" / "foo_test.go").lexically_normal());
}

TEST_CASE("TestSourceResolver rejects an absolute path that does not exist", "[TestSourceResolver]") {
    const TempTree tree("absolute_missing");
    tree.Write("pkg/foo_test.go");

    TestSourceResolver resolver(tree.root);
    // Deliberately not "search for the basename anyway": an absolute path is
    // the framework stating the location outright, so a miss is a real miss.
    REQUIRE_FALSE(resolver.Resolve((tree.root / "nope" / "foo_test.go").string(), "").has_value());
}

TEST_CASE("TestSourceResolver resolves a root-relative path without walking", "[TestSourceResolver]") {
    const TempTree tree("root_relative");
    tree.Write("internal/sub/bar_test.go");

    TestSourceResolver resolver(tree.root);
    const auto         resolved = resolver.Resolve("internal/sub/bar_test.go", "");

    REQUIRE(resolved.has_value());
    REQUIRE(*resolved == (tree.root / "internal" / "sub" / "bar_test.go").lexically_normal());
}

TEST_CASE("TestSourceResolver finds a bare basename by walking the project", "[TestSourceResolver]") {
    const TempTree tree("basename");
    tree.Write("internal/sub/only_test.go");

    TestSourceResolver resolver(tree.root);
    const auto         resolved = resolver.Resolve("only_test.go", "");

    REQUIRE(resolved.has_value());
    REQUIRE(*resolved == (tree.root / "internal" / "sub" / "only_test.go"));
}

TEST_CASE("TestSourceResolver disambiguates same-named files by go package path", "[TestSourceResolver]") {
    // The exact multi-directory-module case Gap C names: go's testing package
    // prints filepath.Base(file), so both of these arrive as "foo_test.go"
    // and only the import path can tell them apart.
    const TempTree tree("package_hint");
    tree.Write("internal/alpha/foo_test.go");
    tree.Write("internal/beta/foo_test.go");

    TestSourceResolver resolver(tree.root);

    const auto alpha = resolver.Resolve("foo_test.go", "github.com/org/mod/internal/alpha");
    REQUIRE(alpha.has_value());
    REQUIRE(*alpha == (tree.root / "internal" / "alpha" / "foo_test.go"));

    const auto beta = resolver.Resolve("foo_test.go", "github.com/org/mod/internal/beta");
    REQUIRE(beta.has_value());
    REQUIRE(*beta == (tree.root / "internal" / "beta" / "foo_test.go"));
}

TEST_CASE("TestSourceResolver probes the longest package-path suffix first", "[TestSourceResolver]") {
    // Both directories are a legitimate suffix of the import path, so the
    // most specific one has to win -- and this resolves by stat, never
    // reaching the directory walk (which is why the go case, the one that
    // always needs resolving, doesn't cost a walk).
    const TempTree tree("suffix_order");
    tree.Write("sub/foo_test.go");
    tree.Write("internal/sub/foo_test.go");

    TestSourceResolver resolver(tree.root);
    const auto         resolved = resolver.Resolve("foo_test.go", "github.com/org/mod/internal/sub");

    REQUIRE(resolved.has_value());
    REQUIRE(*resolved == (tree.root / "internal" / "sub" / "foo_test.go").lexically_normal());
}

TEST_CASE("TestSourceResolver prefers a deeper reported path over a bare basename match", "[TestSourceResolver]") {
    const TempTree tree("path_suffix");
    tree.Write("alpha/helper_test.py");
    tree.Write("beta/helper_test.py");

    TestSourceResolver resolver(tree.root);
    // "beta/helper_test.py" shares two trailing components with the beta
    // candidate and only one with alpha's, so the reported path itself
    // disambiguates without any package hint. (Not resolvable by the
    // root-relative step: the run's cwd was some other directory, so the
    // reported prefix doesn't start at the root.)
    const auto resolved = resolver.Resolve("out/beta/helper_test.py", "");

    REQUIRE(resolved.has_value());
    REQUIRE(*resolved == (tree.root / "beta" / "helper_test.py"));
}

TEST_CASE("TestSourceResolver ties break deterministically on the shallowest candidate", "[TestSourceResolver]") {
    const TempTree tree("ambiguous");
    tree.Write("deep/nested/dir/same_test.go");
    tree.Write("top/same_test.go");

    TestSourceResolver resolver(tree.root);
    const auto         first = resolver.Resolve("same_test.go", "");
    TestSourceResolver second_resolver(tree.root);
    const auto         second = second_resolver.Resolve("same_test.go", "");

    REQUIRE(first.has_value());
    REQUIRE(*first == (tree.root / "top" / "same_test.go"));
    // Never dependent on directory-iteration order.
    REQUIRE(second == first);
}

TEST_CASE("TestSourceResolver reports nothing when the file is absent entirely", "[TestSourceResolver]") {
    const TempTree tree("missing");
    tree.Write("pkg/other_test.go");

    TestSourceResolver resolver(tree.root);
    // The caller's cue to leave the framework's own text alone rather than
    // hand OpenOrCreateFile a path that would silently become an empty buffer.
    REQUIRE_FALSE(resolver.Resolve("absent_test.go", "").has_value());
    REQUIRE_FALSE(resolver.Resolve("", "").has_value());
}

TEST_CASE("TestSourceResolver skips dot-directories and gitignored paths", "[TestSourceResolver]") {
    const TempTree tree("ignored");
    std::ofstream(tree.root / ".gitignore") << "build/\n";
    tree.Write("build/generated_test.go");
    tree.Write(".hidden/hidden_test.go");
    tree.Write("src/real_test.go");

    TestSourceResolver resolver(tree.root);
    REQUIRE_FALSE(resolver.Resolve("generated_test.go", "").has_value());
    REQUIRE_FALSE(resolver.Resolve("hidden_test.go", "").has_value());

    const auto real = resolver.Resolve("real_test.go", "");
    REQUIRE(real.has_value());
    REQUIRE(*real == (tree.root / "src" / "real_test.go"));
}

TEST_CASE("TestSourceResolver with an empty root resolves nothing by walking", "[TestSourceResolver]") {
    TestSourceResolver resolver(std::filesystem::path{});
    REQUIRE_FALSE(resolver.Resolve("anything_test.go", "").has_value());
}
