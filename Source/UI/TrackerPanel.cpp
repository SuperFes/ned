#include "TrackerPanel.h"

#include <algorithm>
#include <utility>

namespace ned::ui {

namespace {

    std::string StatusLabel(const std::string& status) {
        return status.empty() ? "(no status)" : status;
    }

} // namespace

TrackerPanel::TrackerPanel(const Theme& theme, std::string panelName) : theme_(theme), panelName_(std::move(panelName)), tree_(theme) {
    tree_.SetOnSelectionChanged([this](std::size_t index) { selectedIndex_ = index; });
    tree_.SetOnActivate([this](std::size_t index) { HandleActivate(index); });
    tree_.SetOnToggleExpand([this](std::size_t index) { SetStatusCollapsed(index, false); });
    tree_.SetOnCollapseRequested([this](std::size_t index) { SetStatusCollapsed(index, true); });
    tree_.SetOnKey([this](const editor::KeyChord& chord) { HandleKey(chord); });
    tree_.SetOnCancel([this] {
        if (onCancel_) {
            onCancel_();
        }
    });
    Rebuild();
}

TreeView& TrackerPanel::Tree() {
    return tree_;
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

void TrackerPanel::Report(std::string message) {
    if (onMessage_) {
        onMessage_(std::move(message));
    }
}

const editor::tracker::Issue* TrackerPanel::IssueAt(std::size_t index) const {
    if (index >= rows_.size() || rows_[index].kind != Row::Kind::Issue) {
        return nullptr;
    }
    return &issues_[rows_[index].issue];
}

void TrackerPanel::Rebuild() {
    rows_.clear();
    TreeViewModel model;
    model.title = panelName_;

    const auto placeholder = [&](std::string text) {
        rows_.push_back(Row{.kind = Row::Kind::Placeholder});
        model.rows.push_back(TreeRow{.label = std::move(text), .labelForeground = theme_.indentGuideForeground, .hasChildren = false});
    };

    if (state_ == State::NotFetched) {
        placeholder("Press g to load");
    }
    else if (state_ == State::Loading && issues_.empty()) {
        placeholder("Loading…");
    }
    else if (state_ == State::Failed) {
        placeholder("Fetch failed -- g to retry");
    }
    else if (issues_.empty()) {
        placeholder("(no issues)");
    }

    // Statuses in order of first appearance: the tracker's own sort (most
    // recently updated, usually) decides which group comes first.
    std::vector<std::string> statuses;
    for (const editor::tracker::Issue& issue : issues_) {
        if (std::ranges::find(statuses, issue.status) == statuses.end()) {
            statuses.push_back(issue.status);
        }
    }
    for (const std::string& status : statuses) {
        const auto count = std::ranges::count(issues_, status, &editor::tracker::Issue::status);
        const bool open  = !collapsedStatuses_.contains(status);
        rows_.push_back(Row{.kind = Row::Kind::StatusHeader, .status = status});
        model.rows.push_back(TreeRow{.label           = StatusLabel(status),
                                     .right           = std::to_string(count),
                                     .rightForeground = theme_.indentGuideForeground,
                                     .hasChildren     = true,
                                     .expanded        = open,
                                     .loading         = state_ == State::Loading});
        if (!open) {
            continue;
        }
        for (std::size_t i = 0; i < issues_.size(); ++i) {
            if (issues_[i].status != status) {
                continue;
            }
            rows_.push_back(Row{.kind = Row::Kind::Issue, .status = status, .issue = i});
            model.rows.push_back(TreeRow{.label = issues_[i].key + "  " + issues_[i].title, .depth = 1, .hasChildren = false});
        }
    }

    if (!model.rows.empty()) {
        selectedIndex_      = std::min(selectedIndex_, model.rows.size() - 1);
        model.selectedIndex = selectedIndex_;
    }
    tree_.SetModel(std::move(model));
}

void TrackerPanel::HandleActivate(std::size_t index) {
    if (index >= rows_.size()) {
        return;
    }
    selectedIndex_ = index;
    const Row& row = rows_[index];
    switch (row.kind) {
        case Row::Kind::StatusHeader:
            SetStatusCollapsed(index, !collapsedStatuses_.contains(row.status));
            return;
        case Row::Kind::Issue:
            if (onOpenIssue_) {
                onOpenIssue_(issues_[row.issue]);
            }
            return;
        case Row::Kind::Placeholder:
            if (state_ == State::NotFetched || state_ == State::Failed) {
                Refresh();
            }
            return;
    }
}

void TrackerPanel::SetStatusCollapsed(std::size_t index, bool collapsed) {
    if (index >= rows_.size() || rows_[index].kind == Row::Kind::Placeholder) {
        return;
    }
    const std::string status = rows_[index].status;
    if (collapsed) {
        collapsedStatuses_.insert(status);
    }
    else {
        collapsedStatuses_.erase(status);
    }
    // Collapsing from inside a group lands on its header rather than on
    // whatever row slides up under the selection.
    Rebuild();
    const auto header = std::ranges::find_if(rows_, [&status](const Row& row) {
        return row.kind == Row::Kind::StatusHeader && row.status == status;
    });
    if (header != rows_.end()) {
        selectedIndex_ = static_cast<std::size_t>(header - rows_.begin());
        Rebuild();
    }
}

void TrackerPanel::HandleKey(const editor::KeyChord& chord) {
    if (chord.Control || chord.Meta) {
        return;
    }
    const std::size_t                   index = tree_.SelectedRow().value_or(selectedIndex_);
    const editor::tracker::Issue* const issue = IssueAt(index);
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
