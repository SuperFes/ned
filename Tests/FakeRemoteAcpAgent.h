//
// An ACP agent for tests on a fake host (FakeRemoteHost.h): a Python script
// the host runs, logging what it is sent, over a small project in a
// repository of its own.
//

#ifndef NED_TESTS_FAKEREMOTEACPAGENT_H
#define NED_TESTS_FAKEREMOTEACPAGENT_H

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Acp/Config.h"

namespace ned::tests {

inline constexpr const char* kRemoteAcpAgent = "remote-acp-test";

// Logs its working directory, then every message it is sent, one JSON
// object a line. A prompt whose text is a JSON array is a script: each
// {method, params} is sent to the client in turn, as a notification when
// it has "notify", else as a request whose answer is waited for, and each
// {write, text} writes that file itself, as an agent's own edit tool does.
// Every prompt then ends with the reply "done".
inline constexpr const char* kRemoteAcpAgentScript = R"(import json, os, sys
log = open(sys.argv[1], "a")
log.write(json.dumps({"cwd": os.getcwd()}) + "\n")
log.flush()
asked = 0
def send(message):
    sys.stdout.write(json.dumps(dict(jsonrpc="2.0", **message)) + "\n")
    sys.stdout.flush()
def read():
    line = sys.stdin.readline()
    if not line:
        sys.exit(0)
    message = json.loads(line)
    log.write(json.dumps(message) + "\n")
    log.flush()
    return message
def update(session, body):
    send({"method": "session/update", "params": {"sessionId": session, "update": body}})
def run(session, step):
    global asked
    if "write" in step:
        with open(step["write"], "w") as out:
            out.write(step["text"])
        return
    if step.get("notify"):
        send({"method": step["method"], "params": step["params"]})
        return
    asked += 1
    send({"id": "agent-%d" % asked, "method": step["method"], "params": step["params"]})
    read()
while True:
    message = read()
    method, params, result = message.get("method"), message.get("params", {}), {}
    if "id" not in message:
        continue
    if method == "initialize":
        result = {"protocolVersion": 1, "agentCapabilities": {"sessionCapabilities": {"list": {}, "resume": {}}}}
    elif method == "session/new":
        result = {"sessionId": "s1"}
    elif method == "session/list":
        result = {"sessions": []}
    elif method == "session/prompt":
        text = next((block["text"] for block in params["prompt"] if block.get("type") == "text"), "")
        try:
            script = json.loads(text)
        except ValueError:
            script = []
        for step in script if isinstance(script, list) else []:
            run(params["sessionId"], step)
        update(params["sessionId"], {"sessionUpdate": "agent_message_chunk", "content": {"type": "text", "text": "done"}})
        result = {"stopReason": "end_turn"}
    send({"id": message["id"], "result": result})
)";

// dir/project is a repository holding src/a.c ("int x;\nint y;\n").
struct FakeRemoteAcpAgent {
    std::filesystem::path log;
    std::filesystem::path project;
    std::filesystem::path file;

    explicit FakeRemoteAcpAgent(const std::filesystem::path& dir) : log(dir / "agent.log") {
        std::filesystem::create_directories(dir / "project" / ".git");
        std::filesystem::create_directories(dir / "project" / "src");
        std::ofstream(dir / "project" / "src" / "a.c") << "int x;\nint y;\n";
        std::ofstream(dir / "acp-agent.py") << kRemoteAcpAgentScript;
        project = std::filesystem::canonical(dir / "project");
        file    = project / "src" / "a.c";
        editor::acp::SetAcpAgentCommand(kRemoteAcpAgent, {"python3", (dir / "acp-agent.py").string(), log.string()});
    }
    ~FakeRemoteAcpAgent() {
        editor::acp::SetAcpAgentCommand(kRemoteAcpAgent, {});
    }
    FakeRemoteAcpAgent(const FakeRemoteAcpAgent&)            = delete;
    FakeRemoteAcpAgent& operator=(const FakeRemoteAcpAgent&) = delete;

    [[nodiscard]] std::vector<nlohmann::json> Messages() const {
        std::vector<nlohmann::json> messages;
        std::ifstream               in(log);
        for (std::string line; std::getline(in, line);) {
            messages.push_back(nlohmann::json::parse(line));
        }
        return messages;
    }
    [[nodiscard]] std::vector<nlohmann::json> Requests(const std::string& method) const {
        std::vector<nlohmann::json> requests;
        for (nlohmann::json& message : Messages()) {
            if (message.value("method", "") == method) {
                requests.push_back(std::move(message));
            }
        }
        return requests;
    }
    // What the client answered the agent's own requests, in order.
    [[nodiscard]] std::vector<nlohmann::json> Answers() const {
        std::vector<nlohmann::json> answers;
        for (nlohmann::json& message : Messages()) {
            if (message.contains("id") && !message.contains("method") && message["id"].is_string()) {
                answers.push_back(std::move(message));
            }
        }
        return answers;
    }
};

} // namespace ned::tests

#endif // NED_TESTS_FAKEREMOTEACPAGENT_H
