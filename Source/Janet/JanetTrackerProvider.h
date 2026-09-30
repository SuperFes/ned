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
// Callbacks are bound under generated names and invoked via janet_dostring,
// the same way JanetVcsProvider calls its plugin (see its header for why
// not janet_pcall), so every call must run on the main thread.
//

#ifndef NED_JANET_JANETTRACKERPROVIDER_H
#define NED_JANET_JANETTRACKERPROVIDER_H

#include <janet.h>

#include <optional>
#include <string>

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

  private:
    [[nodiscard]] Janet Call(const std::string& callback, std::initializer_list<Janet> args) const;

    JanetTable* env_;
    std::string name_;
    bool        hasView_ = false;
};

} // namespace ned::janet

#endif // NED_JANET_JANETTRACKERPROVIDER_H
