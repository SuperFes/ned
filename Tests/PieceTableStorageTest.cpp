#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Text/ITextStorage.h"
#include "Text/PieceTable.h"
#include "Text/PieceTableStorage.h"

using ned::text::ITextStorage;
using ned::text::PieceTable;
using ned::text::PieceTableStorage;

namespace {
std::filesystem::path WriteTempFile(const std::string& name, std::string_view content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream                file(path, std::ios::binary);
    file << content;
    return path;
}
} // namespace

TEST_CASE("PieceTableStorage IsHuge is true, unlike RopeStorage", "[PieceTableStorage]") {
    const PieceTableStorage storage;
    REQUIRE(storage.IsHuge());
}

TEST_CASE("PieceTableStorage delegates content queries to the wrapped PieceTable", "[PieceTableStorage]") {
    const std::filesystem::path path = WriteTempFile("ned_piecetablestorage_basic.txt", "hello, world\nsecond line\n");
    const PieceTableStorage      storage(PieceTable::FromFile(path));

    REQUIRE_FALSE(storage.Empty());
    REQUIRE(storage.ByteLength() == 25);
    REQUIRE(storage.LineCount() == 3);
    REQUIRE(storage.ToString() == "hello, world\nsecond line\n");
    REQUIRE(storage.Substring(0, 5) == "hello");
    REQUIRE(storage.ByteOffsetToLine(14) == 1);
    REQUIRE(storage.LineToByteOffset(1) == 13);

    std::filesystem::remove(path);
}

TEST_CASE("PieceTableStorage Inserted/Erased return new IsHuge storage, non-mutating", "[PieceTableStorage]") {
    const std::filesystem::path path = WriteTempFile("ned_piecetablestorage_edit.txt", "hello world");
    const PieceTableStorage      original(PieceTable::FromFile(path));

    const std::unique_ptr<ITextStorage> inserted = original.Inserted(5, ",");
    REQUIRE(inserted->IsHuge());
    REQUIRE(inserted->ToString() == "hello, world");
    REQUIRE(original.ToString() == "hello world"); // unchanged

    const std::unique_ptr<ITextStorage> erased = original.Erased(5, 6);
    REQUIRE(erased->ToString() == "hello");
    REQUIRE(original.ToString() == "hello world"); // unchanged

    std::filesystem::remove(path);
}

TEST_CASE("PieceTableStorage Clone is an independent, equal-content copy", "[PieceTableStorage]") {
    const std::filesystem::path path = WriteTempFile("ned_piecetablestorage_clone.txt", "clone me");
    const PieceTableStorage      original(PieceTable::FromFile(path));

    const std::unique_ptr<ITextStorage> cloned = original.Clone();
    REQUIRE(cloned->IsHuge());
    REQUIRE(cloned->ToString() == "clone me");

    const std::unique_ptr<ITextStorage> editedClone = cloned->Inserted(0, "did I ");
    REQUIRE(editedClone->ToString() == "did I clone me");
    REQUIRE(cloned->ToString() == "clone me");   // the clone itself is untouched by editing its own derivative
    REQUIRE(original.ToString() == "clone me"); // and the original is untouched by any of it

    std::filesystem::remove(path);
}

TEST_CASE("PieceTableStorage codepoint navigation matches ITextStorage's UTF-8 contract", "[PieceTableStorage]") {
    const std::string           text  = "h\xC3\xA9llo"; // "héllo"
    const std::filesystem::path path  = WriteTempFile("ned_piecetablestorage_utf8.txt", text);
    const PieceTableStorage      storage(PieceTable::FromFile(path));

    REQUIRE(storage.ByteLength() == 6);
    REQUIRE(storage.CodepointLength() == 5);

    const auto decoded = storage.CodepointAt(1);
    REQUIRE(decoded.codepoint == static_cast<char32_t>(0x00E9));
    REQUIRE(decoded.byteLength == 2);

    REQUIRE(storage.NextCodepointBoundary(1) == 3);
    REQUIRE(storage.PreviousCodepointBoundary(3) == 1);
    REQUIRE(storage.ByteOffsetToCodepointOffset(3) == 2);
    REQUIRE(storage.CodepointOffsetToByteOffset(2) == 3);

    std::filesystem::remove(path);
}

TEST_CASE("PieceTableStorage Value exposes the wrapped PieceTable unchanged", "[PieceTableStorage]") {
    const std::filesystem::path path = WriteTempFile("ned_piecetablestorage_value.txt", "raw access");
    const PieceTableStorage      storage(PieceTable::FromFile(path));

    REQUIRE(storage.Value().ToString() == "raw access");

    std::filesystem::remove(path);
}
