#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

#include "Editor/Tracker/Registry.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "JanetTestSupport.h"

using Catch::Matchers::ContainsSubstring;
using ned::editor::tracker::Connection;
using ned::editor::tracker::Issue;
using ned::editor::tracker::Panel;
using ned::janet::Environment;
using ned::janet::InstallEditorBindings;

namespace tracker = ned::editor::tracker;

namespace {

struct RegistryResetGuard {
    RegistryResetGuard() {
        tracker::ClearRegistry();
    }
    ~RegistryResetGuard() {
        tracker::ClearRegistry();
    }
};

Environment& BoundEnvironment() {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);
    return env;
}

std::string EvalString(Environment& env, const std::string& code) {
    const Janet result = env.DoString(code);
    REQUIRE(janet_checktype(result, JANET_STRING));
    const JanetString text = janet_unwrap_string(result);
    return std::string(reinterpret_cast<const char*>(text), janet_string_length(text));
}

} // namespace

TEST_CASE("ned/json-decode maps JSON onto Janet values", "[JanetTrackerProvider]") {
    Environment& env = BoundEnvironment();

    env.DoString(R"((def decoded (ned/json-decode `{"number": 42, "title": "Crash", "open": true, "ratio": 0.5,
                                                   "assignee": null, "labels": [{"name": "bug"}, {"name": "ui"}]}`)))");
    CHECK(EvalString(env, "(string (decoded :number))") == "42");
    CHECK(EvalString(env, "(decoded :title)") == "Crash");
    CHECK(EvalString(env, "(string (decoded :open))") == "true");
    CHECK(EvalString(env, "(string (decoded :ratio))") == "0.5");
    CHECK(EvalString(env, "(string (has-key? decoded :assignee))") == "false");
    CHECK(EvalString(env, "(string/join (map |($ :name) (decoded :labels)) \",\")") == "bug,ui");
    CHECK(EvalString(env, "(string (type (ned/json-decode `[1, null]`)))") == "array");
    CHECK(EvalString(env, "(string (length (ned/json-decode `[1, null]`)))") == "2");

    CHECK_THROWS_WITH(env.DoString("(ned/json-decode `{\"unterminated\": `)"), ContainsSubstring("invalid JSON"));
}

TEST_CASE("ned/tracker-register-provider adapts :list-argv and :parse-list", "[JanetTrackerProvider]") {
    RegistryResetGuard guard;
    Environment&       env = BoundEnvironment();

    env.DoString(R"(
      (ned/tracker-register-provider "stub-gh"
        {:list-argv (fn [connection query]
                      ["gh" "issue" "list" "--search" query "--json" "number,title"
                       (connection :name) (connection :url) (connection :email) (string (connection :token))])
         :parse-list (fn [output]
                       (map (fn [issue]
                              {:key (issue :number)
                               :title (issue :title)
                               :status (issue :state)
                               :labels (map |($ :name) (issue :labels))
                               :url (issue :url)})
                            (ned/json-decode output)))})
    )");

    const auto provider = tracker::FindProvider("stub-gh");
    REQUIRE(provider);

    const Connection connection{.name         = "gh",
                                .provider     = "stub-gh",
                                .url          = "https://github.com",
                                .email        = "me@example.com",
                                .tokenCommand = {"pass", "show", "secret"}};
    CHECK(provider->ListArgv(connection, "is:open assignee:@me").argv ==
          std::vector<std::string>{"gh", "issue", "list", "--search", "is:open assignee:@me", "--json", "number,title", "gh",
                                   "https://github.com", "me@example.com", ""});

    const std::vector<Issue> issues = provider->ParseList(R"([
        {"number": 7, "title": "Rail tooltip clips", "state": "OPEN", "url": "https://github.com/o/r/issues/7",
         "labels": [{"name": "ui"}, {"name": "bug"}]},
        {"number": 8, "title": "No labels", "state": "CLOSED", "labels": []}
    ])");
    REQUIRE(issues.size() == 2);
    CHECK(issues[0] == Issue{.key    = "7",
                             .title  = "Rail tooltip clips",
                             .status = "OPEN",
                             .labels = {"ui", "bug"},
                             .url    = "https://github.com/o/r/issues/7"});
    CHECK(issues[1].key == "8");
    CHECK(issues[1].labels.empty());
    CHECK(issues[1].url.empty());

    CHECK_THROWS_WITH(provider->ParseList("not json"), ContainsSubstring("invalid JSON"));
}

TEST_CASE("ned/tracker-register-provider rejects incomplete providers and unsafe names", "[JanetTrackerProvider]") {
    RegistryResetGuard guard;
    Environment&       env = BoundEnvironment();

    CHECK_THROWS_WITH(env.DoString(R"((ned/tracker-register-provider "half" {:list-argv (fn [c q] [])}))"),
                      ContainsSubstring(":parse-list"));
    CHECK_THROWS_WITH(env.DoString(R"((ned/tracker-register-provider "a b" {:list-argv (fn [c q] []) :parse-list (fn [o] [])}))"),
                      ContainsSubstring("must be letters"));
    CHECK_THROWS(env.DoString(R"((ned/tracker-register-provider "x" "not a table"))"));
    CHECK(tracker::FindProvider("half") == nullptr);
}

TEST_CASE("A :parse-list result that isn't a list of tables degrades", "[JanetTrackerProvider]") {
    RegistryResetGuard guard;
    Environment&       env = BoundEnvironment();

    env.DoString(R"(
      (ned/tracker-register-provider "odd"
        {:list-argv (fn [c q] [])
         :parse-list (fn [output] (if (= output "scalar") 5 [{:key "A-1" :title 3 :labels ["x" 4]} "skipped"]))})
    )");
    const auto provider = tracker::FindProvider("odd");
    REQUIRE(provider);

    const std::vector<Issue> issues = provider->ParseList("list");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].key == "A-1");
    CHECK(issues[0].title == "3");
    CHECK(issues[0].labels == std::vector<std::string>{"x"});

    CHECK_THROWS_WITH(provider->ParseList("scalar"), ContainsSubstring("array of issue tables"));
}

TEST_CASE("ned/set-tracker-connection and ned/add-tracker-panel fill the registry", "[JanetTrackerProvider]") {
    RegistryResetGuard guard;
    Environment&       env = BoundEnvironment();

    env.DoString(R"janet(
      (ned/add-tracker-panel "Sprint" {:connection "work" :query "sprint in openSprints()" :glyph "S"})
      (ned/set-tracker-connection "work" {:provider "jira" :url "https://acme.atlassian.net" :email "me@acme.test"
                                          :token-command ["secret-tool" "lookup" "service" "jira"]})
      (ned/set-tracker-connection "gh" {:provider "github"})
      (ned/add-tracker-panel "Issues" {:connection "gh"})
    )janet");

    CHECK(tracker::FindConnection("work") == Connection{.name         = "work",
                                                        .provider     = "jira",
                                                        .url          = "https://acme.atlassian.net",
                                                        .email        = "me@acme.test",
                                                        .tokenCommand = {"secret-tool", "lookup", "service", "jira"}});
    CHECK(tracker::FindConnection("gh") == Connection{.name = "gh", .provider = "github"});

    const std::vector<Panel> panels = tracker::Panels();
    REQUIRE(panels.size() == 2);
    CHECK(panels[0] == Panel{.name = "Sprint", .connection = "work", .query = "sprint in openSprints()", .glyph = "S"});
    CHECK(panels[1] == Panel{.name = "Issues", .connection = "gh"});

    CHECK_THROWS_WITH(env.DoString(R"((ned/set-tracker-connection "bad" {:url "https://x"}))"), ContainsSubstring("needs a :provider"));
    CHECK_THROWS_WITH(env.DoString(R"((ned/set-tracker-connection "bad" {:provider "jira" :email 5}))"),
                      ContainsSubstring(":email must be a string"));
    CHECK_THROWS(env.DoString(R"((ned/set-tracker-connection "bad" {:provider "jira" :token-command "pass show jira"}))"));
    CHECK_THROWS_WITH(env.DoString(R"((ned/add-tracker-panel "bad" {:query "x"}))"), ContainsSubstring("needs a :connection"));
    CHECK_THROWS_WITH(env.DoString(R"((ned/add-tracker-panel "bad" "gh"))"), ContainsSubstring("struct/table of options"));
    CHECK_FALSE(tracker::FindConnection("bad"));
    CHECK_FALSE(tracker::FindPanel("bad"));
}
