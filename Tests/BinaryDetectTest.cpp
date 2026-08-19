#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Text/BinaryDetect.h"

using ned::text::LooksBinary;

TEST_CASE("LooksBinary is false for plain text content", "[BinaryDetect]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_binary_detect_text.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << "hello, world\nsecond line\n";
    }
    REQUIRE_FALSE(LooksBinary(path));
    std::filesystem::remove(path);
}

TEST_CASE("LooksBinary is true for content containing a NUL byte", "[BinaryDetect]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_binary_detect_binary.bin";
    {
        std::ofstream file(path, std::ios::binary);
        file.put('a');
        file.put('\0');
        file.put('b');
    }
    REQUIRE(LooksBinary(path));
    std::filesystem::remove(path);
}

TEST_CASE("LooksBinary is true for a nonexistent/unreadable path", "[BinaryDetect]") {
    REQUIRE(LooksBinary(std::filesystem::temp_directory_path() / "ned_binary_detect_does_not_exist.bin"));
}
