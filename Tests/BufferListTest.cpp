#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Text/BufferList.h"

using ned::text::AsyncLoadThreshold;
using ned::text::Buffer;
using ned::text::BufferList;
using ned::text::CompleteBufferNames;
using ned::text::CompleteFilePath;
using ned::text::SetAsyncLoadThreshold;

TEST_CASE("Fresh BufferList is empty", "[BufferList]") {
    BufferList list;
    REQUIRE(list.Count() == 0);
    REQUIRE(list.Find("scratch") == nullptr);
}

TEST_CASE("CreateBuffer adds a findable buffer", "[BufferList]") {
    BufferList list;

    Buffer& buffer = list.CreateBuffer("scratch");
    REQUIRE(buffer.Name() == "scratch");
    REQUIRE(list.Count() == 1);
    REQUIRE(list.Find("scratch") == &buffer);
}

TEST_CASE("Duplicate buffer names are uniquified Emacs-style", "[BufferList]") {
    BufferList list;

    Buffer& a = list.CreateBuffer("foo");
    Buffer& b = list.CreateBuffer("foo");
    Buffer& c = list.CreateBuffer("foo");

    REQUIRE(a.Name() == "foo");
    REQUIRE(b.Name() == "foo<2>");
    REQUIRE(c.Name() == "foo<3>");
}

TEST_CASE("Close removes a buffer by name", "[BufferList]") {
    BufferList list;
    list.CreateBuffer("scratch");

    REQUIRE(list.Close("scratch"));
    REQUIRE(list.Count() == 0);
    REQUIRE(list.Find("scratch") == nullptr);
    REQUIRE_FALSE(list.Close("scratch")); // already gone
}

TEST_CASE("OpenFile loads content and uniquifies same-basename files", "[BufferList]") {
    const std::filesystem::path dirA = std::filesystem::temp_directory_path() / "ned_bufferlist_test_a";
    const std::filesystem::path dirB = std::filesystem::temp_directory_path() / "ned_bufferlist_test_b";
    std::filesystem::create_directories(dirA);
    std::filesystem::create_directories(dirB);

    const std::filesystem::path pathA = dirA / "notes.txt";
    const std::filesystem::path pathB = dirB / "notes.txt";

    {
        std::ofstream(pathA) << "from A";
        std::ofstream(pathB) << "from B";
    }

    BufferList list;
    Buffer&    bufferA = list.OpenFile(pathA);
    Buffer&    bufferB = list.OpenFile(pathB);

    REQUIRE(bufferA.Name() == "notes.txt");
    REQUIRE(bufferB.Name() == "notes.txt<2>");
    REQUIRE(bufferA.Text() == "from A");
    REQUIRE(bufferB.Text() == "from B");

    std::filesystem::remove_all(dirA);
    std::filesystem::remove_all(dirB);
}

TEST_CASE("OpenOrCreateFile delegates to OpenFile when the path exists", "[BufferList]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferlist_test_existing.txt";
    std::ofstream(path) << "already here";

    BufferList list;
    Buffer&    buffer = list.OpenOrCreateFile(path);

    REQUIRE(buffer.Text() == "already here");
    REQUIRE(buffer.Path() == path);

    std::filesystem::remove(path);
}

TEST_CASE("OpenOrCreateFile creates an empty, path-associated buffer when the path doesn't exist", "[BufferList]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferlist_test_missing.txt";
    std::filesystem::remove(path);

    BufferList list;
    Buffer&    buffer = list.OpenOrCreateFile(path);

    REQUIRE(buffer.Text().empty());
    REQUIRE(buffer.Path() == path);
    REQUIRE_FALSE(std::filesystem::exists(path)); // not written to disk yet

    buffer.Save();
    REQUIRE(std::filesystem::exists(path));

    std::filesystem::remove(path);
}

TEST_CASE("OpenOrCreateFile uniquifies a new-file buffer's name against existing buffers", "[BufferList]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferlist_test_unique.txt";
    std::filesystem::remove(path);

    BufferList list;
    list.CreateBuffer("ned_bufferlist_test_unique.txt");

    Buffer& buffer = list.OpenOrCreateFile(path);
    REQUIRE(buffer.Name() == "ned_bufferlist_test_unique.txt<2>");
}

TEST_CASE("CompleteBufferNames returns sorted, prefix-matched open buffer names", "[BufferList]") {
    BufferList list;
    list.CreateBuffer("alpha");
    list.CreateBuffer("alphabet");
    list.CreateBuffer("beta");

    REQUIRE(CompleteBufferNames(list, "alph") == std::vector<std::string>{"alpha", "alphabet"});
    REQUIRE(CompleteBufferNames(list, "beta") == std::vector<std::string>{"beta"});
    REQUIRE(CompleteBufferNames(list, "") == std::vector<std::string>{"alpha", "alphabet", "beta"});
    REQUIRE(CompleteBufferNames(list, "nope").empty());
}

TEST_CASE("CompleteFilePath lists matching entries in the typed directory, sorted", "[BufferList]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferlist_test_complete";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    std::filesystem::create_directory(dir / "apple_dir");
    {
        std::ofstream(dir / "apple.txt") << "x";
    }
    {
        std::ofstream(dir / "banana.txt") << "x";
    }

    const std::string              prefix  = (dir / "app").string();
    const std::vector<std::string> matches = CompleteFilePath(prefix);

    REQUIRE(matches == std::vector<std::string>{(dir / "apple.txt").string(), (dir / "apple_dir").string() + "/"});

    std::filesystem::remove_all(dir);
}

TEST_CASE("CompleteFilePath with an empty filename component lists the whole directory", "[BufferList]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferlist_test_complete_all";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "one.txt") << "x";
    }
    {
        std::ofstream(dir / "two.txt") << "x";
    }

    const std::string              prefix  = dir.string() + "/";
    const std::vector<std::string> matches = CompleteFilePath(prefix);

    REQUIRE(matches == std::vector<std::string>{prefix + "one.txt", prefix + "two.txt"});

    std::filesystem::remove_all(dir);
}

TEST_CASE("CompleteFilePath returns no candidates for a directory that doesn't exist", "[BufferList]") {
    const std::string prefix = (std::filesystem::temp_directory_path() / "ned_no_such_dir_at_all" / "x").string();
    REQUIRE(CompleteFilePath(prefix).empty());
}

TEST_CASE("FindByPath finds an already-open buffer by its associated path", "[BufferList]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferlist_test_findbypath";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path file = dir / "target.txt";
    {
        std::ofstream(file) << "hello";
    }

    BufferList list;
    Buffer&    opened = list.OpenFile(file);

    REQUIRE(list.FindByPath(file) == &opened);

    std::filesystem::remove_all(dir);
}

TEST_CASE("FindByPath matches a relative and absolute path to the same file", "[BufferList]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferlist_test_findbypath_rel";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path file = dir / "target.txt";
    {
        std::ofstream(file) << "hello";
    }

    BufferList list;
    Buffer&    opened = list.OpenFile(std::filesystem::absolute(file));

    const std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::current_path(dir);
    REQUIRE(list.FindByPath("target.txt") == &opened);
    std::filesystem::current_path(previous);

    std::filesystem::remove_all(dir);
}

TEST_CASE("FindByPath returns nullptr for a buffer with no associated path", "[BufferList]") {
    BufferList list;
    list.CreateBuffer("scratch");

    REQUIRE(list.FindByPath("/nonexistent/anywhere.txt") == nullptr);
}

TEST_CASE("FindByPath returns nullptr when nothing matches", "[BufferList]") {
    BufferList list;
    REQUIRE(list.FindByPath("/nonexistent/anywhere.txt") == nullptr);
}

TEST_CASE("PreviewBuffer is unset by default", "[BufferList]") {
    BufferList list;
    REQUIRE(list.PreviewBuffer() == nullptr);
}

TEST_CASE("SetPreviewBuffer/PreviewBuffer round-trip", "[BufferList]") {
    BufferList list;
    Buffer&    buffer = list.CreateBuffer("scratch");

    list.SetPreviewBuffer(&buffer);
    REQUIRE(list.PreviewBuffer() == &buffer);

    list.SetPreviewBuffer(nullptr);
    REQUIRE(list.PreviewBuffer() == nullptr);
}

TEST_CASE("PreviewBuffer self-clears once the buffer becomes Modified()", "[BufferList]") {
    BufferList list;
    Buffer&    buffer = list.CreateBuffer("scratch");
    list.SetPreviewBuffer(&buffer);

    buffer.InsertAtPoint("x");

    REQUIRE(list.PreviewBuffer() == nullptr);
}

TEST_CASE("Close clears PreviewBuffer if it was the buffer being closed", "[BufferList]") {
    BufferList list;
    Buffer&    buffer = list.CreateBuffer("scratch");
    list.SetPreviewBuffer(&buffer);

    list.Close("scratch");

    REQUIRE(list.PreviewBuffer() == nullptr);
}

TEST_CASE("OpenFile refuses a binary file even with no async opener set", "[BufferList]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferlist_binary.bin";
    {
        std::ofstream file(path, std::ios::binary);
        file.put('a');
        file.put('\0');
        file.put('b');
    }

    BufferList list;
    REQUIRE_THROWS_AS(list.OpenFile(path), std::runtime_error);

    std::filesystem::remove(path);
}

TEST_CASE("OpenFile with no async opener loads a large file synchronously, unchanged from before", "[BufferList]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferlist_large_sync.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << std::string(17 * 1024 * 1024, 'x'); // above kAsyncLoadThreshold (16 MiB)
    }

    BufferList list;
    Buffer&    buffer = list.OpenFile(path);

    REQUIRE_FALSE(buffer.IsLoading());
    REQUIRE(buffer.Size() == 17 * 1024 * 1024);

    std::filesystem::remove(path);
}

TEST_CASE("OpenFile hands a large file to the async opener hook instead of loading it synchronously", "[BufferList]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferlist_large_async.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << std::string(17 * 1024 * 1024, 'x');
    }

    BufferList  list;
    Buffer*     hookedBuffer = nullptr;
    std::size_t hookCalls    = 0;
    list.SetAsyncFileOpener([&](Buffer& buffer, const std::filesystem::path&) {
        hookedBuffer = &buffer;
        ++hookCalls;
    });

    Buffer& buffer = list.OpenFile(path);

    REQUIRE(hookCalls == 1);
    REQUIRE(hookedBuffer == &buffer);
    REQUIRE(buffer.IsLoading()); // placeholder handed to the hook, not yet filled in
    REQUIRE(buffer.Size() == 0); // OpenFile itself never reads the file on the async path

    std::filesystem::remove(path);
}

TEST_CASE("OpenFile with allowBinary=true loads a binary file as text anyway", "[BufferList]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferlist_binary_allowed.bin";
    {
        std::ofstream file(path, std::ios::binary);
        file.put('a');
        file.put('\0');
        file.put('b');
    }

    BufferList list;
    Buffer&    buffer = list.OpenFile(path, /*allowBinary=*/true);
    REQUIRE(buffer.Size() == 3);

    std::filesystem::remove(path);
}

TEST_CASE("OpenOrCreateFile forwards allowBinary through to OpenFile", "[BufferList]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferlist_binary_allowed2.bin";
    {
        std::ofstream file(path, std::ios::binary);
        file.put('\0');
    }

    BufferList list;
    REQUIRE_THROWS_AS(list.OpenOrCreateFile(path), std::runtime_error);
    Buffer& buffer = list.OpenOrCreateFile(path, /*allowBinary=*/true);
    REQUIRE(buffer.Size() == 1);

    std::filesystem::remove(path);
}

TEST_CASE("OpenFile does not invoke the async opener for a small file even with a hook set", "[BufferList]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferlist_small_with_hook.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << "just a small file\n";
    }

    BufferList  list;
    std::size_t hookCalls = 0;
    list.SetAsyncFileOpener([&](Buffer&, const std::filesystem::path&) { ++hookCalls; });

    Buffer& buffer = list.OpenFile(path);

    REQUIRE(hookCalls == 0);
    REQUIRE_FALSE(buffer.IsLoading());
    REQUIRE(buffer.Text() == "just a small file\n");

    std::filesystem::remove(path);
}

TEST_CASE("SetAsyncLoadThreshold governs which files go to the async opener", "[BufferList]") {
    // Process-wide state (loose-ends follow-up: was a hardcoded 16 MiB
    // constant) -- restored via RAII, TabWidthGuard's exact precedent.
    struct ThresholdGuard {
        ~ThresholdGuard() {
            SetAsyncLoadThreshold(16 * 1024 * 1024);
        }
    } guard;

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferlist_threshold.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << "tiny but over a tiny threshold\n";
    }

    BufferList  list;
    std::size_t hookCalls = 0;
    list.SetAsyncFileOpener([&](Buffer&, const std::filesystem::path&) { ++hookCalls; });

    REQUIRE(AsyncLoadThreshold() == 16 * 1024 * 1024);
    SetAsyncLoadThreshold(4);
    Buffer& buffer = list.OpenFile(path);
    REQUIRE(hookCalls == 1); // 31 bytes > 4-byte threshold -> async path
    REQUIRE(buffer.IsLoading());

    SetAsyncLoadThreshold(1024);
    std::filesystem::remove(path);
    const std::filesystem::path smallPath = std::filesystem::temp_directory_path() / "ned_bufferlist_threshold2.txt";
    {
        std::ofstream file(smallPath, std::ios::binary);
        file << "under\n";
    }
    Buffer& small = list.OpenFile(smallPath);
    REQUIRE(hookCalls == 1); // unchanged -- back under the (raised) threshold
    REQUIRE_FALSE(small.IsLoading());

    std::filesystem::remove(smallPath);
}
