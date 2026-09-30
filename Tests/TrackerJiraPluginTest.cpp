#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Tracker/Registry.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/PluginLoader.h"
#include "JanetTestSupport.h"

using Catch::Matchers::ContainsSubstring;
using ned::editor::tracker::Comment;
using ned::editor::tracker::Connection;
using ned::editor::tracker::Issue;
using ned::editor::tracker::IssueDetail;
using ned::editor::tracker::Provider;

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

std::shared_ptr<const Provider> LoadJiraProvider() {
    ned::janet::Environment& env = ned_tests::TestEnvironment();
    ned::janet::InstallEditorBindings(env);
    env.DoString(ned::janet::ReadBundledPlugin("tracker-jira"), "tracker-jira.janet");
    auto provider = tracker::FindProvider("jira");
    REQUIRE(provider);
    return provider;
}

const Connection kSite{.name         = "work",
                       .provider     = "jira",
                       .url          = "https://example.atlassian.net/",
                       .email        = "me@example.com",
                       .tokenCommand = {"pass", "jira"}};

const std::vector<std::string> kCurl{"curl", "-sS", "--fail-with-body", "--proto", "=https", "--max-time", "60", "-H",
                                     "Accept: application/json"};

std::vector<std::string> Curl(std::vector<std::string> args, const std::string& url) {
    std::vector<std::string> argv = kCurl;
    argv.insert(argv.end(), args.begin(), args.end());
    argv.push_back(url);
    return argv;
}

// The body of a view response whose description is `adf`.
std::string ViewWithDescription(const std::string& adf) {
    return R"({"key": "NED-1", "fields": {"description": )" + adf + "}}";
}

std::string Render(const Provider& jira, const std::string& content) {
    return jira.ParseView(ViewWithDescription(R"({"type": "doc", "version": 1, "content": [)" + content + "]}")).body;
}

} // namespace

TEST_CASE("tracker-jira searches with JQL and asks for curl credentials", "[TrackerJira]") {
    RegistryResetGuard guard;
    const auto         jira = LoadJiraProvider();
    CHECK_FALSE(jira->NumericKeys());

    const auto list = jira->ListArgv(kSite, "project = NED ORDER BY rank");
    CHECK(list.curlCredentials);
    CHECK(list.argv == Curl({"-G", "--data-urlencode", "jql=project = NED ORDER BY rank", "--data-urlencode",
                             "fields=summary,status,assignee,labels,updated", "--data-urlencode", "maxResults=100"},
                            "https://example.atlassian.net/rest/api/3/search/jql"));

    // An empty query lists your unresolved issues.
    CHECK(jira->ListArgv(kSite, "  ").argv[11] ==
          "jql=assignee = currentUser() AND statusCategory != Done ORDER BY updated DESC");

    CHECK_THROWS_WITH(jira->ListArgv(Connection{.name = "bare"}, ""), ContainsSubstring("\"bare\" needs an https:// :url"));
    CHECK_THROWS_WITH(jira->ListArgv(Connection{.name = "plain", .url = "http://example.atlassian.net"}, ""),
                      ContainsSubstring("needs an https:// :url"));
}

TEST_CASE("tracker-jira parses a search into issues with browse URLs", "[TrackerJira]") {
    RegistryResetGuard guard;
    const auto         jira = LoadJiraProvider();

    const std::vector<Issue> issues = jira->ParseList(R"({"issues": [
        {"id": "10001", "key": "NED-7", "self": "https://example.atlassian.net/rest/api/3/issue/10001",
         "fields": {"summary": "Rail tooltip clips", "status": {"name": "In Progress"},
                    "assignee": {"displayName": "Ann Lee"}, "labels": ["ui", "bug"],
                    "updated": "2026-09-28T12:00:00.000+0000"}},
        {"id": "10002", "key": "NED-8", "self": "https://example.atlassian.net/rest/api/3/issue/10002",
         "fields": {"summary": "Unassigned", "status": {"name": "To Do"}, "assignee": null, "labels": []}}
    ], "isLast": true})");
    REQUIRE(issues.size() == 2);
    CHECK(issues[0] == Issue{.key      = "NED-7",
                             .title    = "Rail tooltip clips",
                             .status   = "In Progress",
                             .assignee = "Ann Lee",
                             .labels   = {"ui", "bug"},
                             .url      = "https://example.atlassian.net/browse/NED-7",
                             .updated  = "2026-09-28T12:00:00.000+0000"});
    CHECK(issues[1].assignee.empty());
    CHECK(issues[1].status == "To Do");

    CHECK(jira->ParseList(R"({"issues": []})").empty());
    CHECK(jira->ParseList("{}").empty());
}

TEST_CASE("tracker-jira views one issue with its comments", "[TrackerJira]") {
    RegistryResetGuard guard;
    const auto         jira = LoadJiraProvider();

    const auto view = jira->ViewArgv(kSite, " ned-12 ");
    REQUIRE(view);
    CHECK(view->curlCredentials);
    CHECK(view->argv == Curl({"-G", "--data-urlencode", "fields=summary,status,assignee,labels,updated,description,comment"},
                             "https://example.atlassian.net/rest/api/3/issue/NED-12"));
    CHECK_THROWS_WITH(jira->ViewArgv(kSite, "../../myself"), ContainsSubstring("isn't a Jira issue key"));
    CHECK_THROWS_WITH(jira->ViewArgv(kSite, "#12"), ContainsSubstring("isn't a Jira issue key"));

    const IssueDetail detail = jira->ParseView(R"({"key": "NED-12",
        "self": "https://example.atlassian.net/rest/api/3/issue/10012",
        "fields": {"summary": "Crash", "status": {"name": "Open"}, "labels": [],
          "description": {"type": "doc", "version": 1, "content": [
            {"type": "paragraph", "content": [{"type": "text", "text": "Steps"}]}]},
          "comment": {"comments": [
            {"author": {"displayName": "Ann Lee"}, "created": "2026-09-28T12:00:00.000+0000",
             "body": {"type": "doc", "version": 1, "content": [
               {"type": "paragraph", "content": [{"type": "text", "text": "Same here"}]}]}},
            {"created": "2026-09-29T08:00:00.000+0000", "body": null}],
            "total": 2}}})");
    CHECK(detail.issue.key == "NED-12");
    CHECK(detail.issue.title == "Crash");
    CHECK(detail.issue.url == "https://example.atlassian.net/browse/NED-12");
    CHECK(detail.body == "Steps");
    CHECK(detail.comments ==
          std::vector<Comment>{{.author = "Ann Lee", .created = "2026-09-28T12:00:00.000+0000", .body = "Same here"},
                               {.author = "", .created = "2026-09-29T08:00:00.000+0000", .body = ""}});

    CHECK(jira->ParseView(ViewWithDescription("null")).body.empty());
}

TEST_CASE("tracker-jira renders ADF inline marks and nodes as Markdown", "[TrackerJira]") {
    RegistryResetGuard guard;
    const auto         jira = LoadJiraProvider();

    CHECK(Render(*jira, R"json({"type": "paragraph", "content": [
        {"type": "text", "text": "bold", "marks": [{"type": "strong"}]},
        {"type": "text", "text": " "},
        {"type": "text", "text": "it", "marks": [{"type": "em"}]},
        {"type": "text", "text": " "},
        {"type": "text", "text": "x()", "marks": [{"type": "code"}]},
        {"type": "text", "text": " "},
        {"type": "text", "text": "old", "marks": [{"type": "strike"}]},
        {"type": "text", "text": " "},
        {"type": "text", "text": "docs", "marks": [{"type": "link", "attrs": {"href": "https://x.dev"}}, {"type": "strong"}]},
        {"type": "hardBreak"},
        {"type": "mention", "attrs": {"id": "abc", "text": "@Ann Lee"}},
        {"type": "text", "text": " "},
        {"type": "emoji", "attrs": {"shortName": ":tada:", "text": "🎉"}},
        {"type": "text", "text": " "},
        {"type": "status", "attrs": {"text": "BLOCKED", "color": "red"}},
        {"type": "text", "text": " "},
        {"type": "date", "attrs": {"timestamp": "1790640000000"}},
        {"type": "text", "text": " "},
        {"type": "inlineCard", "attrs": {"url": "https://example.atlassian.net/browse/NED-2"}}
    ]})json") == "**bold** *it* `x()` ~~old~~ [**docs**](https://x.dev)\n"
                 "@Ann Lee 🎉 [BLOCKED] 2026-09-29 https://example.atlassian.net/browse/NED-2");
}

TEST_CASE("tracker-jira renders ADF blocks as Markdown", "[TrackerJira]") {
    RegistryResetGuard guard;
    const auto         jira = LoadJiraProvider();

    SECTION("headings, paragraphs, rules, code and quotes are separated by blank lines") {
        CHECK(Render(*jira, R"(
            {"type": "heading", "attrs": {"level": 2}, "content": [{"type": "text", "text": "Repro"}]},
            {"type": "paragraph", "content": [{"type": "text", "text": "Run it."}]},
            {"type": "paragraph"},
            {"type": "rule"},
            {"type": "codeBlock", "attrs": {"language": "cpp"}, "content": [{"type": "text", "text": "int x;\nint y;"}]},
            {"type": "blockquote", "content": [
              {"type": "paragraph", "content": [{"type": "text", "text": "one"}]},
              {"type": "paragraph", "content": [{"type": "text", "text": "two"}]}]}
        )") == "## Repro\n\nRun it.\n\n---\n\n```cpp\nint x;\nint y;\n```\n\n> one\n>\n> two");
    }
    SECTION("lists nest and number from their order") {
        CHECK(Render(*jira, R"(
            {"type": "bulletList", "content": [
              {"type": "listItem", "content": [
                {"type": "paragraph", "content": [{"type": "text", "text": "a"}]},
                {"type": "orderedList", "attrs": {"order": 3}, "content": [
                  {"type": "listItem", "content": [{"type": "paragraph", "content": [{"type": "text", "text": "c"}]}]},
                  {"type": "listItem", "content": [{"type": "paragraph", "content": [{"type": "text", "text": "d"}]}]}]}]},
              {"type": "listItem", "content": [{"type": "paragraph", "content": [{"type": "text", "text": "b"}]}]}]},
            {"type": "taskList", "content": [
              {"type": "taskItem", "attrs": {"state": "DONE"}, "content": [{"type": "text", "text": "done"}]},
              {"type": "taskItem", "attrs": {"state": "TODO"}, "content": [{"type": "text", "text": "todo"}]}]}
        )") == "- a\n  3. c\n  4. d\n- b\n\n- [x] done\n- [ ] todo");
    }
    SECTION("tables get a header separator and escaped pipes") {
        CHECK(Render(*jira, R"(
            {"type": "table", "content": [
              {"type": "tableRow", "content": [
                {"type": "tableHeader", "content": [{"type": "paragraph", "content": [{"type": "text", "text": "Key"}]}]},
                {"type": "tableHeader", "content": [{"type": "paragraph", "content": [{"type": "text", "text": "Value"}]}]}]},
              {"type": "tableRow", "content": [
                {"type": "tableCell", "content": [{"type": "paragraph", "content": [{"type": "text", "text": "a|b"}]}]}]}]}
        )") == "| Key | Value |\n| --- | --- |\n| a\\|b |  |");
    }
    SECTION("panels, expands and media degrade to readable text") {
        CHECK(Render(*jira, R"(
            {"type": "panel", "attrs": {"panelType": "warning"}, "content": [
              {"type": "paragraph", "content": [{"type": "text", "text": "Careful"}]}]},
            {"type": "expand", "attrs": {"title": "Logs"}, "content": [
              {"type": "paragraph", "content": [{"type": "text", "text": "trace"}]}]},
            {"type": "mediaSingle", "content": [{"type": "media", "attrs": {"type": "file", "id": "x"}}]},
            {"type": "someFutureNode", "content": [{"type": "paragraph", "content": [{"type": "text", "text": "still here"}]}]}
        )") == "> **Warning**\n>\n> Careful\n\n**Logs**\n\ntrace\n\n[attachment]\n\nstill here");
    }
}

namespace {

// The ADF document a comment of `markdown` sends.
nlohmann::json CommentDocument(const Provider& jira, const std::string& markdown) {
    const tracker::CommandSpec spec = jira.CommentArgv(kSite, "NED-1", markdown);
    REQUIRE(spec.input);
    return nlohmann::json::parse(*spec.input).at("body");
}

nlohmann::json Text(const std::string& text, nlohmann::json marks = nullptr) {
    nlohmann::json node{{"type", "text"}, {"text", text}};
    if (!marks.is_null()) {
        node["marks"] = std::move(marks);
    }
    return node;
}

} // namespace

TEST_CASE("tracker-jira posts a comment as ADF through a private input file", "[TrackerJira]") {
    RegistryResetGuard guard;
    const auto         jira = LoadJiraProvider();
    REQUIRE(jira->Supports(tracker::Capability::Comment));

    const tracker::CommandSpec spec = jira->CommentArgv(kSite, "ned-1", "Looks good");
    CHECK(spec.argv == Curl({"-X", "POST", "-H", "Content-Type: application/json", "--data-binary", "@{input-file}"},
                            "https://example.atlassian.net/rest/api/3/issue/NED-1/comment"));
    CHECK(spec.curlCredentials);
    REQUIRE(spec.input);
    CHECK(nlohmann::json::parse(*spec.input) ==
          nlohmann::json{{"body",
                          {{"version", 1},
                           {"type", "doc"},
                           {"content", nlohmann::json::array({{{"type", "paragraph"}, {"content", {Text("Looks good")}}}})}}}});

    CHECK_THROWS_WITH(jira->CommentArgv(kSite, "NED-1/../2", "x"), ContainsSubstring("isn't a Jira issue key"));
}

TEST_CASE("tracker-jira writes Markdown inline marks as ADF", "[TrackerJira]") {
    RegistryResetGuard guard;
    const auto         jira = LoadJiraProvider();

    const nlohmann::json paragraph =
        CommentDocument(*jira, "Hi **bold** and `a*b` and *em* ~~no~~ [site](https://x.io)\nsee https://y.io/a. \\*not em\\*")
            .at("content")
            .at(0);
    const nlohmann::json expected = nlohmann::json::array({
        Text("Hi "),
        Text("bold", {{{"type", "strong"}}}),
        Text(" and "),
        Text("a*b", {{{"type", "code"}}}),
        Text(" and "),
        Text("em", {{{"type", "em"}}}),
        Text(" "),
        Text("no", {{{"type", "strike"}}}),
        Text(" "),
        Text("site", {{{"type", "link"}, {"attrs", {{"href", "https://x.io"}}}}}),
        {{"type", "hardBreak"}},
        Text("see "),
        Text("https://y.io/a", {{{"type", "link"}, {"attrs", {{"href", "https://y.io/a"}}}}}),
        Text(". *not em*"),
    });
    CHECK(paragraph.at("content") == expected);

    // Nested marks accumulate; a lone or unclosed delimiter is text.
    CHECK(CommentDocument(*jira, "**a *b* c**").at("content").at(0).at("content") ==
          nlohmann::json::array({Text("a ", {{{"type", "strong"}}}), Text("b", {{{"type", "strong"}}, {{"type", "em"}}}),
                                 Text(" c", {{{"type", "strong"}}})}));
    CHECK(CommentDocument(*jira, "2 * 3 and snake_case_name and **open").at("content").at(0).at("content") ==
          nlohmann::json::array({Text("2 * 3 and snake_case_name and **open")}));
}

TEST_CASE("tracker-jira's Markdown writer round-trips through its ADF renderer", "[TrackerJira]") {
    RegistryResetGuard guard;
    const auto         jira = LoadJiraProvider();

    const std::string markdown = "# Plan\n"
                                 "\n"
                                 "Some **bold**, *em*, ~~gone~~, `code` and a [link](https://x.io).\n"
                                 "Second line.\n"
                                 "\n"
                                 "- one\n"
                                 "- two\n"
                                 "  - nested\n"
                                 "- three\n"
                                 "\n"
                                 "3. third\n"
                                 "4. fourth\n"
                                 "\n"
                                 "> quoted\n"
                                 "> more\n"
                                 "\n"
                                 "---\n"
                                 "\n"
                                 "```cpp\n"
                                 "int x;\n"
                                 "\n"
                                 "  y();\n"
                                 "```";
    const std::string adf      = CommentDocument(*jira, markdown).dump();
    CHECK(jira->ParseView(ViewWithDescription(adf)).body == markdown);
}

TEST_CASE("tracker-jira's Markdown writer keeps list items' own paragraphs and lazy lines", "[TrackerJira]") {
    RegistryResetGuard guard;
    const auto         jira = LoadJiraProvider();

    const nlohmann::json list = CommentDocument(*jira, "1. first\ncontinued\n\n   more of first\n2. second").at("content").at(0);
    CHECK(list.at("type") == "orderedList");
    CHECK(list.at("attrs").at("order") == 1);
    REQUIRE(list.at("content").size() == 2);
    const nlohmann::json& first = list.at("content").at(0).at("content");
    REQUIRE(first.size() == 2);
    CHECK(first.at(0).at("content") == nlohmann::json::array({Text("first"), {{"type", "hardBreak"}}, Text("continued")}));
    CHECK(first.at(1).at("content") == nlohmann::json::array({Text("more of first")}));

    // A list ends at a line that isn't indented into it.
    const nlohmann::json blocks = CommentDocument(*jira, "- a\n\nafter").at("content");
    REQUIRE(blocks.size() == 2);
    CHECK(blocks.at(1).at("type") == "paragraph");
    // An empty fence is still a code block, with no empty text node.
    CHECK(CommentDocument(*jira, "```\n```").at("content").at(0) == nlohmann::json{{"type", "codeBlock"}, {"content", nlohmann::json::array()}});
}

TEST_CASE("tracker-jira lists transitions and moves an issue through one", "[TrackerJira]") {
    RegistryResetGuard guard;
    const auto         jira = LoadJiraProvider();
    REQUIRE(jira->Supports(tracker::Capability::Transition));

    const tracker::CommandSpec list = jira->TransitionsArgv(kSite, "ned-7");
    CHECK(list.argv == Curl({}, "https://example.atlassian.net/rest/api/3/issue/NED-7/transitions"));
    CHECK(list.curlCredentials);

    CHECK(jira->ParseTransitions(R"({"transitions": [{"id": "11", "name": "To Do", "to": {"name": "To Do"}},
                                                     {"id": "21", "name": "Start work", "to": {"name": "In Progress"}},
                                                     {"id": "31", "name": "Done"}]})") ==
          std::vector<tracker::Choice>{{.id = "11", .name = "To Do"}, {.id = "21", .name = "Start work → In Progress"}, {.id = "31", .name = "Done"}});

    const tracker::CommandSpec move = jira->TransitionArgv(kSite, "NED-7", "21");
    CHECK(move.argv == Curl({"-X", "POST", "-H", "Content-Type: application/json", "--data-binary", "@{input-file}"},
                            "https://example.atlassian.net/rest/api/3/issue/NED-7/transitions"));
    CHECK(move.input == R"({"transition":{"id":"21"}})");
}

TEST_CASE("tracker-jira lists assignable users and assigns or unassigns", "[TrackerJira]") {
    RegistryResetGuard guard;
    const auto         jira = LoadJiraProvider();
    REQUIRE(jira->Supports(tracker::Capability::Assign));

    CHECK(jira->AssigneesArgv(kSite, "NED-7").argv ==
          Curl({"-G", "--data-urlencode", "issueKey=NED-7", "--data-urlencode", "maxResults=100"},
               "https://example.atlassian.net/rest/api/3/user/assignable/search"));
    CHECK_THROWS_WITH(jira->AssigneesArgv(kSite, "NED-7&x=1"), ContainsSubstring("isn't a Jira issue key"));

    CHECK(jira->ParseAssignees(R"([{"accountId": "a1", "displayName": "Ann", "emailAddress": "ann@example.com", "accountType": "atlassian"},
                                   {"accountId": "bot", "displayName": "Automation", "accountType": "app"},
                                   {"accountId": "b2", "displayName": "Bo"}])") ==
          std::vector<tracker::Choice>{{.id = "a1", .name = "Ann", .email = "ann@example.com"}, {.id = "b2", .name = "Bo"}, {.id = "", .name = "Unassigned"}});

    const tracker::CommandSpec assign = jira->AssignArgv(kSite, "NED-7", "a1");
    CHECK(assign.argv == Curl({"-X", "PUT", "-H", "Content-Type: application/json", "--data-binary", "@{input-file}"},
                              "https://example.atlassian.net/rest/api/3/issue/NED-7/assignee"));
    CHECK(assign.input == R"({"accountId":"a1"})");
    CHECK(jira->AssignArgv(kSite, "NED-7", "").input == R"({"accountId":null})");
}

TEST_CASE("tracker-jira lists project keys and the user's own issues", "[TrackerJira]") {
    RegistryResetGuard guard;
    const auto         jira = LoadJiraProvider();
    REQUIRE(jira->Supports(tracker::Capability::ProjectKeys));
    REQUIRE(jira->Supports(tracker::Capability::Mine));

    const tracker::CommandSpec keys = jira->ProjectKeysArgv(kSite);
    CHECK(keys.argv == Curl({"-G", "--data-urlencode", "maxResults=100"}, "https://example.atlassian.net/rest/api/3/project/search"));
    CHECK(keys.curlCredentials);
    CHECK(jira->ParseProjectKeys(R"({"values": [{"key": "DEV", "name": "Development"}, {"name": "keyless"}, {"key": "OPS"}]})") ==
          std::vector<std::string>{"DEV", "OPS"});

    CHECK(jira->MineQuery(kSite) == "assignee = currentUser() OR watcher = currentUser() ORDER BY updated DESC");
}

TEST_CASE("tracker-jira posts a worklog started in UTC", "[TrackerJira]") {
    RegistryResetGuard guard;
    const auto         jira = LoadJiraProvider();
    REQUIRE(jira->Supports(tracker::Capability::Worklog));

    // 2026-09-29T08:05:00Z
    const tracker::CommandSpec spec = jira->WorklogArgv(kSite, "NED-7", tracker::Worklog{.started = 1790669100, .seconds = 5100});
    CHECK(spec.argv == Curl({"-X", "POST", "-H", "Content-Type: application/json", "--data-binary", "@{input-file}"},
                            "https://example.atlassian.net/rest/api/3/issue/NED-7/worklog"));
    CHECK(nlohmann::json::parse(*spec.input) == nlohmann::json{{"timeSpentSeconds", 5100}, {"started", "2026-09-29T08:05:00.000+0000"}});
}
