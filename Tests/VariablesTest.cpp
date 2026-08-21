#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Variables.h"

using ned::editor::Variable;
using ned::editor::VariablesPath;
using ned::editor::VariableStore;

namespace {

// Mirrors ThemeFileTest's EnvVarGuard, against the state-dir variable.
class EnvVarGuard {
  public:
    EnvVarGuard(const char* name, const char* value) : name_(name) {
        if (const char* existing = std::getenv(name)) {
            hadPrevious_ = true;
            previous_    = existing;
        }
        if (value != nullptr) {
            setenv(name, value, 1);
        }
        else {
            unsetenv(name);
        }
    }

    ~EnvVarGuard() {
        if (hadPrevious_) {
            setenv(name_.c_str(), previous_.c_str(), 1);
        }
        else {
            unsetenv(name_.c_str());
        }
    }

  private:
    std::string name_;
    bool        hadPrevious_ = false;
    std::string previous_;
};

} // namespace

TEST_CASE("VariableStore sets, overwrites, and looks up string values", "[Variables]") {
    VariableStore store;
    REQUIRE(store.Count() == 0);
    REQUIRE_FALSE(store.Get("theme").has_value());

    store.Set("theme", "nord");
    REQUIRE(store.Get("theme") == "nord");

    store.Set("theme", "fuchsia"); // overwrite, not accumulate
    REQUIRE(store.Get("theme") == "fuchsia");
    REQUIRE(store.Count() == 1);
}

TEST_CASE("VariableStore round-trips through JSON", "[Variables]") {
    VariableStore store;
    store.Set("theme", "gruvbox-dark");
    store.Set("some_future_variable", "value with spaces");

    const VariableStore restored = VariableStore::FromJson(store.ToJson());
    REQUIRE(restored.Count() == 2);
    REQUIRE(restored.Get("theme") == "gruvbox-dark");
    REQUIRE(restored.Get("some_future_variable") == "value with spaces");
}

TEST_CASE("VariableStore discards malformed JSON and skips non-string values", "[Variables]") {
    REQUIRE(VariableStore::FromJson("not json at all").Count() == 0);
    REQUIRE(VariableStore::FromJson("{}").Count() == 0); // missing "variables" object

    // A future version storing richer values must not break this one.
    const VariableStore mixed = VariableStore::FromJson(R"({"variables": {"theme": "nord", "count": 3}})");
    REQUIRE(mixed.Count() == 1);
    REQUIRE(mixed.Get("theme") == "nord");
}

TEST_CASE("VariableStore saves and loads through a real file; missing file loads empty", "[Variables]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_variables_test";
    std::filesystem::remove_all(dir);
    const std::filesystem::path path = dir / "nested" / "variables.json"; // parent dirs created by SaveToFile

    VariableStore store;
    store.Set("theme", "monokai");
    store.SaveToFile(path);

    VariableStore loaded;
    loaded.LoadFromFile(path);
    REQUIRE(loaded.Get("theme") == "monokai");

    VariableStore missing;
    missing.Set("stale", "x");
    missing.LoadFromFile(dir / "does-not-exist.json");
    REQUIRE(missing.Count() == 0); // load replaces wholesale, even with nothing

    std::filesystem::remove_all(dir);
}

TEST_CASE("VariablesPath prefers XDG_STATE_HOME and falls back to HOME/.local/state", "[Variables]") {
    {
        EnvVarGuard state("XDG_STATE_HOME", "/tmp/ned-var-test-state");
        REQUIRE(VariablesPath() == std::filesystem::path("/tmp/ned-var-test-state/ned/variables.json"));
    }
    {
        EnvVarGuard state("XDG_STATE_HOME", nullptr);
        EnvVarGuard home("HOME", "/tmp/ned-var-test-home");
        REQUIRE(VariablesPath() == std::filesystem::path("/tmp/ned-var-test-home/.local/state/ned/variables.json"));
    }
}

TEST_CASE("SetVariable writes through to VariablesPath and Variable reads it back", "[Variables]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_variables_test_process";
    std::filesystem::remove_all(dir);
    EnvVarGuard state("XDG_STATE_HOME", dir.c_str());

    ned::editor::SetVariable("theme", "tokyo-night");
    REQUIRE(Variable("theme") == "tokyo-night");
    REQUIRE(std::filesystem::exists(dir / "ned" / "variables.json"));

    // A fresh load from the written file (the startup path) sees the value.
    ned::editor::LoadVariables();
    REQUIRE(Variable("theme") == "tokyo-night");

    std::filesystem::remove_all(dir);
    // Leave the process-wide store empty for any later test.
    ned::editor::LoadVariables();
}
