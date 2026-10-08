//
// A debug adapter for tests on a fake host (FakeRemoteHost.h): a Python
// script the host runs, logging what it is sent, over a small project in a
// repository of its own.
//

#ifndef NED_TESTS_FAKEREMOTEDAPADAPTER_H
#define NED_TESTS_FAKEREMOTEDAPADAPTER_H

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Dap/Config.h"

namespace ned::tests {

inline constexpr const char* kRemoteDapLanguage = "remote-dap-test";

// Logs its working directory, then every request it is sent, one JSON
// object a line. Sends initialized once launched, stops (thread 1) once
// configured, reports a single frame at line 2 of the file it was given,
// and terminates when continued. Anything else it is asked succeeds with
// an empty body.
inline constexpr const char* kRemoteDapAdapter = R"(import json, os, sys
log = open(sys.argv[1], "a")
log.write(json.dumps({"cwd": os.getcwd()}) + "\n")
log.flush()
source = sys.argv[2]
inp, out = sys.stdin.buffer, sys.stdout.buffer
seq = 0
def send(message):
    global seq
    seq += 1
    body = json.dumps(dict(seq=seq, **message)).encode()
    out.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
    out.flush()
def event(name, body={}):
    send({"type": "event", "event": name, "body": body})
while True:
    length = None
    while True:
        line = inp.readline()
        if not line:
            sys.exit(0)
        line = line.strip()
        if not line:
            break
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":")[1])
    request = json.loads(inp.read(length))
    log.write(json.dumps(request) + "\n")
    log.flush()
    command, arguments, body = request["command"], request.get("arguments", {}), {}
    if command == "initialize":
        body = {"supportsConfigurationDoneRequest": True}
    elif command == "setBreakpoints":
        body = {"breakpoints": [{"verified": True, "line": b["line"]} for b in arguments["breakpoints"]]}
    elif command == "stackTrace":
        body = {"stackFrames": [{"id": 1, "name": "main", "line": 2, "column": 1, "source": {"path": source}}]}
    elif command == "threads":
        body = {"threads": [{"id": 1, "name": "main"}]}
    send({"type": "response", "request_seq": request["seq"], "success": True, "command": command, "body": body})
    if command == "launch":
        event("initialized")
    elif command == "configurationDone":
        event("stopped", {"reason": "breakpoint", "threadId": 1})
    elif command == "continue":
        event("terminated")
    elif command == "disconnect":
        sys.exit(0)
)";

// dir/project is a repository holding src/a.c ("int x;\nint y;\n"); the
// adapter reports its frames in that file.
struct FakeRemoteDapAdapter {
    std::filesystem::path log;
    std::filesystem::path project;
    std::filesystem::path file;

    explicit FakeRemoteDapAdapter(const std::filesystem::path& dir) : log(dir / "adapter.log") {
        std::filesystem::create_directories(dir / "project" / ".git");
        std::filesystem::create_directories(dir / "project" / "src");
        std::ofstream(dir / "project" / "src" / "a.c") << "int x;\nint y;\n";
        std::ofstream(dir / "adapter.py") << kRemoteDapAdapter;
        project = std::filesystem::canonical(dir / "project");
        file    = project / "src" / "a.c";
        editor::dap::SetAdapterCommand(kRemoteDapLanguage, {"python3", (dir / "adapter.py").string(), log.string(), file.string()});
        editor::dap::SetLaunchConfig(kRemoteDapLanguage, R"({"program": "a.out"})");
    }
    ~FakeRemoteDapAdapter() {
        editor::dap::SetAdapterCommand(kRemoteDapLanguage, {});
        editor::dap::SetLaunchConfig(kRemoteDapLanguage, "");
    }
    FakeRemoteDapAdapter(const FakeRemoteDapAdapter&)            = delete;
    FakeRemoteDapAdapter& operator=(const FakeRemoteDapAdapter&) = delete;

    [[nodiscard]] std::vector<nlohmann::json> Messages() const {
        std::vector<nlohmann::json> messages;
        std::ifstream               in(log);
        for (std::string line; std::getline(in, line);) {
            messages.push_back(nlohmann::json::parse(line));
        }
        return messages;
    }
    [[nodiscard]] std::vector<nlohmann::json> Requests(const std::string& command) const {
        std::vector<nlohmann::json> requests;
        for (nlohmann::json& message : Messages()) {
            if (message.value("command", "") == command) {
                requests.push_back(std::move(message));
            }
        }
        return requests;
    }
};

} // namespace ned::tests

#endif // NED_TESTS_FAKEREMOTEDAPADAPTER_H
