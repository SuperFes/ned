//
// One issue-tracker panel (Editor/Tracker/Registry.h's Panel) hosted in
// LeftDock: its query's issues grouped under collapsible status headers.
// DebugPanel's controller-over-TreeView shape; everything that leaves the
// panel -- fetching, opening an issue, a URL, the kill ring -- goes out
// through Set* callbacks, so the panel runs headless in tests and main.cpp
// owns the wiring.
//

#ifndef NED_UI_TRACKERPANEL_H
#define NED_UI_TRACKERPANEL_H

#include <cstddef>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "Editor/Key.h"
#include "Editor/Tracker/Provider.h"
#include "Theme.h"
#include "TreeView.h"

namespace ned::ui {

class TrackerPanel {
  public:
    TrackerPanel(const Theme& theme, std::string panelName);

    [[nodiscard]] TreeView&          Tree();
    [[nodiscard]] const std::string& Name() const {
        return panelName_;
    }

    // The first time the panel is on screen: fetch, unless that has already
    // happened. Fetching every panel at startup would spawn one command per
    // panel on every launch.
    void NotifyShown();
    // 'g': fetch now, whatever has happened before.
    void Refresh();

    // A fetch's outcome, delivered on the main thread.
    void ShowIssues(std::vector<editor::tracker::Issue> issues);
    void ShowError(std::string error);

    // Asked to fetch; the answer comes back through ShowIssues/ShowError.
    void SetOnFetchRequested(std::function<void()> handler);
    // Enter on an issue.
    void SetOnOpenIssue(std::function<void(const editor::tracker::Issue&)> handler);
    // 'o' on an issue that has a URL.
    void SetOnOpenUrl(std::function<void(const std::string&)> handler);
    // 'w' (the key) and 'W' (the URL): text for the kill ring.
    void SetOnCopy(std::function<void(std::string)> handler);
    void SetOnMessage(std::function<void(std::string)> handler);
    void SetOnCancel(std::function<void()> handler);

  private:
    enum class State { NotFetched,
                       Loading,
                       Loaded,
                       Failed };

    struct Row {
        enum class Kind { StatusHeader,
                          Issue,
                          Placeholder };
        Kind        kind = Kind::Placeholder;
        std::string status;
        std::size_t issue = 0; // into issues_
    };

    const Theme& theme_;
    std::string  panelName_;
    TreeView     tree_;

    State                               state_ = State::NotFetched;
    std::vector<editor::tracker::Issue> issues_;
    std::string                         error_;
    std::vector<Row>                    rows_;
    std::size_t                         selectedIndex_ = 0;
    // Collapsed statuses survive a refresh; a status seen for the first
    // time is open.
    std::set<std::string> collapsedStatuses_;

    std::function<void()>                              onFetchRequested_;
    std::function<void(const editor::tracker::Issue&)> onOpenIssue_;
    std::function<void(const std::string&)>            onOpenUrl_;
    std::function<void(std::string)>                   onCopy_;
    std::function<void(std::string)>                   onMessage_;
    std::function<void()>                              onCancel_;

    void                                        Rebuild();
    void                                        Report(std::string message);
    [[nodiscard]] const editor::tracker::Issue* IssueAt(std::size_t index) const;

    void HandleActivate(std::size_t index);
    void SetStatusCollapsed(std::size_t index, bool collapsed);
    void HandleKey(const editor::KeyChord& chord);
};

} // namespace ned::ui

#endif // NED_UI_TRACKERPANEL_H
