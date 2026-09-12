// A single Environment (and therefore a single Janet VM) for the whole
// ned_tests binary.
//
// Repeated janet_init()/janet_deinit() cycles (one per TEST_CASE, or one per
// Environment instance if each test constructed its own) corrupt global
// state in this Janet build in a way that only surfaces later as a segfault
// inside janet_pcall -- reproduced independently of Catch2, so it's a
// real constraint of the C API/this Janet version, not a project bug. Real
// application usage only ever constructs one Environment for the process
// lifetime anyway, so every Janet-related test file should share this one
// rather than constructing its own.

#include "JanetTestSupport.h"

#include <memory>

#include "Editor/CaptureClassifiers.h"
#include "Janet/EditorBindings.h"
#include "Janet/Plugins.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace ned_tests {

namespace {
    std::unique_ptr<ned::janet::Environment> g_environment;
}

ned::janet::Environment& TestEnvironment() {
    return *g_environment;
}

void RestoreBundledCaptureClassifiers() {
    ned::editor::ClearCaptureClassifiers();
    g_environment->DoString(ned::janet::plugins::kLanguages, "languages.janet");
}

namespace {

    class JanetGlobalFixture final : public Catch::EventListenerBase {
      public:
        using Catch::EventListenerBase::EventListenerBase;

        void testRunStarting(const Catch::TestRunInfo&) override {
            g_environment = std::make_unique<ned::janet::Environment>();
            // The bindings plus the bundled language classifiers
            // (Plugins/languages.janet), unconditionally: Org's headline
            // level and TODO-vs-DONE live there now, and any test touching
            // an org-mode highlight -- most of them nowhere near Janet --
            // needs them registered exactly the way a running ned has them.
            // Deliberately NOT LoadBundledPlugins: vcs-git registers a live
            // VCS provider, which the provider-registry tests assume absent.
            ned::janet::InstallEditorBindings(*g_environment);
            g_environment->DoString(ned::janet::plugins::kLanguages, "languages.janet");
        }

        void testRunEnded(const Catch::TestRunStats&) override {
            g_environment.reset();
        }
    };

} // namespace

CATCH_REGISTER_LISTENER(JanetGlobalFixture)

} // namespace ned_tests
