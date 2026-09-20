// ITextStorage::SnapshotForBackgroundRead() -- the copy a thread may read
// while the main thread keeps editing.
//
// What these cases cover is that the snapshot reads correct, stable content
// through both storage kinds. What they deliberately do NOT claim to cover
// is the reason the method exists, because it is not observable from a
// single thread: a piece table's spans are re-derived from added->data() on
// every SpanView call and an append only ever extends past them, so a
// snapshot that wrongly SHARED the append buffer would still return the
// right bytes here. The defect it prevents is a data race -- concurrent
// unsynchronized read and write of one std::string -- and only a race
// detector can see it.
//
// That proof was run directly against these sources rather than assumed.
// Building Text/PieceTable.cpp with -fsanitize=thread, a reader thread
// looping ForEachChunk() over a snapshot while the main thread does
// Inserted() on the source reports, for a shared backing:
//
//     WARNING: ThreadSanitizer: data race
//       Write ... PieceTable::Inserted   PieceTable.cpp:316  (added->append)
//       Read  ... PieceTable::SpanView   PieceTable.cpp:119  (added->data())
//
// and reports nothing at all once the snapshot goes through DetachedCopy().
// Re-run that way if this ever needs re-confirming; the repository's
// sanitize preset is ASan/UBSan, which does not detect races.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "Text/Buffer.h"
#include "Text/PieceTableStorage.h"
#include "Text/RopeStorage.h"

using ned::text::Buffer;
using ned::text::PieceTable;
using ned::text::PieceTableStorage;
using ned::text::Rope;
using ned::text::RopeStorage;

namespace {

std::filesystem::path WriteTempFile(const std::string& name, std::string_view content) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("ned_storage_snapshot_" + name + "_" + std::to_string(::getpid()));
    std::ofstream file(path, std::ios::binary);
    file << content;
    return path;
}

} // namespace

TEST_CASE("A rope snapshot is unaffected by later edits to the source", "[StorageSnapshot]") {
    RopeStorage storage{Rope("original content")};

    const std::unique_ptr<ned::text::ITextStorage> snapshot = storage.SnapshotForBackgroundRead();
    storage                                                 = RopeStorage{Rope("entirely different content, much longer than before")};

    REQUIRE(snapshot->ToString() == "original content");
}

TEST_CASE("A piece-table snapshot keeps its own content across appends that reallocate the source's append buffer",
          "[StorageSnapshot]") {
    const std::filesystem::path path = WriteTempFile("realloc.txt", "file contents\n");

    PieceTable table = PieceTable::FromFile(path).Inserted(0, "INSERTED");

    PieceTableStorage                              storage{table};
    const std::unique_ptr<ned::text::ITextStorage> snapshot = storage.SnapshotForBackgroundRead();
    REQUIRE(snapshot->ToString() == "INSERTEDfile contents\n");

    // Each insert appends to the source's append buffer; growing it well
    // past its initial capacity forces it to reallocate several times.
    for (int i = 0; i < 200; ++i) {
        table = table.Inserted(0, std::string(1024, 'x'));
    }

    REQUIRE(snapshot->ToString() == "INSERTEDfile contents\n");
    std::filesystem::remove(path);
}

TEST_CASE("A huge buffer's snapshot does not follow the buffer's own later edits", "[StorageSnapshot]") {
    const std::filesystem::path path = WriteTempFile("huge.txt", "line one\nline two\n");

    Buffer buffer = Buffer::FromHugeFile(path);
    REQUIRE(buffer.Content().IsHuge());

    const std::unique_ptr<ned::text::ITextStorage> snapshot = buffer.Content().SnapshotForBackgroundRead();
    const std::string                              before   = snapshot->ToString();

    for (int i = 0; i < 200; ++i) {
        buffer.InsertAt(0, std::string(1024, 'z'));
    }

    REQUIRE(snapshot->ToString() == before);
    REQUIRE(buffer.Content().ToString() != before);
    std::filesystem::remove(path);
}
