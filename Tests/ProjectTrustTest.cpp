#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/ProjectTrust.h"

using ned::editor::HashFileContent;
using ned::editor::ProjectTrustEntry;
using ned::editor::ProjectTrustStore;

namespace {

std::filesystem::path FreshTestDir(const std::string& name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path WriteTestFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
    return path;
}

constexpr std::int64_t kDay = 24 * 60 * 60;

} // namespace

TEST_CASE("Trust requires a matching content hash", "[ProjectTrust]") {
    const std::filesystem::path dir  = FreshTestDir("ned_trust_test_hash");
    const std::filesystem::path init = WriteTestFile(dir / "init.janet", "(print :hi)");

    ProjectTrustStore store;
    REQUIRE_FALSE(store.IsTrusted(init, "abc"));

    store.Trust(init, "abc", 1000);
    REQUIRE(store.IsTrusted(init, "abc"));
    // The file changed (different hash): not trusted, but the stale entry
    // remains for an "always" re-approval to overwrite.
    REQUIRE_FALSE(store.IsTrusted(init, "def"));
    REQUIRE(store.Count() == 1);

    store.Trust(init, "def", 2000);
    REQUIRE(store.IsTrusted(init, "def"));
    REQUIRE(store.Count() == 1);
}

TEST_CASE("Expiry prunes by disuse, not by trust age", "[ProjectTrust]") {
    const std::filesystem::path dir   = FreshTestDir("ned_trust_test_expiry");
    const std::filesystem::path stale = WriteTestFile(dir / "stale.janet", "x");
    const std::filesystem::path fresh = WriteTestFile(dir / "fresh.janet", "y");

    ProjectTrustStore store;
    // Both trusted long ago -- but only one has been *used* recently.
    store.Trust(stale, "h1", 0);
    store.Trust(fresh, "h2", 0);
    store.Touch(fresh, 100 * kDay);

    store.PruneExpired(/*now=*/100 * kDay, /*expiryDays=*/30);
    REQUIRE_FALSE(store.Lookup(stale).has_value());
    REQUIRE(store.Lookup(fresh).has_value());

    // 0 (or negative) = never expire.
    ProjectTrustStore never;
    never.Trust(stale, "h1", 0);
    never.PruneExpired(100 * kDay, 0);
    REQUIRE(never.Count() == 1);
}

TEST_CASE("Missing init files are pruned", "[ProjectTrust]") {
    const std::filesystem::path dir     = FreshTestDir("ned_trust_test_missing");
    const std::filesystem::path present = WriteTestFile(dir / "present.janet", "x");
    const std::filesystem::path gone    = dir / "gone.janet";

    ProjectTrustStore store;
    store.Trust(present, "h1", 0);
    store.Trust(gone, "h2", 0);

    store.PruneMissingFiles();
    REQUIRE(store.Lookup(present).has_value());
    REQUIRE_FALSE(store.Lookup(gone).has_value());
}

TEST_CASE("Trust store JSON and file round-trips", "[ProjectTrust]") {
    const std::filesystem::path dir  = FreshTestDir("ned_trust_test_roundtrip");
    const std::filesystem::path init = WriteTestFile(dir / "init.janet", "x");

    ProjectTrustStore store;
    store.Trust(init, "somehash", 1234);

    const ProjectTrustStore fromJson = ProjectTrustStore::FromJson(store.ToJson());
    REQUIRE(fromJson.Count() == 1);
    const auto entry = fromJson.Lookup(init);
    REQUIRE(entry.has_value());
    REQUIRE(entry->contentHash == "somehash");
    REQUIRE(entry->trustedAt == 1234);
    REQUIRE(entry->lastUsed == 1234);

    const std::filesystem::path storePath = dir / "state" / "trusted.json";
    store.SaveToFile(storePath);
    ProjectTrustStore loaded;
    loaded.LoadFromFile(storePath);
    REQUIRE(loaded.Count() == 1);
    REQUIRE(loaded.IsTrusted(init, "somehash"));

    REQUIRE(ProjectTrustStore::FromJson("garbage").Count() == 0);
    loaded.LoadFromFile(dir / "missing.json");
    REQUIRE(loaded.Count() == 0);
}

TEST_CASE("HashFileContent tracks content, not path", "[ProjectTrust]") {
    const std::filesystem::path dir = FreshTestDir("ned_trust_test_hashfile");
    const auto                  a   = WriteTestFile(dir / "a.janet", "(print 1)");
    const auto                  b   = WriteTestFile(dir / "b.janet", "(print 1)");
    const auto                  c   = WriteTestFile(dir / "c.janet", "(print 2)");

    const auto hashA = HashFileContent(a);
    REQUIRE(hashA.has_value());
    REQUIRE(HashFileContent(b) == hashA); // same content, same hash, path-independent
    REQUIRE(HashFileContent(c) != hashA);
    REQUIRE_FALSE(HashFileContent(dir / "missing.janet").has_value());

    WriteTestFile(a, "(print 3)");
    REQUIRE(HashFileContent(a) != hashA); // edits change the hash -> re-prompt
}
