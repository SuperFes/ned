#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>

#include "Text/MappedFile.h"

using ned::text::AccessPattern;
using ned::text::MappedFile;
using ned::text::MappedFileError;

TEST_CASE("MappedFile maps plain text content", "[MappedFile]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_mapped_file_text.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << "hello, world\nsecond line\n";
    }

    MappedFile mapped = MappedFile::Open(path);
    REQUIRE(mapped.Size() == std::filesystem::file_size(path));
    REQUIRE(std::string(mapped.Data(), mapped.Size()) == "hello, world\nsecond line\n");

    std::filesystem::remove(path);
}

TEST_CASE("MappedFile handles a zero-byte file without calling mmap", "[MappedFile]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_mapped_file_empty.txt";
    { std::ofstream file(path, std::ios::binary); }

    MappedFile mapped = MappedFile::Open(path);
    REQUIRE(mapped.Size() == 0);
    REQUIRE(mapped.Data() == nullptr);

    std::filesystem::remove(path);
}

TEST_CASE("MappedFile::Open throws MappedFileError for a nonexistent path", "[MappedFile]") {
    REQUIRE_THROWS_AS(MappedFile::Open(std::filesystem::temp_directory_path() / "ned_mapped_file_does_not_exist.bin"), MappedFileError);
}

TEST_CASE("MappedFile move construction transfers the mapping", "[MappedFile]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_mapped_file_move.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << "movable content";
    }

    MappedFile original = MappedFile::Open(path);
    MappedFile moved(std::move(original));

    REQUIRE(moved.Size() == std::strlen("movable content"));
    REQUIRE(std::string(moved.Data(), moved.Size()) == "movable content");

    std::filesystem::remove(path);
}

TEST_CASE("MappedFile content survives Advise and ReleasePages", "[MappedFile]") {
    // The real contract that matters: dropping pages via ReleasePages must
    // never lose or corrupt data -- a later read has to transparently
    // re-fault from the file. Advise/ReleasePages are residency hints, not
    // part of the correctness surface, so this is what actually needs
    // testing (RSS itself isn't practically observable from a unit test).
    std::string content;
    content.reserve(20000);
    for (int i = 0; i < 20000; ++i) {
        content.push_back(static_cast<char>('a' + (i % 26)));
    }

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_mapped_file_advise.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << content;
    }

    MappedFile mapped = MappedFile::Open(path);
    REQUIRE(mapped.Size() == content.size());

    mapped.Advise(AccessPattern::kSequential);
    REQUIRE(std::string(mapped.Data(), mapped.Size()) == content);

    mapped.ReleasePages(0, mapped.Size());
    mapped.Advise(AccessPattern::kRandom);
    REQUIRE(std::string(mapped.Data(), mapped.Size()) == content); // re-faults transparently

    // Partial release (unaligned on purpose) must not disturb neighboring bytes.
    mapped.ReleasePages(100, 37);
    REQUIRE(std::string(mapped.Data(), mapped.Size()) == content);

    std::filesystem::remove(path);
}

TEST_CASE("MappedFile Advise/ReleasePages on an empty mapping are safe no-ops", "[MappedFile]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_mapped_file_advise_empty.txt";
    { std::ofstream file(path, std::ios::binary); }

    MappedFile mapped = MappedFile::Open(path);
    REQUIRE(mapped.Data() == nullptr);

    mapped.Advise(AccessPattern::kRandom);
    mapped.ReleasePages(0, 100);
    REQUIRE(mapped.Size() == 0);

    std::filesystem::remove(path);
}

TEST_CASE("MappedFile move assignment releases the previous mapping", "[MappedFile]") {
    const std::filesystem::path pathA = std::filesystem::temp_directory_path() / "ned_mapped_file_move_a.txt";
    const std::filesystem::path pathB = std::filesystem::temp_directory_path() / "ned_mapped_file_move_b.txt";
    {
        std::ofstream fileA(pathA, std::ios::binary);
        fileA << "AAAA";
        std::ofstream fileB(pathB, std::ios::binary);
        fileB << "BBBBBBBB";
    }

    MappedFile a = MappedFile::Open(pathA);
    MappedFile b = MappedFile::Open(pathB);
    a            = std::move(b);

    REQUIRE(a.Size() == 8);
    REQUIRE(std::string(a.Data(), a.Size()) == "BBBBBBBB");

    std::filesystem::remove(pathA);
    std::filesystem::remove(pathB);
}
