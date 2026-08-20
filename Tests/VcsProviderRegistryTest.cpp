#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "Editor/Vcs/VcsProviderRegistry.h"

using ned::editor::vcs::ActiveProviderFor;
using ned::editor::vcs::ClearProviderCache;
using ned::editor::vcs::ClearRegistry;
using ned::editor::vcs::RegisterProvider;
using ned::editor::vcs::VcsBlameLine;
using ned::editor::vcs::VcsCommandSpec;
using ned::editor::vcs::VcsDiffHunk;
using ned::editor::vcs::VcsLogEntry;
using ned::editor::vcs::VcsProvider;

namespace {

// A fake provider whose Detect() answer and identity are both fixed at
// construction, so tests can assert exactly which instance won. Blame/log
// aren't exercised by these registry-only tests.
class FakeProvider : public VcsProvider {
  public:
    explicit FakeProvider(bool matches) : matches_(matches) {}

    [[nodiscard]] bool Detect(const std::filesystem::path&) const override { return matches_; }

    [[nodiscard]] VcsCommandSpec BlameArgv(const std::filesystem::path&) const override { return {}; }
    [[nodiscard]] std::vector<VcsBlameLine> ParseBlame(const std::string&) const override { return {}; }
    [[nodiscard]] VcsCommandSpec LogArgv(const std::filesystem::path&) const override { return {}; }
    [[nodiscard]] std::vector<VcsLogEntry> ParseLog(const std::string&) const override { return {}; }
    [[nodiscard]] VcsCommandSpec DiffArgv(const std::filesystem::path&) const override { return {}; }
    [[nodiscard]] std::vector<VcsDiffHunk> ParseDiff(const std::string&) const override { return {}; }

  private:
    bool matches_;
};

// Ensures each TEST_CASE starts from a clean registry regardless of
// ordering/prior failures, mirroring how other mutex-guarded-static-state
// tests in this codebase reset before asserting.
struct RegistryResetGuard {
    RegistryResetGuard() { ClearRegistry(); }
    ~RegistryResetGuard() { ClearRegistry(); }
};

} // namespace

TEST_CASE("ActiveProviderFor returns nullptr when no provider is registered", "[VcsProviderRegistry]") {
    RegistryResetGuard guard;
    REQUIRE(ActiveProviderFor("/some/root") == nullptr);
}

TEST_CASE("ActiveProviderFor returns nullptr when no registered provider matches", "[VcsProviderRegistry]") {
    RegistryResetGuard guard;
    RegisterProvider("never-matches", std::make_unique<FakeProvider>(false));
    REQUIRE(ActiveProviderFor("/some/root") == nullptr);
}

TEST_CASE("ActiveProviderFor returns the first provider whose Detect matches", "[VcsProviderRegistry]") {
    RegistryResetGuard guard;
    RegisterProvider("no-match", std::make_unique<FakeProvider>(false));
    auto* const matching = new FakeProvider(true);
    RegisterProvider("matches", std::unique_ptr<VcsProvider>(matching));

    REQUIRE(ActiveProviderFor("/some/root") == matching);
}

TEST_CASE("ActiveProviderFor prefers the first-registered match on ties", "[VcsProviderRegistry]") {
    RegistryResetGuard guard;
    auto* const first  = new FakeProvider(true);
    auto* const second = new FakeProvider(true);
    RegisterProvider("first", std::unique_ptr<VcsProvider>(first));
    RegisterProvider("second", std::unique_ptr<VcsProvider>(second));

    REQUIRE(ActiveProviderFor("/some/root") == first);
}

TEST_CASE("ActiveProviderFor caches its result per root", "[VcsProviderRegistry]") {
    RegistryResetGuard guard;
    auto* const matching = new FakeProvider(true);
    RegisterProvider("matches", std::unique_ptr<VcsProvider>(matching));

    REQUIRE(ActiveProviderFor("/cached/root") == matching);

    // Registering a *new* provider that would now match first shouldn't
    // change the cached answer for an already-resolved root -- the cache
    // is only invalidated explicitly.
    auto* const laterMatch = new FakeProvider(true);
    RegisterProvider("later", std::unique_ptr<VcsProvider>(laterMatch));
    REQUIRE(ActiveProviderFor("/cached/root") == matching);

    ClearProviderCache();
    REQUIRE(ActiveProviderFor("/cached/root") == matching); // still the first-registered match once re-resolved
}

TEST_CASE("RegisterProvider overwrites an existing name in place, preserving registration order",
          "[VcsProviderRegistry]") {
    RegistryResetGuard guard;
    RegisterProvider("name", std::make_unique<FakeProvider>(false));
    auto* const secondMatch = new FakeProvider(true);
    RegisterProvider("other-first", std::unique_ptr<VcsProvider>(secondMatch));

    // Re-register "name" so it now matches -- since it kept its original
    // (first) registration slot, it should win over "other-first".
    auto* const replaced = new FakeProvider(true);
    RegisterProvider("name", std::unique_ptr<VcsProvider>(replaced));

    REQUIRE(ActiveProviderFor("/some/root") == replaced);
}
