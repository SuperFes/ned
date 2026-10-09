#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include "Editor/Dap/Config.h"

using ned::editor::dap::AdapterCommand;
using ned::editor::dap::AttachConfig;
using ned::editor::dap::LaunchConfig;
using ned::editor::dap::SetAdapterCommand;
using ned::editor::dap::SetAttachConfig;
using ned::editor::dap::SetLaunchConfig;

// Config is process-wide state (mutex-guarded statics, same as
// ServerConfig) -- every test here uses its own unique language key and
// clears it on the way out, so tests can't contaminate each other.

TEST_CASE("AdapterCommand returns nullopt for an unconfigured language", "[Dap]") {
    REQUIRE_FALSE(AdapterCommand("dap-config-test-unset").has_value());
    REQUIRE_FALSE(LaunchConfig("dap-config-test-unset").has_value());
}

TEST_CASE("SetAdapterCommand stores and clears the adapter argv", "[Dap]") {
    SetAdapterCommand("dap-config-test-adapter", {"fake-adapter", "--flag"});
    const auto argv = AdapterCommand("dap-config-test-adapter");
    REQUIRE(argv.has_value());
    REQUIRE(*argv == std::vector<std::string>{"fake-adapter", "--flag"});

    SetAdapterCommand("dap-config-test-adapter", {}); // empty clears
    REQUIRE_FALSE(AdapterCommand("dap-config-test-adapter").has_value());
}

TEST_CASE("SetLaunchConfig stores and clears the launch JSON verbatim", "[Dap]") {
    const std::string json = R"({"program": "./a.out", "args": ["x"]})";
    SetLaunchConfig("dap-config-test-launch", json);
    const auto stored = LaunchConfig("dap-config-test-launch");
    REQUIRE(stored.has_value());
    REQUIRE(*stored == json); // verbatim -- not parsed/normalized here

    SetLaunchConfig("dap-config-test-launch", ""); // empty clears
    REQUIRE_FALSE(LaunchConfig("dap-config-test-launch").has_value());
}

// DAP round 3: SetLaunchConfig's exact sibling.
TEST_CASE("SetAttachConfig stores and clears the attach JSON verbatim", "[Dap]") {
    const std::string json = R"({"processId": 1234})";
    SetAttachConfig("dap-config-test-attach", json);
    const auto stored = AttachConfig("dap-config-test-attach");
    REQUIRE(stored.has_value());
    REQUIRE(*stored == json); // verbatim -- not parsed/normalized here

    SetAttachConfig("dap-config-test-attach", ""); // empty clears
    REQUIRE_FALSE(AttachConfig("dap-config-test-attach").has_value());
}

TEST_CASE("A session's arguments are the project's own, else init.janet's, with ${workspaceFolder} filled in", "[Dap]") {
    using ned::editor::dap::SessionArguments;
    using Json = nlohmann::json;
    struct Clear {
        ~Clear() {
            SetLaunchConfig("dap-config-test-session", "");
            SetAttachConfig("dap-config-test-session", "");
        }
    } clear;
    ned::editor::ProjectSettings settings;
    CHECK_FALSE(SessionArguments(settings, "dap-config-test-session", false, "/srv/app").has_value());

    SetLaunchConfig("dap-config-test-session", R"({"program": "${workspaceFolder}/bin/app", "args": ["--root=${workspaceFolder}"]})");
    CHECK(SessionArguments(settings, "dap-config-test-session", false, "/srv/app") ==
          Json{{"program", "/srv/app/bin/app"}, {"args", Json::array({"--root=/srv/app"})}});

    settings.dapLaunchByLanguage["dap-config-test-session"] = Json{{"program", "${workspaceFolder}/build/run"}, {"stopOnEntry", true}};
    CHECK(SessionArguments(settings, "dap-config-test-session", false, "/home/me/proj") == Json{{"program", "/home/me/proj/build/run"}, {"stopOnEntry", true}});
    // Attach has its own.
    CHECK_FALSE(SessionArguments(settings, "dap-config-test-session", true, "/srv/app").has_value());

    SetAttachConfig("dap-config-test-session", "{not json");
    CHECK_THROWS_AS(SessionArguments(settings, "dap-config-test-session", true, "/srv/app"), std::runtime_error);
}
