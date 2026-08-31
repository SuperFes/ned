#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "Text/MappedFile.h"
#include "Text/PieceTable.h"

using ned::text::MappedFileError;
using ned::text::PieceTable;

namespace {
std::filesystem::path WriteTempFile(const std::string& name, std::string_view content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream                file(path, std::ios::binary);
    file << content;
    return path;
}

#if defined(__linux__)
// VmRSS in kB, per proc(5) -- the one portable-enough-for-this-repo way to
// observe actual resident memory rather than just trusting the design.
std::size_t CurrentRssKb() {
    std::ifstream status("/proc/self/status");
    std::string   line;
    while (std::getline(status, line)) {
        if (line.starts_with("VmRSS:")) {
            return static_cast<std::size_t>(std::stoul(line.substr(line.find_first_of("0123456789"))));
        }
    }
    return 0;
}
#endif
} // namespace

TEST_CASE("Empty piece table", "[PieceTable]") {
    const PieceTable table;

    REQUIRE(table.Empty());
    REQUIRE(table.ByteLength() == 0);
    REQUIRE(table.CodepointLength() == 0);
    REQUIRE(table.LineCount() == 1);
    REQUIRE(table.ToString().empty());
}

TEST_CASE("PieceTable::FromFile maps ASCII content", "[PieceTable]") {
    const std::filesystem::path path  = WriteTempFile("ned_piecetable_ascii.txt", "hello");
    const PieceTable             table = PieceTable::FromFile(path);

    REQUIRE_FALSE(table.Empty());
    REQUIRE(table.ByteLength() == 5);
    REQUIRE(table.CodepointLength() == 5);
    REQUIRE(table.ToString() == "hello");

    std::filesystem::remove(path);
}

TEST_CASE("PieceTable::FromFile content is correct after its internal page release", "[PieceTable]") {
    // FromFile releases the pages its own initial scan touched (see its
    // own comment / MappedFile.h's memory-residency model) -- this exists
    // to confirm that optimization never costs correctness: every byte
    // must still read back right after the mapping's pages were dropped
    // and re-faulted on demand.
    std::string content;
    content.reserve(50000);
    for (int i = 0; i < 50000; ++i) {
        content.push_back(static_cast<char>('a' + (i % 26)));
        if (i % 77 == 76) {
            content.push_back('\n');
        }
    }

    const std::filesystem::path path  = WriteTempFile("ned_piecetable_release.txt", content);
    const PieceTable             table = PieceTable::FromFile(path);

    REQUIRE(table.ByteLength() == content.size());
    REQUIRE(table.ToString() == content);
    REQUIRE(table.Substring(12345, 100) == content.substr(12345, 100));

    std::filesystem::remove(path);
}

TEST_CASE("PieceTable::FromFile on a zero-byte file is empty", "[PieceTable]") {
    const std::filesystem::path path  = WriteTempFile("ned_piecetable_empty.txt", "");
    const PieceTable             table = PieceTable::FromFile(path);

    REQUIRE(table.Empty());
    REQUIRE(table.ByteLength() == 0);
    REQUIRE(table.LineCount() == 1);

    std::filesystem::remove(path);
}

TEST_CASE("PieceTable::FromFile throws MappedFileError for a nonexistent path", "[PieceTable]") {
    REQUIRE_THROWS_AS(PieceTable::FromFile(std::filesystem::temp_directory_path() / "ned_piecetable_does_not_exist.txt"), MappedFileError);
}

TEST_CASE("PieceTable insert and erase are non-mutating", "[PieceTable]") {
    const std::filesystem::path path     = WriteTempFile("ned_piecetable_nonmutating.txt", "hello world");
    const PieceTable             original = PieceTable::FromFile(path);

    const PieceTable inserted = original.Inserted(5, ",");
    REQUIRE(inserted.ToString() == "hello, world");
    REQUIRE(original.ToString() == "hello world"); // unchanged

    const PieceTable erased = original.Erased(5, 6);
    REQUIRE(erased.ToString() == "hello");
    REQUIRE(original.ToString() == "hello world"); // unchanged

    std::filesystem::remove(path);
}

TEST_CASE("PieceTable insert at boundaries", "[PieceTable]") {
    const std::filesystem::path path  = WriteTempFile("ned_piecetable_boundaries.txt", "world");
    const PieceTable             table = PieceTable::FromFile(path);

    REQUIRE(table.Inserted(0, "hello ").ToString() == "hello world");
    REQUIRE(table.Inserted(5, "!").ToString() == "world!");

    std::filesystem::remove(path);
}

TEST_CASE("PieceTable insert on top of insert exercises the shared append buffer", "[PieceTable]") {
    const std::filesystem::path path  = WriteTempFile("ned_piecetable_double_insert.txt", "ac");
    const PieceTable             base  = PieceTable::FromFile(path);
    const PieceTable             once  = base.Inserted(1, "b");
    const PieceTable             twice = once.Inserted(3, "d");

    // Every intermediate snapshot must stay independently correct even
    // though `once` and `twice` share the same underlying append buffer.
    REQUIRE(base.ToString() == "ac");
    REQUIRE(once.ToString() == "abc");
    REQUIRE(twice.ToString() == "abcd");

    std::filesystem::remove(path);
}

TEST_CASE("PieceTable erase spanning original and added text", "[PieceTable]") {
    const std::filesystem::path path  = WriteTempFile("ned_piecetable_erase_span.txt", "helloworld");
    const PieceTable             base  = PieceTable::FromFile(path);
    const PieceTable             mixed = base.Inserted(5, ", "); // "hello, world"

    REQUIRE(mixed.ToString() == "hello, world");

    const PieceTable erased = mixed.Erased(3, 6); // remove "lo, wo" -> "helrld"
    REQUIRE(erased.ToString() == "helrld");
    REQUIRE(mixed.ToString() == "hello, world"); // unchanged

    std::filesystem::remove(path);
}

TEST_CASE("PieceTable multi-byte UTF-8 counts bytes vs codepoints correctly", "[PieceTable]") {
    const std::string           text  = "h\xC3\xA9llo"; // "héllo"
    const std::filesystem::path path  = WriteTempFile("ned_piecetable_utf8.txt", text);
    const PieceTable             table = PieceTable::FromFile(path);

    REQUIRE(table.ByteLength() == 6);
    REQUIRE(table.CodepointLength() == 5);
    REQUIRE(table.ToString() == text);

    const auto decoded = table.CodepointAt(1);
    REQUIRE(decoded.codepoint == static_cast<char32_t>(0x00E9));
    REQUIRE(decoded.byteLength == 2);

    REQUIRE(table.NextCodepointBoundary(1) == 3);
    REQUIRE(table.PreviousCodepointBoundary(3) == 1);

    std::filesystem::remove(path);
}

TEST_CASE("PieceTable line counting", "[PieceTable]") {
    const std::filesystem::path path  = WriteTempFile("ned_piecetable_lines.txt", "a\nb\nc");
    const PieceTable             table = PieceTable::FromFile(path);

    REQUIRE(table.LineCount() == 3);
    REQUIRE(table.ByteOffsetToLine(0) == 0);
    REQUIRE(table.ByteOffsetToLine(2) == 1);
    REQUIRE(table.ByteOffsetToLine(4) == 2);
    REQUIRE(table.LineToByteOffset(0) == 0);
    REQUIRE(table.LineToByteOffset(1) == 2);
    REQUIRE(table.LineToByteOffset(2) == 4);

    std::filesystem::remove(path);
}

TEST_CASE("PieceTable codepoint/byte offset conversions", "[PieceTable]") {
    const std::string           text  = "h\xC3\xA9llo"; // "héllo"
    const std::filesystem::path path  = WriteTempFile("ned_piecetable_offsets.txt", text);
    const PieceTable             table = PieceTable::FromFile(path);

    REQUIRE(table.ByteOffsetToCodepointOffset(0) == 0);
    REQUIRE(table.ByteOffsetToCodepointOffset(1) == 1);
    REQUIRE(table.ByteOffsetToCodepointOffset(3) == 2);
    REQUIRE(table.ByteOffsetToCodepointOffset(6) == 5);

    REQUIRE(table.CodepointOffsetToByteOffset(0) == 0);
    REQUIRE(table.CodepointOffsetToByteOffset(1) == 1);
    REQUIRE(table.CodepointOffsetToByteOffset(2) == 3);
    REQUIRE(table.CodepointOffsetToByteOffset(5) == 6);

    std::filesystem::remove(path);
}

TEST_CASE("PieceTable Substring matches Rope-style semantics", "[PieceTable]") {
    const std::filesystem::path path  = WriteTempFile("ned_piecetable_substring.txt", "hello, world");
    const PieceTable             table = PieceTable::FromFile(path);

    REQUIRE(table.Substring(0, 5) == "hello");
    REQUIRE(table.Substring(7, 5) == "world");
    REQUIRE(table.Substring(7, 100) == "world"); // clamped past end
}

TEST_CASE("PieceTable::ForEachChunk reconstructs the same content as ToString", "[PieceTable]") {
    const std::filesystem::path path = WriteTempFile("ned_piecetable_chunks.txt", "hello world, this is a test file for chunk streaming");
    const PieceTable             base = PieceTable::FromFile(path);
    const PieceTable             table = base.Inserted(5, " there").Erased(0, 1);

    std::string reconstructed;
    table.ForEachChunk([&](std::string_view chunk) { reconstructed += chunk; });

    REQUIRE(reconstructed == table.ToString());

    std::filesystem::remove(path);
}

TEST_CASE("PieceTable survives random edits across chunk boundaries and origins", "[PieceTable]") {
    // Seed a file well past kChunkSize (512 in PieceTable.cpp) so edits
    // exercise Split crossing between leaves, and mix in enough inserted
    // text that later edits also cross between original-file spans and
    // added-buffer spans.
    std::string seed;
    for (int i = 0; i < 2000; ++i) {
        seed.push_back(static_cast<char>('A' + (i % 26)));
        if (i % 60 == 59) {
            seed.push_back('\n');
        }
    }

    const std::filesystem::path path = WriteTempFile("ned_piecetable_random.txt", seed);

    std::string reference = seed;
    PieceTable  table     = PieceTable::FromFile(path);

    std::mt19937                       rng(4321); // fixed seed: deterministic test
    std::uniform_int_distribution<int> opDist(0, 1);
    std::uniform_int_distribution<int> lenDist(1, 40);

    for (int iteration = 0; iteration < 1000; ++iteration) {
        if (reference.empty() || opDist(rng) == 0) {
            std::uniform_int_distribution<std::size_t> posDist(0, reference.size());
            const std::size_t                          pos = posDist(rng);
            const int                                  len = lenDist(rng);

            std::string chunk;
            chunk.reserve(static_cast<std::size_t>(len));
            for (int i = 0; i < len; ++i) {
                chunk.push_back(static_cast<char>('a' + (i % 26)));
            }

            reference.insert(pos, chunk);
            table = table.Inserted(pos, chunk);
        } else {
            std::uniform_int_distribution<std::size_t> posDist(0, reference.size());
            const std::size_t                          pos    = posDist(rng);
            const std::size_t                          maxLen = reference.size() - pos;
            std::uniform_int_distribution<std::size_t> eraseLenDist(0, maxLen);
            const std::size_t                          len = eraseLenDist(rng);

            reference.erase(pos, len);
            table = table.Erased(pos, len);
        }

        REQUIRE(table.ByteLength() == reference.size());
        REQUIRE(table.ToString() == reference);
    }

    std::string reconstructed;
    table.ForEachChunk([&](std::string_view chunk) { reconstructed += chunk; });
    REQUIRE(reconstructed == reference);

    std::filesystem::remove(path);
}

#if defined(__linux__)
TEST_CASE("PieceTable::FromFile does not leave a large file fully resident", "[PieceTable][memory]") {
    // The whole point of this type: opening a huge file must not pull it
    // into RAM. Write a file large enough that "accidentally resident"
    // would be obvious (well past kOriginalChunkSize's 256 KiB and past
    // any plausible baseline noise), open it, and check actual RSS growth
    // rather than trusting the design -- this is a regression test for the
    // memory-residency model documented in MappedFile.h/PieceTable.h, not
    // just a correctness check.
    constexpr std::size_t kFileSize = 200 * 1024 * 1024; // 200 MiB

    std::string chunk(1024, 'x');
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        chunk[i] = static_cast<char>('a' + (i % 26));
    }

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_piecetable_rss.txt";
    {
        std::ofstream file(path, std::ios::binary);
        for (std::size_t written = 0; written < kFileSize; written += chunk.size()) {
            file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        }
    }

    const std::size_t rssBeforeKb = CurrentRssKb();
    const PieceTable   table       = PieceTable::FromFile(path);
    const std::size_t rssAfterKb  = CurrentRssKb();

    REQUIRE(table.ByteLength() == kFileSize);

    // Generous margin above the tree's own node overhead (a few hundred KB
    // at 256 KiB chunks for a 200 MiB file) -- the real failure mode this
    // guards against is "the whole 200 MiB ended up resident", which would
    // blow well past this bound, not a tight budget on node bookkeeping.
    const std::size_t growthKb = rssAfterKb > rssBeforeKb ? rssAfterKb - rssBeforeKb : 0;
    REQUIRE(growthKb < kFileSize / 1024 / 4); // < 50 MiB, vs. a 200 MiB file -- measured in practice: ~200 KiB

    std::filesystem::remove(path);
}
#endif
