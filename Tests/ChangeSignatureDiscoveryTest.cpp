//
// The pure discovery half of change-signature (Editor/ChangeSignature.h's
// DiscoverSignatureAndCallSites/CandidatePattern): given a name, a target
// arity and a set of candidate files, which call sites and which other
// same-name signatures need rewriting too. Every candidate's text and its
// own Mode::signatures/Mode::calls output are hand-built and handed in via
// the SourceLookup/FileScanner hooks -- no filesystem, no Mode, no Buffer.
//

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Editor/ChangeSignature.h"
#include "Editor/ModeOverrides.h"

using ned::editor::CallMarker;
using ned::editor::SignatureMarker;
using ned::editor::SignatureParameter;
using ned::editor::changesig::CandidatePattern;
using ned::editor::changesig::DiscoverSignatureAndCallSites;
using ned::editor::changesig::DiscoveryResult;
using ned::editor::changesig::FileScanResult;

namespace {

// A small in-memory project: path -> text, plus canned per-path scan
// results, wired into DiscoverSignatureAndCallSites through its two hooks.
struct Project {
    std::unordered_map<std::filesystem::path, std::string>        text;
    std::unordered_map<std::filesystem::path, FileScanResult> scans;

    [[nodiscard]] std::vector<std::filesystem::path> Paths() const {
        std::vector<std::filesystem::path> paths;
        for (const auto& [path, contents] : text) {
            paths.push_back(path);
        }
        return paths;
    }

    [[nodiscard]] DiscoveryResult Discover(std::string_view name, std::size_t targetArity) const {
        SignatureMarker target{};
        target.parameters.resize(targetArity);
        return DiscoverSignatureAndCallSites(
            name, target, Paths(),
            [this](const std::filesystem::path& path) -> std::optional<std::string> {
                const auto found = text.find(path);
                return found != text.end() ? std::optional<std::string>(found->second) : std::nullopt;
            },
            [this](const std::filesystem::path& path, std::string_view) -> FileScanResult {
                const auto found = scans.find(path);
                return found != scans.end() ? found->second : FileScanResult{};
            });
    }
};

SignatureMarker Signature(std::string_view text, std::string_view name, std::size_t arity) {
    const std::size_t start = text.find(name);
    REQUIRE(start != std::string_view::npos);
    SignatureMarker marker{};
    marker.startByte     = 0;
    marker.endByte       = text.size();
    marker.nameStartByte     = start;
    marker.nameEndByte       = start + name.size();
    marker.callNameStartByte = marker.nameStartByte;
    marker.callNameEndByte   = marker.nameEndByte;
    marker.parameters.resize(arity);
    return marker;
}

CallMarker Call(std::string_view text, std::string_view callee) {
    const std::size_t start = text.find(callee);
    REQUIRE(start != std::string_view::npos);
    CallMarker marker{};
    marker.startByte       = 0;
    marker.endByte         = text.size();
    marker.calleeStartByte = start;
    marker.calleeEndByte   = start + callee.size();
    return marker;
}

} // namespace

TEST_CASE("DiscoverSignatureAndCallSites finds call sites spread across candidate files", "[ChangeSignature]") {
    Project project;
    project.text["a.cpp"]  = "void run() { add(1, 2); sub(3, 4); }";
    project.scans["a.cpp"] = FileScanResult{.calls = {Call(project.text["a.cpp"], "add"), Call(project.text["a.cpp"], "sub")}};
    project.text["b.cpp"]  = "int main() { return add(5, 6); }";
    project.scans["b.cpp"] = FileScanResult{.calls = {Call(project.text["b.cpp"], "add")}};

    const DiscoveryResult result = project.Discover("add", 2);
    REQUIRE(result.callSites.size() == 2);
    CHECK(result.arityMismatches == 0);
    CHECK(result.filesSkipped == 0);
}

TEST_CASE("DiscoverSignatureAndCallSites matches a constructor by the name its callers spell", "[ChangeSignature]") {
    const std::string text = "class Widget { function __construct($a) {} }\nnew Widget(1);\n";
    SignatureMarker   ctor = Signature(text, "__construct", 1);
    const std::size_t cls  = text.find("Widget");
    ctor.callNameStartByte = cls;
    ctor.callNameEndByte   = cls + std::string_view("Widget").size();
    const CallMarker call  = Call(text.substr(0), "Widget(1)");
    CallMarker       fixed = call;
    fixed.calleeEndByte    = fixed.calleeStartByte + std::string_view("Widget").size();

    const DiscoveryResult result = DiscoverSignatureAndCallSites(
        "Widget", ctor, {"w.php"}, [&](const std::filesystem::path&) { return std::optional<std::string>(text); },
        [&](const std::filesystem::path&, std::string_view) { return FileScanResult{.signatures = {ctor}, .calls = {fixed}}; });
    CHECK(result.signatureSites.size() == 1);
    CHECK(result.callSites.size() == 1);
}

TEST_CASE("DiscoverSignatureAndCallSites finds a header prototype and its out-of-line definition as two sites",
          "[ChangeSignature]") {
    Project project;
    project.text["Widget.h"]  = "void resize(int w, int h);";
    project.scans["Widget.h"] = FileScanResult{.signatures = {Signature(project.text["Widget.h"], "resize", 2)}};
    project.text["Widget.cpp"] = "void Widget::resize(int w, int h) { }";
    project.scans["Widget.cpp"] =
        FileScanResult{.signatures = {Signature(project.text["Widget.cpp"], "resize", 2)}};

    const DiscoveryResult result = project.Discover("resize", 2);
    REQUIRE(result.signatureSites.size() == 2);
    CHECK(result.arityMismatches == 0);
}

TEST_CASE("DiscoverSignatureAndCallSites counts a different-arity same-name signature as a mismatch, not a site",
          "[ChangeSignature]") {
    Project project;
    project.text["Widget.h"]  = "void resize(int w, int h);";
    project.scans["Widget.h"] = FileScanResult{.signatures = {Signature(project.text["Widget.h"], "resize", 2)}};
    project.text["Other.h"]  = "void resize(int w, int h, int depth);"; // a real overload, different arity
    project.scans["Other.h"] = FileScanResult{.signatures = {Signature(project.text["Other.h"], "resize", 3)}};

    const DiscoveryResult result = project.Discover("resize", 2);
    REQUIRE(result.signatureSites.size() == 1);
    CHECK(result.signatureSites[0].file == "Widget.h");
    CHECK(result.arityMismatches == 1);
}

TEST_CASE("DiscoverSignatureAndCallSites ignores a call or signature belonging to an unrelated name",
          "[ChangeSignature]") {
    Project project;
    project.text["a.cpp"]  = "void run() { add(1, 2); other(3); }";
    project.scans["a.cpp"] = FileScanResult{.calls = {Call(project.text["a.cpp"], "add"), Call(project.text["a.cpp"], "other")}};

    const DiscoveryResult result = project.Discover("add", 2);
    REQUIRE(result.callSites.size() == 1);
}

TEST_CASE("DiscoverSignatureAndCallSites counts a candidate readText declines to read", "[ChangeSignature]") {
    Project project;
    project.text["huge.cpp"] = "add(1, 2);"; // present in Paths(), but never in `text` for the reader below

    SignatureMarker add{};
    add.parameters.resize(2);
    const DiscoveryResult result = DiscoverSignatureAndCallSites(
        "add", add, {"huge.cpp"}, [](const std::filesystem::path&) -> std::optional<std::string> { return std::nullopt; },
        [](const std::filesystem::path&, std::string_view) -> FileScanResult { return {}; });
    CHECK(result.filesSkipped == 1);
    CHECK(result.callSites.empty());
}

TEST_CASE("CandidatePattern word-bounds a plain name and escapes nothing alphanumeric", "[ChangeSignature]") {
    CHECK(CandidatePattern("add") == "\\badd\\b");
    CHECK(CandidatePattern("") == "");
}

namespace {

// Discovery over one real file, scanned by its language's own queries, for
// the first signature named `name`.
DiscoveryResult DiscoverIn(const std::string& modeName, const std::string& text, std::string_view name) {
    const std::optional<ned::editor::Mode> mode = ned::editor::ModeByName(modeName);
    REQUIRE(mode.has_value());
    const std::vector<SignatureMarker> signatures = mode->signatures(text);
    const auto                         target     = std::find_if(signatures.begin(), signatures.end(), [&](const SignatureMarker& signature) {
        return text.substr(signature.callNameStartByte, signature.callNameEndByte - signature.callNameStartByte) == name;
    });
    REQUIRE(target != signatures.end());
    return DiscoverSignatureAndCallSites(
        name, *target, {"file"}, [&](const std::filesystem::path&) { return std::optional<std::string>(text); },
        [&](const std::filesystem::path&, std::string_view scanned) {
            return FileScanResult{.signatures = mode->signatures(scanned), .calls = mode->calls(scanned)};
        });
}

} // namespace

TEST_CASE("Overloads are told apart by how many arguments each accepts", "[ChangeSignature]") {
    const std::string     java  = "class Box {\n"
                                  "    Box(int w) {}\n"
                                  "    Box(int w, int h) {}\n"
                                  "    static void use() { new Box(1); new Box(1, 2); }\n"
                                  "}\n";
    const DiscoveryResult boxes = DiscoverIn("java-mode", java, "Box");
    CHECK(boxes.arityMismatches == 1);
    CHECK(boxes.ambiguousCalls == 0);
    CHECK(boxes.otherOverloadCalls == 1);
    REQUIRE(boxes.callSites.size() == 1);
    CHECK(boxes.callSites[0].call.arguments.size() == 1);

    // A default widens what an overload accepts: g(1, 2) could be either.
    const std::string     kotlin = "fun g(a: Int, b: Int = 0) {}\n"
                                   "fun g(a: Int, b: Int, c: Int = 0) {}\n"
                                   "fun use() { g(1); g(1, 2); g(1, 2, 3) }\n";
    const DiscoveryResult gs     = DiscoverIn("kotlin-mode", kotlin, "g");
    CHECK(gs.ambiguousCalls == 1);
    CHECK(gs.otherOverloadCalls == 1);
}
