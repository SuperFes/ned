//
// The tracker-neutral vocabulary an issue-tracker plugin implements. Same
// split as Vcs/Provider.h: a provider only builds argv and parses captured
// output, both on the main thread (a Janet-backed provider can't run
// anywhere else); Tracker/Runner.h spawns and waits in between.
//

#ifndef NED_EDITOR_TRACKER_PROVIDER_H
#define NED_EDITOR_TRACKER_PROVIDER_H

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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

// With curlCredentials, argv is a curl command that authenticates as the
// connection: the Runner runs its :token-command first and appends
// `-K <file>` naming a config that carries the email and token.
//
// With input, the Runner writes it to a private temporary file and puts
// that file's path wherever an argv element says {input-file} -- a comment
// body is too long, and too arbitrary, for an argv of its own.
struct CommandSpec {
    std::vector<std::string>   argv;
    bool                       curlCredentials = false;
    std::optional<std::string> input;
};

inline constexpr std::string_view kInputFilePlaceholder = "{input-file}";

// Something a provider offers to pick from: a status transition, an
// assignable user. id goes back to the provider; name is what's shown.
// email, for a user, is how the connection's own user is recognized.
struct Choice {
    std::string id;
    std::string name;
    std::string email;

    bool operator==(const Choice&) const = default;
};

// Time spent on an issue: started is seconds since the epoch.
struct Worklog {
    std::int64_t started = 0;
    std::int64_t seconds = 0;

    bool operator==(const Worklog&) const = default;
};

// What a provider may do beyond listing and viewing issues. Each has its
// own callbacks, called only when Supports says so.
enum class Capability {
    Transition,  // TransitionsArgv/ParseTransitions + TransitionArgv
    Assign,      // AssigneesArgv/ParseAssignees + AssignArgv
    Comment,     // CommentArgv
    Worklog,     // WorklogArgv
    ProjectKeys, // ProjectKeysArgv/ParseProjectKeys
    Mine,        // MineQuery
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

    [[nodiscard]] virtual bool Supports(Capability /*capability*/) const {
        return false;
    }

    // Whether issue keys are bare numbers written "#42" (GitHub) rather
    // than PROJ-42 (Jira).
    [[nodiscard]] virtual bool NumericKeys() const {
        return false;
    }

    // An action's command succeeds or fails by its exit status; its output
    // is only shown when it fails.
    [[nodiscard]] virtual CommandSpec TransitionsArgv(const Connection& /*connection*/, const std::string& /*key*/) const {
        throw Unsupported();
    }
    [[nodiscard]] virtual std::vector<Choice> ParseTransitions(const std::string& /*output*/) const {
        throw Unsupported();
    }
    [[nodiscard]] virtual CommandSpec TransitionArgv(const Connection& /*connection*/, const std::string& /*key*/,
                                                     const std::string& /*transitionId*/) const {
        throw Unsupported();
    }

    [[nodiscard]] virtual CommandSpec AssigneesArgv(const Connection& /*connection*/, const std::string& /*key*/) const {
        throw Unsupported();
    }
    [[nodiscard]] virtual std::vector<Choice> ParseAssignees(const std::string& /*output*/) const {
        throw Unsupported();
    }
    [[nodiscard]] virtual CommandSpec AssignArgv(const Connection& /*connection*/, const std::string& /*key*/,
                                                 const std::string& /*userId*/) const {
        throw Unsupported();
    }

    // body is Markdown; a tracker that wants something else converts it.
    [[nodiscard]] virtual CommandSpec CommentArgv(const Connection& /*connection*/, const std::string& /*key*/,
                                                  const std::string& /*body*/) const {
        throw Unsupported();
    }

    [[nodiscard]] virtual CommandSpec WorklogArgv(const Connection& /*connection*/, const std::string& /*key*/,
                                                  const Worklog& /*worklog*/) const {
        throw Unsupported();
    }

    // The prefixes the connection's issue keys start with ("DEV" for DEV-12).
    [[nodiscard]] virtual CommandSpec ProjectKeysArgv(const Connection& /*connection*/) const {
        throw Unsupported();
    }
    [[nodiscard]] virtual std::vector<std::string> ParseProjectKeys(const std::string& /*output*/) const {
        throw Unsupported();
    }

    // A ListArgv query for the issues that are the user's own: assigned to
    // or watched by them.
    [[nodiscard]] virtual std::string MineQuery(const Connection& /*connection*/) const {
        throw Unsupported();
    }

  private:
    [[nodiscard]] static std::logic_error Unsupported() {
        return std::logic_error("the tracker provider doesn't support this");
    }
};

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_PROVIDER_H
