#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Text/BufferList.h"

using ned::text::Buffer;
using ned::text::BufferList;
using ned::text::CompleteBufferNames;
using ned::text::CompleteFilePath;

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
