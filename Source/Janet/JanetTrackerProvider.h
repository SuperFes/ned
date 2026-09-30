//
// Adapts a Janet tracker plugin onto editor::tracker::Provider. The plugin
// is one struct/table keyed by keyword:
//   :list-argv  (fn [connection query] argv)   connection is a struct of
//               :name :provider :url :email -- never a token
//   :parse-list (fn [output] issues)           each issue a table of :key
//               :title :status :assignee :labels :url :updated
//   :view-argv  (fn [connection key] argv)     optional, with :parse-view
//   :parse-view (fn [output] issue)            an issue table plus :body and
//               :comments, each a table of :author :created :body
//   :detect     (fn [remote-urls] found)       optional; each found entry a
//               connection table (:name :url :email) whose :panels are
//               tables of :name :query :glyph
// and, optionally, the actions (Provider.h's Capability), each group all
// or nothing:
//   :transitions-argv (fn [connection key]) + :parse-transitions (fn [output])
//               -> :id :name tables, + :transition-argv (fn [connection key id])
//   :assignees-argv / :parse-assignees / :assign-argv, the same shape
//   :comment-argv (fn [connection key markdown-body])
//   :worklog-argv (fn [connection key started-epoch-seconds seconds])
//   :project-keys-argv (fn [connection]) + :parse-project-keys (fn [output])
//               -> strings
//   :mine-query (fn [connection]) -> a :list-argv query, or just the string
// :numeric-keys true says the tracker's keys are "#42", not PROJ-42.
// An argv callback returns an argv array or a table of :argv plus
// :curl-credentials and :input (Provider.h's CommandSpec).
// Callbacks are bound under generated names and invoked via janet_dostring,
// the same way JanetVcsProvider calls its plugin (see its header for why
// not janet_pcall), so every call must run on the main thread.
//

#ifndef NED_JANET_JANETTRACKERPROVIDER_H
#define NED_JANET_JANETTRACKERPROVIDER_H

#include <janet.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Editor/Tracker/Provider.h"

namespace ned::janet {

class JanetTrackerProvider : public editor::tracker::Provider {
  public:
    // Throws std::runtime_error unless callbacks is a struct/table carrying
    // both :list-argv and :parse-list.
    JanetTrackerProvider(JanetTable* env, std::string name, Janet callbacks);

    [[nodiscard]] editor::tracker::CommandSpec        ListArgv(const editor::tracker::Connection& connection,
                                                               const std::string&                 query) const override;
    [[nodiscard]] std::vector<editor::tracker::Issue> ParseList(const std::string& output) const override;
    [[nodiscard]] std::optional<editor::tracker::CommandSpec> ViewArgv(const editor::tracker::Connection& connection,
                                                                       const std::string&                 key) const override;
    [[nodiscard]] editor::tracker::IssueDetail                ParseView(const std::string& output) const override;
    [[nodiscard]] std::vector<editor::tracker::Detected>      Detect(const std::vector<std::string>& remoteUrls) const override;

    [[nodiscard]] bool Supports(editor::tracker::Capability capability) const override;
    [[nodiscard]] bool NumericKeys() const override {
        return numericKeys_;
    }

    [[nodiscard]] editor::tracker::CommandSpec         TransitionsArgv(const editor::tracker::Connection& connection,
                                                                       const std::string&                 key) const override;
    [[nodiscard]] std::vector<editor::tracker::Choice> ParseTransitions(const std::string& output) const override;
    [[nodiscard]] editor::tracker::CommandSpec         TransitionArgv(const editor::tracker::Connection& connection, const std::string& key,
                                                                      const std::string& transitionId) const override;
    [[nodiscard]] editor::tracker::CommandSpec         AssigneesArgv(const editor::tracker::Connection& connection,
                                                                     const std::string&                 key) const override;
    [[nodiscard]] std::vector<editor::tracker::Choice> ParseAssignees(const std::string& output) const override;
    [[nodiscard]] editor::tracker::CommandSpec         AssignArgv(const editor::tracker::Connection& connection, const std::string& key,
                                                                  const std::string& userId) const override;
    [[nodiscard]] editor::tracker::CommandSpec         CommentArgv(const editor::tracker::Connection& connection, const std::string& key,
                                                                   const std::string& body) const override;
    [[nodiscard]] editor::tracker::CommandSpec         WorklogArgv(const editor::tracker::Connection& connection, const std::string& key,
                                                                   const editor::tracker::Worklog& worklog) const override;
    [[nodiscard]] editor::tracker::CommandSpec         ProjectKeysArgv(const editor::tracker::Connection& connection) const override;
    [[nodiscard]] std::vector<std::string>             ParseProjectKeys(const std::string& output) const override;
    [[nodiscard]] std::string                          MineQuery(const editor::tracker::Connection& connection) const override;

  private:
    [[nodiscard]] Janet Call(const std::string& callback, std::initializer_list<Janet> args) const;

    JanetTable* env_;
    std::string name_;
    bool        hasView_   = false;
    bool        hasDetect_ = false;
    bool        numericKeys_ = false;

    std::set<editor::tracker::Capability> capabilities_;
};

} // namespace ned::janet

#endif // NED_JANET_JANETTRACKERPROVIDER_H
