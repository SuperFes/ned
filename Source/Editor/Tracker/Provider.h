//
// The tracker-neutral vocabulary an issue-tracker plugin implements. Same
// split as Vcs/Provider.h: a provider only builds argv and parses captured
// output, both on the main thread (a Janet-backed provider can't run
// anywhere else); Tracker/Runner.h spawns and waits in between.
//

#ifndef NED_EDITOR_TRACKER_PROVIDER_H
#define NED_EDITOR_TRACKER_PROVIDER_H

#include <optional>
#include <string>
#include <vector>

namespace ned::editor::tracker {

// One issue as a panel lists it. Every field is the tracker's own text:
// key is "PROJ-123" or "#42", updated is whatever timestamp format the
// tracker returns (the panel only displays and sorts it).
struct Issue {
    std::string              key;
    std::string              title;
    std::string              status;
    std::string              assignee;
    std::vector<std::string> labels;
    std::string              url;
    std::string              updated;

    bool operator==(const Issue&) const = default;
};

struct Comment {
    std::string author;
    std::string created;
    std::string body;

    bool operator==(const Comment&) const = default;
};

// One issue as its own buffer shows it. body and comment bodies are text
// the provider has already flattened (Markdown for GitHub; Jira's ADF is
// the provider's to render).
struct IssueDetail {
    Issue                issue;
    std::string          body;
    std::vector<Comment> comments;

    bool operator==(const IssueDetail&) const = default;
};

// A named tracker instance. The token is never stored: tokenCommand is run
// when a provider needs one, and its output never reaches an argv.
struct Connection {
    std::string              name;
    std::string              provider;
    std::string              url;
    std::string              email;
    std::vector<std::string> tokenCommand;

    bool operator==(const Connection&) const = default;
};

// A named view over one connection. query is opaque to ned -- JQL for
// Jira, a search string for GitHub -- and handed to the provider verbatim.
struct Panel {
    std::string name;
    std::string connection;
    std::string query;
    std::string glyph;

    bool operator==(const Panel&) const = default;
};

struct CommandSpec {
    std::vector<std::string> argv;
};

// A connection a provider recognized in the project's git remotes, with the
// panels to show for it. Each panel's connection is connection.name.
struct Detected {
    Connection         connection;
    std::vector<Panel> panels;

    bool operator==(const Detected&) const = default;
};

class Provider {
  public:
    virtual ~Provider() = default;

    [[nodiscard]] virtual CommandSpec        ListArgv(const Connection& connection, const std::string& query) const = 0;
    [[nodiscard]] virtual std::vector<Issue> ParseList(const std::string& output) const                             = 0;

    // std::nullopt when the provider has no detail view: an issue buffer
    // then shows only what the list already fetched.
    [[nodiscard]] virtual std::optional<CommandSpec> ViewArgv(const Connection& connection, const std::string& key) const = 0;
    [[nodiscard]] virtual IssueDetail                ParseView(const std::string& output) const                           = 0;

    // Given the project's git remote URLs; empty when the provider
    // recognizes none, or detects nothing at all.
    [[nodiscard]] virtual std::vector<Detected> Detect(const std::vector<std::string>& remoteUrls) const = 0;
};

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_PROVIDER_H
