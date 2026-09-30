#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "Text/FileUri.h"

using ned::text::FileUriToPath;
using ned::text::PathToFileUri;

TEST_CASE("PathToFileUri leaves unreserved characters and slashes alone", "[FileUri]") {
    REQUIRE(PathToFileUri("/src/a-b_c.d~e/F9.cpp") == "file:///src/a-b_c.d~e/F9.cpp");
}

TEST_CASE("PathToFileUri percent-encodes reserved and special characters", "[FileUri]") {
    REQUIRE(PathToFileUri("/my project/a b.cpp") == "file:///my%20project/a%20b.cpp");
    REQUIRE(PathToFileUri("/x/#1?.txt") == "file:///x/%231%3F.txt");
    REQUIRE(PathToFileUri("/x/50%.txt") == "file:///x/50%25.txt");
    REQUIRE(PathToFileUri("/usr/include/c++/g++-v16") == "file:///usr/include/c%2B%2B/g%2B%2B-v16");
}

TEST_CASE("PathToFileUri encodes each UTF-8 byte of a non-ASCII path", "[FileUri]") {
    REQUIRE(PathToFileUri("/tmp/caf\xC3\xA9") == "file:///tmp/caf%C3%A9");
}

TEST_CASE("FileUriToPath decodes file uris and rejects other schemes", "[FileUri]") {
    REQUIRE(FileUriToPath("file:///src/a%20b.cpp") == std::filesystem::path("/src/a b.cpp"));
    REQUIRE(FileUriToPath("file:///usr/include/c%2b%2b") == std::filesystem::path("/usr/include/c++"));
    REQUIRE_FALSE(FileUriToPath("https://example.com/x"));
    REQUIRE_FALSE(FileUriToPath("ned-buffer://*scratch*"));
}

TEST_CASE("FileUriToPath keeps a stray percent sign verbatim", "[FileUri]") {
    REQUIRE(FileUriToPath("file:///50%") == std::filesystem::path("/50%"));
    REQUIRE(FileUriToPath("file:///50%zz") == std::filesystem::path("/50%zz"));
    REQUIRE(FileUriToPath("file:///a%4") == std::filesystem::path("/a%4"));
}

TEST_CASE("PathToFileUri and FileUriToPath round-trip awkward paths", "[FileUri]") {
    for (const std::string path : {"/plain/file.cpp", "/with space/and#hash?q", "/50%/%41", "/caf\xC3\xA9/[x]{y}@z:w;v=u&t,s$r!q'p(o)n*m+l",
                                   "/tab\there/new\nline"}) {
        CAPTURE(path);
        REQUIRE(FileUriToPath(PathToFileUri(path)) == std::filesystem::path(path));
    }
}
