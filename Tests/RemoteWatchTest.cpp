#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "Editor/PostQueue.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/Remote/Watch.h"
#include "FakeRemoteHost.h"

using ned::tests::PumpUntil;
namespace fs     = std::filesystem;
namespace remote = ned::editor::remote;

namespace {

struct Fixture {
    ned::tests::FakeRemoteHost                                  fake;
    ned::editor::PostQueue                                      queue;
    remote::Hosts                                               hosts{queue, [this](const std::string& destination) { return fake.Options(destination); },
                                                                      ned::tests::FakeResolver(fake.dir)};
    std::vector<std::pair<std::string, ned::editor::FileEvent>> events;
    remote::Watches                                             watches{hosts, [this](const std::string& destination, const ned::editor::host::WatchEvents& batch) {
                                for (const ned::editor::FileEvent& event : batch.events) {
                                    events.emplace_back(destination, event);
                                }
                                                                        }};

    bool Connect() {
        bool connected = false;
        hosts.Connect("host", [&](const std::expected<remote::Connection*, std::string>& connection) {
            REQUIRE(connection.has_value());
            connected = true;
        });
        return PumpUntil(queue, [&] { return connected; });
    }

    [[nodiscard]] bool Saw(const fs::path& path) const {
        return std::ranges::any_of(events, [&](const auto& event) { return event.first == "host" && event.second.path == path; });
    }

    // Rewrites path until its event arrives: a watch/set is only in force
    // once the host has had it.
    bool TouchUntilSeen(const fs::path& path) {
        return PumpUntil(queue, [&] {
            if (Saw(path)) {
                return true;
            }
            std::ofstream(path) << "touched\n";
            return false;
        });
    }
};

} // namespace

TEST_CASE("A host is told what to watch once it connects, and again after a reconnect", "[Remote][RemoteWatch][HostAgent]") {
    Fixture        fixture;
    const fs::path dir = fixture.fake.dir / "proj";
    fs::create_directories(dir / "src");
    std::ofstream(dir / "a.txt") << "a\n";

    // Nothing is sent, or connected for, before the host is connected.
    fixture.watches.Set({{"host", remote::WatchSet{.files = {dir / "a.txt"}, .directories = {dir / "src"}}}});
    CHECK(fixture.hosts.Find("host") == nullptr);

    REQUIRE(fixture.Connect());
    fixture.watches.Resync();
    REQUIRE(fixture.TouchUntilSeen(dir / "a.txt"));
    std::ofstream(dir / "src" / "b.txt") << "b\n";
    REQUIRE(PumpUntil(fixture.queue, [&] { return fixture.Saw(dir / "src" / "b.txt"); }));

    // A changed set replaces the old one: d.txt is reported only once it's
    // in force, and src's entries aren't after it.
    fixture.watches.Set({{"host", remote::WatchSet{.files = {}, .directories = {dir}}}});
    REQUIRE(fixture.TouchUntilSeen(dir / "d.txt"));
    fixture.events.clear();
    std::ofstream(dir / "src" / "c.txt") << "c\n";
    REQUIRE(fixture.TouchUntilSeen(dir / "d.txt"));
    CHECK_FALSE(fixture.Saw(dir / "src" / "c.txt"));

    // A reconnect is a new connection, told again.
    REQUIRE(fixture.hosts.Disconnect("host"));
    REQUIRE(fixture.Connect());
    fixture.events.clear();
    fixture.watches.Resync();
    REQUIRE(fixture.TouchUntilSeen(dir / "e.txt"));
}
