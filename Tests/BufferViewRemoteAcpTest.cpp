//
// ACP commands typed into a BufferView on a remote buffer, against an
// agent on a fake host (FakeRemoteAcpAgent.h).
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Editor/Acp/Manager.h"
#include "Editor/Acp/TurnReview.h"
#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Remote/Hosts.h"
#include "FakeRemoteAcpAgent.h"
#include "FakeRemoteHost.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/AcpPanel.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EventLoop.h"
#include "UI/Theme.h"

using Catch::Matchers::ContainsSubstring;
using ned::tests::PumpUntil;
namespace acp  = ned::editor::acp;
namespace test = ned::ui::test;
namespace text = ned::text;
using Json     = nlohmann::json;

namespace {

struct Fixture {
    ned::tests::FakeRemoteHost     fake;
    ned::tests::ScopedEnvOverride  home{"HOME", (fake.dir / "home").c_str()};
    ned::ui::EventLoop             loop;
    ned::editor::remote::Hosts     hosts{loop, [this](const std::string& destination) { return fake.Options(destination); },
                                         ned::tests::FakeResolver(fake.dir)};
    ned::tests::FakeRemoteAcpAgent agent{fake.dir};

    text::Buffer                 buffer{"scratch"};
    text::KillRing               killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    text::BufferList             bufferList;
    acp::Manager                 acpManager{bufferList, loop};
    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode = [] {
        ned::editor::Mode m = ned::editor::FundamentalMode();
        m.name              = "c-mode";
        return m;
    }();
    ned::ui::Theme theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};
    ned::ui::BufferView   view{activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme};

    Fixture() {
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
        view.SetRemoteHosts(&hosts);
        view.SetAcpManager(&acpManager);
        acpManager.SetRemoteHosts(&hosts);
    }

    void Command(const std::string& name) {
        view.OnEvent(test::Alt('x'));
        for (const char c : name) {
            view.OnEvent(test::Character(std::string(1, c)));
        }
        view.OnEvent(test::Return());
    }
    [[nodiscard]] text::RemoteFile File() const {
        return {.destination = "host", .path = agent.file};
    }
    [[nodiscard]] bool Showing() const {
        const text::Buffer& shown = activeBuffer.Get();
        return shown.Remote() == File() && !shown.IsLoading();
    }
    bool OpenFile() {
        view.VisitRemoteFile(File());
        return PumpUntil(loop, [&] { return Showing(); });
    }
};

} // namespace

TEST_CASE("acp-start-session on a remote buffer runs the agent on its host", "[BufferView][Remote][Acp][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    fixture.Command("acp-start-session");
    for (const char c : std::string(ned::tests::kRemoteAcpAgent)) {
        fixture.view.OnEvent(test::Character(std::string(1, c)));
    }
    fixture.view.OnEvent(test::Return());

    CHECK(fixture.acpManager.AgentBeside() == std::optional<text::RemoteFile>(fixture.File()));
    CHECK_THAT(fixture.activeBuffer.Get().Name(), ContainsSubstring("*acp: "));
    REQUIRE(PumpUntil(fixture.loop, [&] { return fixture.acpManager.State() == acp::Manager::SessionState::Active; }));
    CHECK(fixture.agent.Requests("session/new").front()["params"]["cwd"] == fixture.agent.project.string());
    fixture.acpManager.StopSession();
}

namespace {

std::string WriteScript(const text::RemoteFile& file, const std::string& content) {
    return Json::array({Json{{"method", "fs/write_text_file"}, {"params", {{"sessionId", "s1"}, {"path", file.path.string()}, {"content", content}}}}}).dump();
}

bool Start(Fixture& fixture) {
    fixture.acpManager.StartSession(ned::tests::kRemoteAcpAgent, fixture.File());
    return PumpUntil(fixture.loop, [&] { return fixture.acpManager.State() == acp::Manager::SessionState::Active; });
}

bool Turn(Fixture& fixture, const std::string& prompt) {
    const std::size_t turns = fixture.acpManager.CheckpointCount();
    return fixture.acpManager.SendPrompt(prompt) == "Sent." &&
           PumpUntil(fixture.loop, [&] { return !fixture.acpManager.PromptInFlight() && fixture.acpManager.CheckpointCount() > turns; });
}

} // namespace

TEST_CASE("A host's ACP agent reads an open buffer's own text", "[BufferView][Remote][Acp][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    text::Buffer& file = fixture.activeBuffer.Get();
    file.SetPoint(file.Text().size());
    file.InsertAtPoint("int z;\n");
    REQUIRE(Start(fixture));

    REQUIRE(Turn(fixture, Json::array({Json{{"method", "fs/read_text_file"}, {"params", {{"sessionId", "s1"}, {"path", fixture.agent.file.string()}}}}}).dump()));
    const std::vector<Json> answers = fixture.agent.Answers();
    REQUIRE(answers.size() == 1);
    CHECK(answers[0]["result"]["content"] == "int x;\nint y;\nint z;\n");
    fixture.acpManager.StopSession();
}

TEST_CASE("A host's ACP agent's write reaches the open buffer, and rewinds", "[BufferView][Remote][Acp][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    text::Buffer& file = fixture.activeBuffer.Get();
    REQUIRE(Start(fixture));

    REQUIRE(Turn(fixture, WriteScript(fixture.File(), "int x;\nint w;\n")));
    std::ifstream in(fixture.agent.file);
    CHECK(std::string(std::istreambuf_iterator<char>(in), {}) == "int x;\nint w;\n");
    CHECK(file.Text() == "int x;\nint w;\n");
    CHECK_FALSE(file.Modified());
    REQUIRE(fixture.acpManager.CheckpointAt(0).fileRecords.size() == 1);
    CHECK(fixture.acpManager.CheckpointAt(0).fileRecords[0].file == text::FileRef(fixture.File()));

    const acp::Manager::RewindOutcome outcome = fixture.acpManager.RewindTo(0);
    CHECK(outcome.revertedFiles == std::vector<std::string>{fixture.File().ToString()});
    CHECK(file.Text() == "int x;\nint y;\n");
    fixture.acpManager.StopSession();
}

TEST_CASE("A host's ACP agent's write merges into a buffer with changes of its own", "[BufferView][Remote][Acp][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    text::Buffer& file = fixture.activeBuffer.Get();
    file.SetPoint(file.Text().size());
    file.InsertAtPoint("int z;\n");
    REQUIRE(Start(fixture));

    REQUIRE(Turn(fixture, WriteScript(fixture.File(), "int x;\nint w;\n")));
    CHECK(file.Text() == "int x;\nint w;\nint z;\n");
    CHECK(file.Modified());
    fixture.acpManager.StopSession();
}

namespace {

// The agent says it's editing the file, then edits it itself.
std::string OwnEditScript(const text::RemoteFile& file, const std::string& content) {
    const Json toolCall{{"sessionUpdate", "tool_call"},
                        {"toolCallId", "t1"},
                        {"title", "Edit a.c"},
                        {"kind", "edit"},
                        {"status", "in_progress"},
                        {"locations", Json::array({Json{{"path", file.path.string()}}})}};
    return Json::array({Json{{"method", "session/update"}, {"notify", true}, {"params", {{"sessionId", "s1"}, {"update", toolCall}}}},
                        Json{{"write", file.path.string()}, {"text", content}}})
        .dump();
}

bool Reviewable(const Fixture& fixture) {
    const auto& transcript = fixture.acpManager.Transcript();
    return std::any_of(transcript.begin(), transcript.end(), [](const acp::Manager::TranscriptEntry& entry) { return entry.status == "review"; });
}

} // namespace

TEST_CASE("A host's ACP agent's own edit to an open file is reviewed and rewound", "[BufferView][Remote][Acp][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    text::Buffer& file = fixture.activeBuffer.Get();
    REQUIRE(Start(fixture));

    REQUIRE(Turn(fixture, OwnEditScript(fixture.File(), "int x;\nint w;\n")));
    // Settled at the turn's end, whether or not the host's watch got there first.
    REQUIRE(PumpUntil(fixture.loop, [&] { return Reviewable(fixture); }));
    CHECK(file.Text() == "int x;\nint w;\n");
    const acp::Manager::Checkpoint& turn = fixture.acpManager.CheckpointAt(0);
    REQUIRE(turn.files.size() == 1);
    CHECK(turn.files[0].file == text::FileRef(fixture.File()));
    CHECK(turn.files[0].before.text == "int x;\nint y;\n");
    REQUIRE(turn.files[0].after.has_value());
    CHECK(turn.files[0].after->text == "int x;\nint w;\n");

    const acp::Manager::RewindOutcome outcome = fixture.acpManager.RewindTo(0);
    CHECK(outcome.revertedFiles == std::vector<std::string>{fixture.File().ToString()});
    // Back in the buffer, for the user to save.
    CHECK(file.Text() == "int x;\nint y;\n");
    CHECK(file.Modified());
    fixture.acpManager.StopSession();
}

TEST_CASE("A host's ACP turn review names its files against the host's root and visits them there", "[BufferView][Remote][Acp][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    text::Buffer& file = fixture.activeBuffer.Get();
    REQUIRE(Start(fixture));
    REQUIRE(Turn(fixture, OwnEditScript(fixture.File(), "int x;\nint w;\n")));
    REQUIRE(PumpUntil(fixture.loop, [&] { return Reviewable(fixture); }));

    fixture.view.OpenAcpTurnReview("edit", fixture.acpManager.CheckpointAt(0).files);
    text::Buffer& review = fixture.activeBuffer.Get();
    REQUIRE(review.Name() == acp::kReviewBufferName);
    CHECK_THAT(review.Text(), ContainsSubstring("▸ src/a.c:2"));

    fixture.view.HandleAcpReviewRequest(ned::editor::InteractiveRequest::AcpReviewUndoHunk);
    CHECK(file.Text() == "int x;\nint y;\n");

    // Undoing rebuilt the review buffer.
    text::Buffer& rebuilt = fixture.activeBuffer.Get();
    REQUIRE(rebuilt.Name() == acp::kReviewBufferName);
    rebuilt.SetPoint(rebuilt.Text().find("- int y;"));
    fixture.view.OnEvent(test::Return());
    REQUIRE(PumpUntil(fixture.loop, [&] { return fixture.Showing(); }));
    CHECK(&fixture.activeBuffer.Get() == &file);
    fixture.acpManager.StopSession();
}

TEST_CASE("The ACP composer names a host agent's files by their paths there", "[BufferView][Remote][Acp][AcpPanel][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    REQUIRE(Start(fixture));
    ned::ui::AcpPanel panel{fixture.theme};
    panel.SetAcpManager(&fixture.acpManager);
    panel.SetBufferList(&fixture.bufferList);
    panel.SetActiveBufferProvider([&fixture]() -> ned::ui::ActiveBuffer& { return fixture.activeBuffer; });
    panel.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 30});
    const auto type = [&panel](std::string_view text) {
        for (const char c : text) {
            panel.OnEvent(test::Character(std::string(1, c)));
        }
    };

    // Completion offers the host's open file; nothing on the host's disk can be listed.
    type("@src/a");
    panel.OnEvent(test::Return());
    type(" @src/gone.c @buffer");
    panel.OnEvent(test::Return());
    panel.OnEvent(test::Return());
    REQUIRE(PumpUntil(fixture.loop, [&] { return fixture.agent.Requests("session/prompt").size() == 1; }));

    const Json        prompt = fixture.agent.Requests("session/prompt").front()["params"]["prompt"];
    std::vector<Json> links;
    std::vector<Json> resources;
    for (const Json& block : prompt) {
        (block["type"] == "resource_link" ? links : resources).push_back(block);
    }
    REQUIRE(links.size() == 1);
    CHECK(links[0]["uri"] == "file://" + fixture.agent.file.string());
    CHECK(links[0]["name"] == "src/a.c");
    const auto resource = std::find_if(resources.begin(), resources.end(), [](const Json& block) { return block["type"] == "resource"; });
    REQUIRE(resource != resources.end());
    CHECK((*resource)["resource"]["uri"] == "file://" + fixture.agent.file.string());
    fixture.acpManager.StopSession();
}
