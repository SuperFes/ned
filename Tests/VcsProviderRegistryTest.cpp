#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <memory>

#include "Editor/Vcs/ProviderRegistry.h"

using ned::editor::vcs::ActiveProviderFor;
using ned::editor::vcs::ClearProviderCache;
using ned::editor::vcs::ClearRegistry;
using ned::editor::vcs::RegisterProvider;
using ned::editor::vcs::BlameLine;
using ned::editor::vcs::CommandSpec;
using ned::editor::vcs::DiffHunk;
using ned::editor::vcs::LogEntry;
using ned::editor::vcs::Provider;

namespace {

// A fake provider whose Detect() answer and identity are both fixed at
// construction, so tests can assert exactly which instance won. Blame/log
// aren't exercised by these registry-only tests.
class FakeProvider : public Provider {
  public:
    explicit FakeProvider(bool matches) : matches_(matches) {
    }

    [[nodiscard]] bool Detect(const std::filesystem::path&) const override {
        return matches_;
    }

    [[nodiscard]] CommandSpec BlameArgv(const std::filesystem::path&) const override {
        return {};
    }
    [[nodiscard]] std::vector<BlameLine> ParseBlame(const std::string&) const override {
        return {};
    }
    [[nodiscard]] CommandSpec LogArgv(const std::filesystem::path&) const override {
        return {};
    }
    [[nodiscard]] std::vector<LogEntry> ParseLog(const std::string&) const override {
        return {};
    }
    [[nodiscard]] CommandSpec DiffArgv(const std::filesystem::path&) const override {
        return {};
    }
    [[nodiscard]] std::vector<DiffHunk> ParseDiff(const std::string&) const override {
        return {};
    }

  private:
    bool matches_;
};

// Ensures each TEST_CASE starts from a clean registry regardless of
// ordering/prior failures, mirroring how other mutex-guarded-static-state
// tests in this codebase reset before asserting.
struct RegistryResetGuard {
    RegistryResetGuard() {
        ClearRegistry();
    }
    ~RegistryResetGuard() {
        ClearRegistry();
    }
};

// BackgroundActivityTest.cpp's own BackgroundActivityGlobalFixture precedent,
// same registry-shape bug: a REQUIRE that throws between RegisterProvider and
// a file's own trailing (plain, not RAII) ClearRegistry() call -- as several
// of the files that touch this process-wide registry write it
// (BufferViewDiffGutterTest.cpp, BufferViewVcsCommitTest.cpp,
// BufferViewVcsStatusTest.cpp, WindowManagerTest.cpp) -- skips that cleanup
// and leaves the registered provider active for every later test case in the
// binary. Found live under `--order rand`: McpToolRegistryTest.cpp's two
// "no VCS provider registered" tests (which have no reset of their own,
// trusting every other file's cleanup) failed with the callback never firing
// -- consistent with a leaked provider routing the call down the real
// (async, unpumped-in-this-fixture) provider path instead of the synchronous
// no-provider error path. A single global reset after every test case closes
// the whole class regardless of which test causes it, the same fix already
// applied to BackgroundActivity's own registry.
class VcsProviderRegistryGlobalFixture final : public Catch::EventListenerBase {
  public:
    using Catch::EventListenerBase::EventListenerBase;

    void testCaseEnded(const Catch::TestCaseStats&) override {
        ClearRegistry();
        ClearProviderCache();
    }
};

CATCH_REGISTER_LISTENER(VcsProviderRegistryGlobalFixture)

} // namespace

TEST_CASE("ActiveProviderFor returns nullptr when no provider is registered", "[ProviderRegistry]") {
    RegistryResetGuard guard;
    REQUIRE(ActiveProviderFor("/some/root") == nullptr);
}

TEST_CASE("ActiveProviderFor returns nullptr when no registered provider matches", "[ProviderRegistry]") {
    RegistryResetGuard guard;
    RegisterProvider("never-matches", std::make_unique<FakeProvider>(false));
    REQUIRE(ActiveProviderFor("/some/root") == nullptr);
}

TEST_CASE("ActiveProviderFor returns the first provider whose Detect matches", "[ProviderRegistry]") {
    RegistryResetGuard guard;
    RegisterProvider("no-match", std::make_unique<FakeProvider>(false));
    auto* const matching = new FakeProvider(true);
    RegisterProvider("matches", std::unique_ptr<Provider>(matching));

    REQUIRE(ActiveProviderFor("/some/root") == matching);
}

TEST_CASE("ActiveProviderFor prefers the first-registered match on ties", "[ProviderRegistry]") {
    RegistryResetGuard guard;
    auto* const        first  = new FakeProvider(true);
    auto* const        second = new FakeProvider(true);
    RegisterProvider("first", std::unique_ptr<Provider>(first));
    RegisterProvider("second", std::unique_ptr<Provider>(second));

    REQUIRE(ActiveProviderFor("/some/root") == first);
}

TEST_CASE("ActiveProviderFor caches its result per root", "[ProviderRegistry]") {
    RegistryResetGuard guard;
    auto* const        matching = new FakeProvider(true);
    RegisterProvider("matches", std::unique_ptr<Provider>(matching));

    REQUIRE(ActiveProviderFor("/cached/root") == matching);

    // Registering a *new* provider that would now match first shouldn't
    // change the cached answer for an already-resolved root -- the cache
    // is only invalidated explicitly.
    auto* const laterMatch = new FakeProvider(true);
    RegisterProvider("later", std::unique_ptr<Provider>(laterMatch));
    REQUIRE(ActiveProviderFor("/cached/root") == matching);

    ClearProviderCache();
    REQUIRE(ActiveProviderFor("/cached/root") == matching); // still the first-registered match once re-resolved
}

TEST_CASE("RegisterProvider overwrites an existing name in place, preserving registration order",
          "[ProviderRegistry]") {
    RegistryResetGuard guard;
    RegisterProvider("name", std::make_unique<FakeProvider>(false));
    auto* const secondMatch = new FakeProvider(true);
    RegisterProvider("other-first", std::unique_ptr<Provider>(secondMatch));

    // Re-register "name" so it now matches -- since it kept its original
    // (first) registration slot, it should win over "other-first".
    auto* const replaced = new FakeProvider(true);
    RegisterProvider("name", std::unique_ptr<Provider>(replaced));

    REQUIRE(ActiveProviderFor("/some/root") == replaced);
}
