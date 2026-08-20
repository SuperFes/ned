#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Vcs/VcsProviderRegistry.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "JanetTestSupport.h"

using ned::janet::Environment;
using ned::janet::InstallEditorBindings;

namespace {

struct RegistryResetGuard {
    RegistryResetGuard() { ned::editor::vcs::ClearRegistry(); }
    ~RegistryResetGuard() { ned::editor::vcs::ClearRegistry(); }
};

} // namespace

TEST_CASE("ned/vcs-register-provider registers a provider resolvable via ActiveProviderFor", "[JanetVcsProvider]") {
    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"(
      (ned/vcs-register-provider "fake"
        (fn [root] (= root "/repo"))
        (fn [path] ["fake-vcs" "blame" path])
        (fn [stdout] [{:hash "abc123" :author "Ada" :date "2026-01-01" :summary "did a thing"}])
        (fn [path] ["fake-vcs" "log" path])
        (fn [stdout] [{:hash "def456" :author "Bea" :date "2026-01-02" :summary "did another thing"}]))
    )");

    auto* provider = ned::editor::vcs::ActiveProviderFor("/repo");
    REQUIRE(provider != nullptr);
    REQUIRE(ned::editor::vcs::ActiveProviderFor("/not-repo") == nullptr);

    const auto blameArgv = provider->BlameArgv("/repo/file.txt");
    REQUIRE(blameArgv.argv == std::vector<std::string>{"fake-vcs", "blame", "/repo/file.txt"});

    const auto blameLines = provider->ParseBlame("irrelevant raw output");
    REQUIRE(blameLines.size() == 1);
    REQUIRE(blameLines[0].commitHash == "abc123");
    REQUIRE(blameLines[0].author == "Ada");
    REQUIRE(blameLines[0].date == "2026-01-01");
    REQUIRE(blameLines[0].summary == "did a thing");

    const auto logArgv = provider->LogArgv("/repo/file.txt");
    REQUIRE(logArgv.argv == std::vector<std::string>{"fake-vcs", "log", "/repo/file.txt"});

    const auto logEntries = provider->ParseLog("irrelevant raw output");
    REQUIRE(logEntries.size() == 1);
    REQUIRE(logEntries[0].commitHash == "def456");
    REQUIRE(logEntries[0].author == "Bea");
}

TEST_CASE("ned/vcs-register-provider's parse callbacks degrade missing fields to empty strings, not a crash",
          "[JanetVcsProvider]") {
    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"(
      (ned/vcs-register-provider "sparse"
        (fn [root] true)
        (fn [path] ["true"])
        (fn [stdout] [{:hash "abc123"}])
        (fn [path] ["true"])
        (fn [stdout] []))
    )");

    auto* provider = ned::editor::vcs::ActiveProviderFor("/anything");
    REQUIRE(provider != nullptr);

    const auto blameLines = provider->ParseBlame("x");
    REQUIRE(blameLines.size() == 1);
    REQUIRE(blameLines[0].commitHash == "abc123");
    REQUIRE(blameLines[0].author.empty());
    REQUIRE(blameLines[0].date.empty());
    REQUIRE(blameLines[0].summary.empty());

    REQUIRE(provider->ParseLog("x").empty());
}

TEST_CASE("ned/vcs-register-provider re-registering the same name replaces the previous provider",
          "[JanetVcsProvider]") {
    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"(
      (ned/vcs-register-provider "replaceable"
        (fn [root] true) (fn [path] ["v1"]) (fn [stdout] []) (fn [path] ["v1"]) (fn [stdout] []))
    )");
    env.DoString(R"(
      (ned/vcs-register-provider "replaceable"
        (fn [root] true) (fn [path] ["v2"]) (fn [stdout] []) (fn [path] ["v2"]) (fn [stdout] []))
    )");

    auto* provider = ned::editor::vcs::ActiveProviderFor("/anything");
    REQUIRE(provider != nullptr);
    REQUIRE(provider->BlameArgv("x").argv == std::vector<std::string>{"v2"});
}
