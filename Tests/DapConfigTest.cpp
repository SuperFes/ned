#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Dap/DapConfig.h"

using ned::editor::dap::DapAdapterCommand;
using ned::editor::dap::DapLaunchConfig;
using ned::editor::dap::SetDapAdapterCommand;
using ned::editor::dap::SetDapLaunchConfig;

// DapConfig is process-wide state (mutex-guarded statics, same as
// LspServerConfig) -- every test here uses its own unique language key and
// clears it on the way out, so tests can't contaminate each other.

TEST_CASE("DapAdapterCommand returns nullopt for an unconfigured language", "[Dap]") {
    REQUIRE_FALSE(DapAdapterCommand("dap-config-test-unset").has_value());
    REQUIRE_FALSE(DapLaunchConfig("dap-config-test-unset").has_value());
}

TEST_CASE("SetDapAdapterCommand stores and clears the adapter argv", "[Dap]") {
    SetDapAdapterCommand("dap-config-test-adapter", {"fake-adapter", "--flag"});
    const auto argv = DapAdapterCommand("dap-config-test-adapter");
    REQUIRE(argv.has_value());
    REQUIRE(*argv == std::vector<std::string>{"fake-adapter", "--flag"});

    SetDapAdapterCommand("dap-config-test-adapter", {}); // empty clears
    REQUIRE_FALSE(DapAdapterCommand("dap-config-test-adapter").has_value());
}

TEST_CASE("SetDapLaunchConfig stores and clears the launch JSON verbatim", "[Dap]") {
    const std::string json = R"({"program": "./a.out", "args": ["x"]})";
    SetDapLaunchConfig("dap-config-test-launch", json);
    const auto stored = DapLaunchConfig("dap-config-test-launch");
    REQUIRE(stored.has_value());
    REQUIRE(*stored == json); // verbatim -- not parsed/normalized here

    SetDapLaunchConfig("dap-config-test-launch", ""); // empty clears
    REQUIRE_FALSE(DapLaunchConfig("dap-config-test-launch").has_value());
}
