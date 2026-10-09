#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Editor/AutoMerge.h"
#include "Editor/Lifetime.h"
#include "Editor/PostQueue.h"
#include "Editor/Remote/ExternalChanges.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/Remote/RemoteBuffer.h"
#include "Editor/Remote/Watch.h"
#include "FakeRemoteHost.h"
#include "Text/BufferList.h"

using ned::tests::PumpUntil;
namespace fs     = std::filesystem;
namespace remote = ned::editor::remote;

namespace {

struct Fixture {
    ned::tests::FakeRemoteHost                                  fake;
    ned::editor::PostQueue                                      queue;
    remote::Hosts                                               hosts{queue, [this](const std::string& destination) { return fake.Options(destination); },
                                                                      ned::tests::FakeResolver(fake.dir)};
    std::vector<std::pair<std::string, ned::editor::FileEvent>>                    events;
    std::function<void(const std::string&, const ned::editor::host::WatchEvents&)> onEvents;
    remote::Watches                                                                watches{hosts, [this](const std::string& destination, const ned::editor::host::WatchEvents& batch) {
                                for (const ned::editor::FileEvent& event : batch.events) {
                                    events.emplace_back(destination, event);
                                }
                                if (onEvents) {
                                    onEvents(destination, batch);
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

TEST_CASE("A host's change reverts, merges or marks the remote buffers holding its file", "[Remote][RemoteWatch][HostAgent]") {
    using Outcome = remote::RemoteChange::Outcome;
    Fixture        fixture;
    const fs::path dir = fixture.fake.dir / "proj";
    fs::create_directories(dir);
    for (const char* name : {"a.txt", "b.txt", "c.txt", "d.txt", "e.txt"}) {
        std::ofstream(dir / name) << "one\ntwo\nthree\n";
    }
    REQUIRE(fixture.Connect());

    ned::text::BufferList                     list;
    ned::editor::LifetimeGuard                guard;
    std::map<std::string, ned::text::Buffer*> open;
    for (const char* name : {"a.txt", "b.txt", "c.txt", "d.txt", "e.txt"}) {
        ned::text::Buffer& placeholder = list.AddRemotePlaceholder({.destination = "host", .path = dir / name});
        remote::LoadRemoteBuffer(fixture.hosts, list, placeholder, false, guard.Token(),
                                 [&open, name](ned::text::Buffer& buffer, const remote::RemoteLoadOutcome& outcome) {
                                     REQUIRE(outcome.loaded.has_value());
                                     open[name] = &buffer;
                                 });
    }
    REQUIRE(PumpUntil(fixture.queue, [&] { return open.size() == 5; }));

    std::vector<remote::RemoteChange> changes;
    const auto                        record  = [&changes](const remote::RemoteChange& change) { changes.push_back(change); };
    const auto                        changed = [&changes](const std::string& name, Outcome outcome) {
        return std::ranges::any_of(changes, [&](const remote::RemoteChange& change) { return change.name == name && change.outcome == outcome; });
    };
    fixture.onEvents = [&](const std::string& destination, const ned::editor::host::WatchEvents& events) {
        remote::FollowRemoteChanges(fixture.hosts, list, destination, events, guard.Token(), record);
    };
    fixture.watches.Set({{"host", remote::WatchSet{.files = {dir / "a.txt", dir / "b.txt", dir / "c.txt"}, .directories = {}}}});

    // Rewritten until noticed: the watch is only in force once the host has it.
    const auto rewriteUntil = [&](std::initializer_list<const char*> names, const std::function<bool()>& done) {
        return PumpUntil(fixture.queue, [&] {
            if (done()) {
                return true;
            }
            for (const char* name : names) {
                std::ofstream(dir / name) << "one\ntwo\nTHREE\n";
            }
            return false;
        });
    };

    ned::text::Buffer& b = *open["b.txt"];
    b.SetPoint(0);
    b.InsertAtPoint("zero\n");
    REQUIRE(rewriteUntil({"a.txt", "b.txt"}, [&] { return changed(open["a.txt"]->Name(), Outcome::Reverted) && changed(b.Name(), Outcome::Merged); }));
    CHECK(open["a.txt"]->Text() == "one\ntwo\nTHREE\n");
    CHECK_FALSE(open["a.txt"]->Modified());
    CHECK(b.Text() == "zero\none\ntwo\nTHREE\n");
    CHECK(b.Modified());

    // With auto-merge off, local edits stay and the next save asks.
    {
        struct Restore {
            ~Restore() {
                ned::editor::SetAutoMergeEnabled(true);
            }
        } restore;
        ned::editor::SetAutoMergeEnabled(false);
        ned::text::Buffer& c = *open["c.txt"];
        c.SetPoint(0);
        c.InsertAtPoint("zero\n");
        REQUIRE(rewriteUntil({"c.txt"}, [&] { return changed(c.Name(), Outcome::Noted); }));
        CHECK(c.Text() == "zero\none\ntwo\nthree\n");
        CHECK(c.ExternallyModified());
    }

    // A file still as the buffer last saw it -- the buffer's own save, say
    // -- changes nothing. Its stat is answered ahead of e's.
    changes.clear();
    std::ofstream(dir / "e.txt") << "one\ntwo\nTHREE\n";
    remote::FollowRemoteChanges(fixture.hosts, list, "host",
                                ned::editor::host::WatchEvents{.events   = {{.path = dir / "d.txt", .kind = ned::editor::FileEvent::Kind::Changed},
                                                                            {.path = dir / "e.txt", .kind = ned::editor::FileEvent::Kind::Changed}},
                                                               .overflow = false},
                                guard.Token(), record);
    REQUIRE(PumpUntil(fixture.queue, [&] { return changed(open["e.txt"]->Name(), Outcome::Reverted); }));
    CHECK(changes.size() == 1);
}

TEST_CASE("A move on a host carries the remote buffers on the file or under the directory along", "[Remote][RemoteWatch]") {
    ned::text::BufferList list;
    ned::text::Buffer&    file  = list.AddRemotePlaceholder({.destination = "host", .path = "/srv/a.txt"});
    ned::text::Buffer&    inner = list.AddRemotePlaceholder({.destination = "host", .path = "/srv/dir/b.txt"});
    ned::text::Buffer&    other = list.AddRemotePlaceholder({.destination = "other", .path = "/srv/a.txt"});
    ned::text::Buffer&    near  = list.AddRemotePlaceholder({.destination = "host", .path = "/srv/directory/c.txt"});

    const std::vector<ned::text::Buffer*> followed =
        remote::FollowRemoteMoves(list, "host", {{.from = "/srv/a.txt", .to = "/srv/z.txt"}, {.from = "/srv/dir", .to = "/srv/moved"}});
    CHECK(followed == std::vector<ned::text::Buffer*>{&file, &inner});
    CHECK(file.Remote() == ned::text::RemoteFile{.destination = "host", .path = "/srv/z.txt"});
    CHECK(file.Name() == "host:z.txt");
    CHECK(inner.Remote() == ned::text::RemoteFile{.destination = "host", .path = "/srv/moved/b.txt"});
    CHECK(other.Remote() == ned::text::RemoteFile{.destination = "other", .path = "/srv/a.txt"});
    // A sibling whose name only starts the same isn't under the directory.
    CHECK(near.Remote() == ned::text::RemoteFile{.destination = "host", .path = "/srv/directory/c.txt"});
}
