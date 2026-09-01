#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "Editor/ProjectRegistry.h"

using ned::editor::ProjectRegistryEntry;
using ned::editor::ProjectRegistryStore;

namespace {

std::filesystem::path FreshTestDir(const std::string& name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("Add creates a new entry and reports so", "[ProjectRegistry]") {
    const std::filesystem::path root = FreshTestDir("ned_registry_test_add");

    ProjectRegistryStore store;
    REQUIRE(store.Add("myproject", root, 1000));
    REQUIRE(store.Count() == 1);

    const auto entry = store.LookupByRoot(root);
    REQUIRE(entry.has_value());
    REQUIRE(entry->name == "myproject");
    REQUIRE(entry->lastUsed == 1000);
}

TEST_CASE("Re-adding an already-known root updates instead of duplicating", "[ProjectRegistry]") {
    const std::filesystem::path root = FreshTestDir("ned_registry_test_dedup");

    ProjectRegistryStore store;
    REQUIRE(store.Add("first-name", root, 1000));
    // Re-registering the same root, even under a different display name,
    // is an update, not a duplicate -- and reports so via the return value.
    REQUIRE_FALSE(store.Add("second-name", root, 2000));
    REQUIRE(store.Count() == 1);

    const auto entry = store.LookupByRoot(root);
    REQUIRE(entry.has_value());
    REQUIRE(entry->name == "second-name");
    REQUIRE(entry->lastUsed == 2000);
}

TEST_CASE("Remove drops the entry, Rename/Touch no-op on an unknown root", "[ProjectRegistry]") {
    const std::filesystem::path root    = FreshTestDir("ned_registry_test_remove");
    const std::filesystem::path unknown = root / "does-not-exist";

    ProjectRegistryStore store;
    store.Add("myproject", root, 1000);

    REQUIRE_FALSE(store.Rename(unknown, "new name"));
    store.Touch(unknown, 2000); // silently does nothing

    REQUIRE(store.Remove(root));
    REQUIRE_FALSE(store.Remove(root)); // already gone
    REQUIRE(store.Count() == 0);
}

TEST_CASE("Rename and Touch update the existing entry in place", "[ProjectRegistry]") {
    const std::filesystem::path root = FreshTestDir("ned_registry_test_rename_touch");

    ProjectRegistryStore store;
    store.Add("original", root, 1000);

    REQUIRE(store.Rename(root, "renamed"));
    store.Touch(root, 5000);

    const auto entry = store.LookupByRoot(root);
    REQUIRE(entry.has_value());
    REQUIRE(entry->name == "renamed");
    REQUIRE(entry->lastUsed == 5000);
}

TEST_CASE("List sorts most-recently-used first", "[ProjectRegistry]") {
    const std::filesystem::path dirA = FreshTestDir("ned_registry_test_sort_a");
    const std::filesystem::path dirB = FreshTestDir("ned_registry_test_sort_b");
    const std::filesystem::path dirC = FreshTestDir("ned_registry_test_sort_c");

    ProjectRegistryStore store;
    store.Add("a", dirA, 1000);
    store.Add("b", dirB, 3000);
    store.Add("c", dirC, 2000);

    const std::vector<ProjectRegistryEntry> listed = store.List();
    REQUIRE(listed.size() == 3);
    REQUIRE(listed[0].name == "b");
    REQUIRE(listed[1].name == "c");
    REQUIRE(listed[2].name == "a");
}

TEST_CASE("Project registry JSON and file round-trips", "[ProjectRegistry]") {
    const std::filesystem::path root = FreshTestDir("ned_registry_test_roundtrip");

    ProjectRegistryStore store;
    store.Add("myproject", root, 1234);

    const ProjectRegistryStore fromJson = ProjectRegistryStore::FromJson(store.ToJson());
    REQUIRE(fromJson.Count() == 1);
    const auto entry = fromJson.LookupByRoot(root);
    REQUIRE(entry.has_value());
    REQUIRE(entry->name == "myproject");
    REQUIRE(entry->lastUsed == 1234);

    const std::filesystem::path storePath = root / "state" / "projects.json";
    store.SaveToFile(storePath);
    ProjectRegistryStore loaded;
    loaded.LoadFromFile(storePath);
    REQUIRE(loaded.Count() == 1);
    REQUIRE(loaded.LookupByRoot(root).has_value());

    // One malformed entry doesn't discard the rest of the file.
    REQUIRE(ProjectRegistryStore::FromJson("garbage").Count() == 0);
    loaded.LoadFromFile(root / "missing.json");
    REQUIRE(loaded.Count() == 0);
}

TEST_CASE("A malformed entry is skipped, not fatal to the whole file", "[ProjectRegistry]") {
    const std::string json = R"({"version": 1, "projects": [
        {"root": "/a", "name": "good", "lastUsed": 10},
        {"root": "/b"},
        {"name": "no-root"},
        "not even an object"
    ]})";

    const ProjectRegistryStore store = ProjectRegistryStore::FromJson(json);
    REQUIRE(store.Count() == 1);
    REQUIRE(store.LookupByRoot("/a")->name == "good");
}
