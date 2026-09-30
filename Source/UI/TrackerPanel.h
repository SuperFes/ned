//
// One issue-tracker panel (Editor/Tracker/Registry.h's Panel) hosted in
// LeftDock: its query's issues as a table (key, title, assignee, age) under
// collapsible status groups. A controller over TableView; everything that
// leaves the panel -- fetching, opening an issue, a URL, the kill ring --
// goes out through Set* callbacks, so the panel runs headless in tests and
// main.cpp owns the wiring.
//

#ifndef NED_UI_TRACKERPANEL_H
#define NED_UI_TRACKERPANEL_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Editor/Key.h"
#include "Editor/Tracker/IssueAction.h"
#include "Editor/Tracker/Provider.h"
#include "TableView.h"
#include "Theme.h"

namespace ned::ui {

class TrackerPanel {
  public:
    TrackerPanel(const Theme& theme, std::string panelName);

    [[nodiscard]] TableView&         Table();
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
    // An action key on an issue: 'b' makes a branch for it, 'c' comments,
    // 's' changes its status, 'a' assigns it, 'i' clocks in on it.
    void SetOnIssueAction(std::function<void(editor::tracker::IssueAction, const editor::tracker::Issue&)> handler);
    // 'w' (the key) and 'W' (the URL): text for the kill ring.
    void SetOnCopy(std::function<void(std::string)> handler);
    void SetOnMessage(std::function<void(std::string)> handler);
    void SetOnCancel(std::function<void()> handler);
    // Seconds since the epoch, for the Age column. Defaults to the system
    // clock.
    void SetClock(std::function<std::int64_t()> now);

  private:
    enum class State { NotFetched,
                       Loading,
                       Loaded,
                       Failed };

    const Theme& theme_;
    std::string  panelName_;
    TableView    table_;

    State                               state_ = State::NotFetched;
    std::vector<editor::tracker::Issue> issues_;
    std::string                         error_;
    std::function<std::int64_t()>       now_;

    std::function<void()>                              onFetchRequested_;
    std::function<void(const editor::tracker::Issue&)> onOpenIssue_;
    std::function<void(const std::string&)>            onOpenUrl_;
    std::function<void(editor::tracker::IssueAction, const editor::tracker::Issue&)> onIssueAction_;
    std::function<void(std::string)>                   onCopy_;
    std::function<void(std::string)>                   onMessage_;
    std::function<void()>                              onCancel_;

    void                                        Rebuild();
    void                                        Report(std::string message);
    [[nodiscard]] const editor::tracker::Issue* SelectedIssue() const;

    void HandleActivate(const std::string& key);
    void HandleKey(const editor::KeyChord& chord);
};

} // namespace ned::ui

#endif // NED_UI_TRACKERPANEL_H
