#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "Editor/Tracker/Credentials.h"

using Catch::Matchers::ContainsSubstring;
using ned::editor::tracker::Connection;
using ned::editor::tracker::PrivateFile;

namespace tracker = ned::editor::tracker;

namespace {

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream      in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// Points $XDG_RUNTIME_DIR at a fresh directory for one test.
class RuntimeDir {
  public:
    RuntimeDir() {
        std::string pattern = (std::filesystem::temp_directory_path() / "ned_tracker_creds_XXXXXX").string();
        REQUIRE(::mkdtemp(pattern.data()) != nullptr);
        dir_ = pattern;
        if (const char* existing = std::getenv("XDG_RUNTIME_DIR")) {
            previous_ = existing;
        }
        ::setenv("XDG_RUNTIME_DIR", dir_.c_str(), 1);
    }

    ~RuntimeDir() {
        if (previous_) {
            ::setenv("XDG_RUNTIME_DIR", previous_->c_str(), 1);
        }
        else {
            ::unsetenv("XDG_RUNTIME_DIR");
        }
        std::error_code ignored;
        std::filesystem::remove_all(dir_, ignored);
    }

    RuntimeDir(const RuntimeDir&)            = delete;
    RuntimeDir& operator=(const RuntimeDir&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const {
        return dir_;
    }

  private:
    std::filesystem::path      dir_;
    std::optional<std::string> previous_;
};

const Connection kWork{.name = "work", .provider = "jira", .email = "me@example.com", .tokenCommand = {"pass", "jira"}};

} // namespace

TEST_CASE("MissingCredentials wants both an email and a token command", "[TrackerCredentials]") {
    CHECK_FALSE(tracker::MissingCredentials(kWork));
    Connection noEmail = kWork;
    noEmail.email.clear();
    CHECK(tracker::MissingCredentials(noEmail) == "connection \"work\" needs an :email and a :token-command");
    Connection noCommand = kWork;
    noCommand.tokenCommand.clear();
    CHECK(tracker::MissingCredentials(noCommand));
}

TEST_CASE("TokenFromOutput trims, and never echoes the output in an error", "[TrackerCredentials]") {
    CHECK(tracker::TokenFromOutput("work", "  s3cret\n", 0) == "s3cret");
    CHECK(tracker::TokenFromOutput("work", "s3cret", 0) == "s3cret");

    const auto failed = tracker::TokenFromOutput("work", "s3cret-ish\n", 1);
    REQUIRE_FALSE(failed);
    CHECK(failed.error() == "connection \"work\": :token-command failed (exit 1)");

    const auto killed = tracker::TokenFromOutput("work", "s3cret", std::nullopt);
    REQUIRE_FALSE(killed);
    CHECK_THAT(killed.error(), ContainsSubstring("couldn't start or was terminated"));
    CHECK_THAT(killed.error(), !ContainsSubstring("s3cret"));

    const auto empty = tracker::TokenFromOutput("work", " \n\t", 0);
    REQUIRE_FALSE(empty);
    CHECK(empty.error() == "connection \"work\": :token-command printed nothing");
}

TEST_CASE("CurlUserConfig quotes the credentials for a curl config file", "[TrackerCredentials]") {
    CHECK(tracker::CurlUserConfig("me@example.com", "abc123") == "user = \"me@example.com:abc123\"\n");
    CHECK(tracker::CurlUserConfig("me@example.com", R"(a"b\c)") == "user = \"me@example.com:a\\\"b\\\\c\"\n");

    const auto broken = tracker::CurlUserConfig("me@example.com", "line1\nline2");
    REQUIRE_FALSE(broken);
    CHECK_THAT(broken.error(), !ContainsSubstring("line1"));
    CHECK_FALSE(tracker::CurlUserConfig("me@example.com\r", "abc"));
}

TEST_CASE("PrivateFile is private to the user and removed with its last owner", "[TrackerCredentials]") {
    RuntimeDir runtime;

    std::filesystem::path path;
    {
        const auto file = PrivateFile::Write("user = \"a:b\"\n");
        REQUIRE(file);
        path = (*file)->Path();
        CHECK(path.parent_path() == runtime.Path());
        CHECK(ReadFile(path) == "user = \"a:b\"\n");
        const auto perms = std::filesystem::status(path).permissions();
        CHECK((perms & std::filesystem::perms::all) == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write));

        const std::shared_ptr<const PrivateFile> copy = *file;
        CHECK(std::filesystem::exists(path));
    }
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("PrivateFile falls back to the temp dir without a usable XDG_RUNTIME_DIR", "[TrackerCredentials]") {
    RuntimeDir runtime;
    ::setenv("XDG_RUNTIME_DIR", (runtime.Path() / "missing").c_str(), 1);
    const auto file = PrivateFile::Write("x");
    REQUIRE(file);
    CHECK((*file)->Path().parent_path() == std::filesystem::temp_directory_path());
}

TEST_CASE("CredentialCommand appends -K naming the connection's curl config", "[TrackerCredentials]") {
    RuntimeDir runtime;

    {
        const auto command = tracker::CredentialCommand(kWork, {"curl", "-sS", "https://example.atlassian.net"}, "tok\n", 0);
        REQUIRE(command);
        REQUIRE(command->argv.size() == 5);
        CHECK(command->argv[3] == "-K");
        CHECK(command->argv[4] == command->file->Path().string());
        CHECK(ReadFile(command->file->Path()) == "user = \"me@example.com:tok\"\n");
        for (const std::string& arg : command->argv) {
            CHECK_THAT(arg, !ContainsSubstring("tok\""));
        }
    }
    CHECK(std::filesystem::is_empty(runtime.Path()));

    const auto failed = tracker::CredentialCommand(kWork, {"curl"}, "secret", 2);
    REQUIRE_FALSE(failed);
    CHECK(failed.error() == "connection \"work\": :token-command failed (exit 2)");

    const auto multiline = tracker::CredentialCommand(kWork, {"curl"}, "first\nsecond", 0);
    REQUIRE_FALSE(multiline);
    CHECK(multiline.error() == "connection \"work\": the email or token contains a line break");
    CHECK(std::filesystem::is_empty(runtime.Path()));
}
