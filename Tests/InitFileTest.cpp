#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "Janet/InitFile.h"
#include "Janet/Value.h"
#include "JanetTestSupport.h"

using ned::janet::InitFilePath;
using ned::janet::LoadInitFile;

namespace {

// Saves/restores an environment variable's previous state (including
// "was unset") around a test, so these tests don't leak XDG_CONFIG_HOME/HOME
// overrides into anything else running in this process.
class EnvVarGuard {
  public:
    EnvVarGuard(const char* name, const char* value) : name_(name) {
        if (const char* existing = std::getenv(name)) {
            hadPrevious_ = true;
            previous_    = existing;
        }
        if (value) {
            setenv(name, value, 1);
        } else {
            unsetenv(name);
        }
    }

    ~EnvVarGuard() {
        if (hadPrevious_) {
            setenv(name_.c_str(), previous_.c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

    EnvVarGuard(const EnvVarGuard&)            = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

  private:
    std::string name_;
    bool        hadPrevious_ = false;
    std::string previous_;
};

} // namespace

TEST_CASE("InitFilePath prefers XDG_CONFIG_HOME when set", "[InitFile]") {
    EnvVarGuard xdg("XDG_CONFIG_HOME", "/tmp/ned-xdg-test-config");
    EnvVarGuard home("HOME", "/tmp/ned-xdg-test-home");

    REQUIRE(InitFilePath() == std::filesystem::path("/tmp/ned-xdg-test-config/ned/init.janet"));
}

TEST_CASE("InitFilePath falls back to HOME/.config when XDG_CONFIG_HOME is unset", "[InitFile]") {
    EnvVarGuard xdg("XDG_CONFIG_HOME", nullptr);
    EnvVarGuard home("HOME", "/tmp/ned-xdg-test-home");

    REQUIRE(InitFilePath() == std::filesystem::path("/tmp/ned-xdg-test-home/.config/ned/init.janet"));
}

TEST_CASE("InitFilePath throws when neither XDG_CONFIG_HOME nor HOME is set", "[InitFile]") {
    EnvVarGuard xdg("XDG_CONFIG_HOME", nullptr);
    EnvVarGuard home("HOME", nullptr);

    REQUIRE_THROWS_AS(InitFilePath(), std::runtime_error);
}

TEST_CASE("LoadInitFile is a no-op when the file doesn't exist", "[InitFile]") {
    const std::filesystem::path configDir = std::filesystem::temp_directory_path() / "ned_initfile_test_missing";
    std::filesystem::remove_all(configDir);

    EnvVarGuard xdg("XDG_CONFIG_HOME", configDir.c_str());
    EnvVarGuard home("HOME", nullptr);

    LoadInitFile(ned_tests::TestEnvironment()); // should not throw

    std::filesystem::remove_all(configDir);
}

TEST_CASE("LoadInitFile evaluates the file when it exists", "[InitFile]") {
    const std::filesystem::path configDir = std::filesystem::temp_directory_path() / "ned_initfile_test_present";
    const std::filesystem::path nedDir    = configDir / "ned";
    std::filesystem::create_directories(nedDir);

    {
        std::ofstream initFile(nedDir / "init.janet");
        initFile << "(def ned-initfile-test-marker 4242)";
    }

    EnvVarGuard xdg("XDG_CONFIG_HOME", configDir.c_str());
    EnvVarGuard home("HOME", nullptr);

    auto& env = ned_tests::TestEnvironment();
    LoadInitFile(env);

    REQUIRE(ned::janet::FromJanet<std::int64_t>(env.DoString("ned-initfile-test-marker")) == 4242);

    std::filesystem::remove_all(configDir);
}
