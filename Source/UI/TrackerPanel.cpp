#include "TrackerPanel.h"

#include <algorithm>
#include <ctime>
#include <iterator>
#include <utility>

#include "Editor/Timestamp.h"

namespace ned::ui {

namespace {

    std::string StatusLabel(const std::string& status) {
        return status.empty() ? "(no status)" : status;
    }

} // namespace

TrackerPanel::TrackerPanel(const Theme& theme, std::string panelName) : theme_(theme), panelName_(std::move(panelName)), table_(theme),
                                                                        now_([] { return static_cast<std::int64_t>(std::time(nullptr)); }) {
    table_.SetOnActivate([this](const std::string& key) { HandleActivate(key); });
    table_.SetOnKey([this](const editor::KeyChord& chord) { HandleKey(chord); });
    table_.SetOnCancel([this] {
        if (onCancel_) {
            onCancel_();
        }
    });
    Rebuild();
}

TableView& TrackerPanel::Table() {
    return table_;
}

void TrackerPanel::NotifyShown() {
    if (state_ == State::NotFetched) {
        Refresh();
    }
}

void TrackerPanel::Refresh() {
    if (state_ == State::Loading) {
        return;
    }
    if (!onFetchRequested_) {
        return;
    }
    state_ = State::Loading;
    Rebuild();
    onFetchRequested_();
}

void TrackerPanel::ShowIssues(std::vector<editor::tracker::Issue> issues) {
    issues_ = std::move(issues);
    error_.clear();
    state_ = State::Loaded;
    Rebuild();
}

void TrackerPanel::ShowError(std::string error) {
    // The last good listing stays: a transient failure (offline, an expired
    // token) shouldn't blank what was already on screen.
    error_ = std::move(error);
    state_ = issues_.empty() ? State::Failed : State::Loaded;
    Report(error_);
    Rebuild();
}

void TrackerPanel::SetOnFetchRequested(std::function<void()> handler) {
    onFetchRequested_ = std::move(handler);
}

void TrackerPanel::SetOnOpenIssue(std::function<void(const editor::tracker::Issue&)> handler) {
    onOpenIssue_ = std::move(handler);
}

void TrackerPanel::SetOnOpenUrl(std::function<void(const std::string&)> handler) {
    onOpenUrl_ = std::move(handler);
}

void TrackerPanel::SetOnIssueAction(std::function<void(editor::tracker::IssueAction, const editor::tracker::Issue&)> handler) {
    onIssueAction_ = std::move(handler);
}

void TrackerPanel::SetOnCopy(std::function<void(std::string)> handler) {
    onCopy_ = std::move(handler);
}

void TrackerPanel::SetOnMessage(std::function<void(std::string)> handler) {
    onMessage_ = std::move(handler);
}

void TrackerPanel::SetOnCancel(std::function<void()> handler) {
    onCancel_ = std::move(handler);
}

void TrackerPanel::SetClock(std::function<std::int64_t()> now) {
    now_ = std::move(now);
    Rebuild();
}

void TrackerPanel::Report(std::string message) {
    if (onMessage_) {
        onMessage_(std::move(message));
    }
}

const editor::tracker::Issue* TrackerPanel::SelectedIssue() const {
    const std::optional<std::string> key = table_.SelectedRowId();
    if (!key) {
        return nullptr;
    }
    const auto found = std::ranges::find(issues_, *key, &editor::tracker::Issue::key);
    return found == issues_.end() ? nullptr : &*found;
}

void TrackerPanel::Rebuild() {
    table::Model model;
    model.title   = panelName_;
    model.loading = state_ == State::Loading;
    model.columns = {
        table::Column{.id = "key", .header = "Key", .maxWidth = 16},
        table::Column{.id = "title", .header = "Title", .width = table::Column::Width::Flex, .minWidth = 8},
        table::Column{.id = "assignee", .header = "Assignee", .maxWidth = 16, .dropPriority = 2},
        table::Column{.id              = "age",
                      .header          = "Age",
                      .align           = table::Align::Right,
                      .dropPriority    = 1,
                      .descendingFirst = true},
    };

    if (state_ == State::NotFetched) {
        model.placeholder = "Press g to load";
    }
    else if (state_ == State::Loading && issues_.empty()) {
        model.placeholder = "Loading…";
    }
    else if (state_ == State::Failed) {
        model.placeholder = "Fetch failed -- g to retry";
    }
    else {
        model.placeholder = "(no issues)";
    }

    // Statuses in order of first appearance: the tracker's own sort (most
    // recently updated, usually) decides which group comes first.
    const std::int64_t now = now_();
    for (const editor::tracker::Issue& issue : issues_) {
        auto group = std::ranges::find(model.groups, "status:" + issue.status, &table::Group::id);
        if (group == model.groups.end()) {
            model.groups.push_back(table::Group{.id = "status:" + issue.status, .label = StatusLabel(issue.status)});
            group = std::prev(model.groups.end());
        }
        // Sorted by the time itself, not its label: newest is the largest.
        table::Cell age{.foreground = theme_.indentGuideForeground};
        if (const std::optional<std::int64_t> updated = editor::ParseIso8601(issue.updated)) {
            age.text       = editor::CompactAge(now - *updated);
            age.sortNumber = *updated;
        }
        group->rows.push_back(table::Row{.id    = issue.key,
                                         .cells = {table::Cell{.text = issue.key},
                                                   table::Cell{.text = issue.title},
                                                   table::Cell{.text = issue.assignee, .foreground = theme_.indentGuideForeground},
                                                   std::move(age)}});
    }
    for (table::Group& group : model.groups) {
        group.right = std::to_string(group.rows.size());
    }
    table_.SetModel(std::move(model));
}

void TrackerPanel::HandleActivate(const std::string& key) {
    if (key.empty()) {
        if (state_ == State::NotFetched || state_ == State::Failed) {
            Refresh();
        }
        return;
    }
    const auto found = std::ranges::find(issues_, key, &editor::tracker::Issue::key);
    if (found != issues_.end() && onOpenIssue_) {
        onOpenIssue_(*found);
    }
}

void TrackerPanel::HandleKey(const editor::KeyChord& chord) {
    if (chord.Control || chord.Meta) {
        return;
    }
    const editor::tracker::Issue* const issue = SelectedIssue();
    switch (chord.Codepoint) {
        case U'g':
            Refresh();
            return;
        case U'o':
            if (issue == nullptr || issue->url.empty()) {
                Report("No URL for this row");
            }
            else if (onOpenUrl_) {
                onOpenUrl_(issue->url);
            }
            return;
        case U'b':
        case U'c':
        case U's':
        case U'a':
        case U'i':
            if (issue == nullptr) {
                Report("Not on an issue");
            }
            else if (onIssueAction_) {
                onIssueAction_(chord.Codepoint == U'b'   ? editor::tracker::IssueAction::CreateBranch
                               : chord.Codepoint == U'c' ? editor::tracker::IssueAction::Comment
                               : chord.Codepoint == U's' ? editor::tracker::IssueAction::Transition
                               : chord.Codepoint == U'a' ? editor::tracker::IssueAction::Assign
                                                         : editor::tracker::IssueAction::ClockIn,
                               *issue);
            }
            return;
        case U'w':
        case U'W': {
            if (issue == nullptr) {
                Report("Not on an issue");
                return;
            }
            std::string text = chord.Codepoint == U'w' ? issue->key : issue->url;
            if (text.empty()) {
                Report("No URL for this row");
                return;
            }
            Report("Copied " + text);
            if (onCopy_) {
                onCopy_(std::move(text));
            }
            return;
        }
        default:
            return;
    }
}

} // namespace ned::ui
