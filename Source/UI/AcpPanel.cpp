#include "AcpPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>

#include "AcpPanel/ComposerLayout.h"
#include "Border.h"
#include "Editor/Acp/PanelConfig.h"
#include "Editor/FuzzyMatch.h"
#include "Editor/Image/Save.h"
#include "Editor/Link.h"
#include "Editor/Project/Root.h"
#include "Editor/Project/Tree.h"
#include "Editor/SyntaxTheme.h"
#include "KeyTranslation.h"
#include "Text/Base64.h"
#include "Text/DisplayWidth.h"
#include "Text/FileUri.h"
#include "Text/Utf8.h"

namespace ned::ui {

using acppanel::WordWrap;
using acppanel::WrappedRow;

namespace {

    constexpr std::chrono::milliseconds kDoubleClickWindow{400}; // ProjectSidebar/VcsPanel's own kDoubleClickWindow

    std::string HomeRelative(const std::filesystem::path& path) {
        const char*       home = std::getenv("HOME");
        const std::string text = path.string();
        if (home != nullptr && *home != '\0' && text.starts_with(std::string(home) + "/")) {
            return "~" + text.substr(std::strlen(home));
        }
        return text;
    }

    // Mirrors BufferView.cpp's own anonymous-namespace IsPlainCharacter --
    // not shared, it's a one-line predicate private to each consumer there
    // too.
    bool IsPlainCharacter(const editor::KeyChord& chord) {
        return !chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint != 0;
    }

    constexpr int      kMinWidthForCloseButton    = 8;
    constexpr int      kMinWidthForMinimizeButton = 12;   // needs room for both 3-glyph button groups plus a gap column
    constexpr int      kCloseOffset               = 4;    // column of '[' counted back from width, matches TerminalPanel's own offset
    constexpr int      kMinimizeOffset            = 8;    // column of '[' for the minimize button, one 3-glyph group + gap left of close
    constexpr char32_t kCloseIcon                 = U'×'; // safe: one whole encoded glyph placed in exactly one Cell, not byte-indexed
    // Matches TerminalPanel's own minimize glyph exactly (its kMinimizeIcon)
    // -- glyph-consistency follow-up: this panel used a plain ASCII "-"
    // originally, reported live as inconsistent with the terminal drawer's
    // own title-row buttons.
    constexpr char32_t kMinimizeIcon = U'▼';
    constexpr int      kMaxInputRows = 6; // cap how far the composer grows before it starts scrolling internally
    // @-file-mention autocomplete follow-up: an arbitrary small cap keeping
    // the suggestion list from crowding out the transcript beneath it on a
    // short panel -- ranked[0] is always the best fuzzy match regardless of
    // how many total candidates exist, so this only ever hides the weaker
    // tail of the ranking, never the top pick.
    constexpr std::size_t kMaxMentionChoices = 6;
    // Matches Editor/Backup.cpp's own LocalTimeLabel format exactly (not
    // shared -- that one's private to its .cpp) so a rewind checkpoint's
    // timestamp reads the same way a backup version's does elsewhere in
    // this editor.
    std::string LocalTimeLabel(std::chrono::system_clock::time_point timestamp) {
        const std::time_t seconds = std::chrono::system_clock::to_time_t(timestamp);
        std::tm           local{};
        localtime_r(&seconds, &local);
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                      local.tm_hour, local.tm_min, local.tm_sec);
        return buffer;
    }

    // "5m ago" / "3h ago" / "2d ago" from an ISO 8601 UTC timestamp
    // ("2026-09-29T15:05:44.175Z"), falling back to the date part once it's
    // older than a week, and to the raw text when it doesn't parse.
    std::string RelativeAge(const std::string& iso) {
        std::tm parsed{};
        if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &parsed.tm_year, &parsed.tm_mon, &parsed.tm_mday, &parsed.tm_hour, &parsed.tm_min,
                        &parsed.tm_sec) != 6) {
            return iso;
        }
        parsed.tm_year -= 1900;
        parsed.tm_mon -= 1;
        const std::time_t then    = timegm(&parsed);
        const std::time_t now     = std::time(nullptr);
        const long long   seconds = static_cast<long long>(now - then);
        if (seconds < 60) {
            return "just now";
        }
        if (seconds < 3600) {
            return std::to_string(seconds / 60) + "m ago";
        }
        if (seconds < 86400) {
            return std::to_string(seconds / 3600) + "h ago";
        }
        if (seconds < 7 * 86400) {
            return std::to_string(seconds / 86400) + "d ago";
        }
        return iso.substr(0, 10);
    }

    // Display columns, as PaintUtf8Row paints the text (Text/DisplayWidth.h)
    // -- the unit every column in this file is counted in.
    int ColumnCount(std::string_view text) {
        return text::StringColumns(text);
    }

    std::string StateLabel(const editor::acp::Manager* manager) {
        if (!manager) {
            return "inactive";
        }
        if (manager->LoginRequired()) {
            return "login required";
        }
        switch (manager->State()) {
            case editor::acp::Manager::SessionState::Starting:
                return "starting";
            case editor::acp::Manager::SessionState::Active:
                return "active";
            case editor::acp::Manager::SessionState::Inactive:
                return "inactive";
        }
        return "inactive";
    }

} // namespace

AcpPanel::AcpPanel(const Theme& theme) : theme_(theme), prompt_("Prompt: ") {
}

void AcpPanel::SetAcpManager(editor::acp::Manager* acpManager) {
    acpManager_ = acpManager;
}

void AcpPanel::SetActiveBufferProvider(std::function<ActiveBuffer&()> provider) {
    activeBufferProvider_ = std::move(provider);
}

void AcpPanel::SetLspManager(editor::lsp::Manager* lspManager) {
    lspManager_ = lspManager;
}

void AcpPanel::SetOnToggleRequest(std::function<void()> onToggle) {
    onToggleRequest_ = std::move(onToggle);
}

void AcpPanel::SetDockHosted(bool dockHosted) {
    dockHosted_ = dockHosted;
}

std::string AcpPanel::TitleText() const {
    const std::string agentName = acpManager_ && !acpManager_->AgentName().empty() ? acpManager_->AgentName() : std::string("ACP agent");
    std::string       title     = (attention_ ? "● " : "") + agentName;
    if (acpManager_ && !acpManager_->SessionTitle().empty()) {
        // Bounded: this is also the dock's tab label.
        constexpr std::size_t kMaxTitleBytes = 40;
        const std::string&    sessionTitle   = acpManager_->SessionTitle();
        title += " · " + (sessionTitle.size() > kMaxTitleBytes
                              ? sessionTitle.substr(0, text::SnapDownToCodepointBoundary(sessionTitle, kMaxTitleBytes)) + "…"
                              : sessionTitle);
    }
    title += " [" + StateLabel(acpManager_) + "]";
    if (!scroll_.Following()) {
        title += " (scrollback)"; // TerminalPanel/DebugConsolePanel's own convention
    }
    return title;
}

void AcpPanel::StopAwaitingSessions() {
    if (awaitingSessions_ && acpManager_) {
        acpManager_->CancelListSessions();
    }
    awaitingSessions_ = false;
}

void AcpPanel::StartLogin(const editor::acp::Manager::AuthMethod& method) {
    if (method.type != "terminal") {
        ShowNotice(acpManager_->Authenticate(method.id));
        return;
    }
    std::optional<std::vector<std::string>> argv = acpManager_->LoginCommand(method);
    if (!argv || !onTerminalLogin_) {
        ShowNotice("Can't run this agent's login here");
        return;
    }
    onTerminalLogin_(std::move(*argv), method.env, method.name, [this](bool succeeded) {
        acpManager_->FinishLogin(succeeded);
        if (succeeded && onRefocusRequest_) {
            onRefocusRequest_();
        }
    });
}

void AcpPanel::SetOnTerminalLogin(TerminalLoginFn onTerminalLogin) {
    onTerminalLogin_ = std::move(onTerminalLogin);
}

void AcpPanel::SyncSessionView() {
    if (!acpManager_) {
        return;
    }
    const std::uint64_t key = acpManager_->CurrentSessionKey();
    if (key == viewedSession_) {
        return;
    }
    if (viewedSession_ != 0) {
        sessionViews_[viewedSession_] = SessionView{.scroll         = scroll_,
                                                    .toggledEntries = std::move(toggledEntries_),
                                                    .draft          = prompt_.Text(),
                                                    .pendingImages  = std::move(pendingImages_)};
    }
    viewedSession_ = key;
    SessionView view;
    if (const auto it = sessionViews_.find(key); it != sessionViews_.end()) {
        view = std::move(it->second);
        sessionViews_.erase(it);
    }
    std::erase_if(sessionViews_, [tabs = acpManager_->SessionTabs()](const auto& entry) {
        return std::none_of(tabs.begin(), tabs.end(), [&entry](const editor::acp::Manager::SessionTab& tab) { return tab.key == entry.first; });
    });
    scroll_         = view.scroll;
    toggledEntries_ = std::move(view.toggledEntries);
    pendingImages_  = std::move(view.pendingImages);
    prompt_.SetText(std::move(view.draft));
    historyIndex_.reset();
    historyDraft_.clear();
    mentionPickerOpen_ = false;
    StopAwaitingSessions();
    picker_.reset();
    pendingPicker_.reset();
    ++viewGeneration_;
}

void AcpPanel::PaintSessionStrip(Canvas& canvas, int y, int width) {
    stripRow_        = y;
    stripItems_      = acppanel::LayoutSessionStrip(acpManager_->SessionTabs(), width);
    const Brush base = BrushForStyle(DisplayStyle::Dim);
    for (int x = 0; x < width; ++x) {
        Cell& cell     = canvas[{.x = x, .y = y}];
        cell.character = " ";
        base.ApplyTo(cell);
    }
    for (const acppanel::SessionStripItem& item : stripItems_) {
        if (item.x >= width) {
            break;
        }
        Brush brush = !item.live ? BrushForStyle(DisplayStyle::Dim) : BrushForStyle(DisplayStyle::Plain);
        if (item.current) {
            brush      = theme_.echoArea;
            brush.bold = true;
        }
        else if (item.attention) {
            brush.foreground = theme_.borderAccent.foreground;
        }
        PaintUtf8Row(canvas, item.x, y, item.text, brush, width - item.x);
    }
}

void AcpPanel::ClickSessionStrip(int x, bool right, Point anchor) {
    const acppanel::SessionStripItem* item = acppanel::SessionStripItemAt(stripItems_, x);
    if (right) {
        std::vector<MenuItem> items;
        if (item && (item->kind == acppanel::SessionStripItem::Kind::Tab || item->kind == acppanel::SessionStripItem::Kind::Close)) {
            acpManager_->SelectSession(item->key);
        }
        if (acpManager_->CanForkSessions()) {
            items.push_back({.label = "Fork", .action = [this] { OpenPicker(editor::acp::PanelPicker::Fork); }});
        }
        items.push_back({.label = "New Conversation", .action = [this] { ShowNotice(acpManager_->NewSession()); }});
        if (acpManager_->SessionTabs().size() > 1) {
            items.push_back({.label = "Close", .action = [this] { ShowNotice(acpManager_->CloseSession(acpManager_->CurrentSessionKey())); }});
        }
        if (onContextMenuRequest_) {
            onContextMenuRequest_("Conversation", std::move(items), anchor);
        }
        return;
    }
    if (!item) {
        return;
    }
    switch (item->kind) {
        case acppanel::SessionStripItem::Kind::Tab:
        case acppanel::SessionStripItem::Kind::More:
            acpManager_->SelectSession(item->key);
            break;
        case acppanel::SessionStripItem::Kind::Close:
            ShowNotice(acpManager_->CloseSession(item->key));
            break;
        case acppanel::SessionStripItem::Kind::New:
            ShowNotice(acpManager_->NewSession());
            break;
    }
}

void AcpPanel::OpenPicker(editor::acp::PanelPicker picker) {
    if (!acpManager_) {
        return;
    }
    SyncSessionView();
    mentionPickerOpen_ = false;
    StopAwaitingSessions();
    using Manager = editor::acp::Manager;
    switch (picker) {
        case editor::acp::PanelPicker::Rewind: {
            // Newest first: 1 is always the most recent turn.
            std::vector<acppanel::ChoiceItem> items;
            const std::size_t                 count = acpManager_->CheckpointCount();
            for (std::size_t offset = 0; offset < count; ++offset) {
                const Manager::Checkpoint& checkpoint = acpManager_->CheckpointAt(count - 1 - offset);
                items.push_back({.label = checkpoint.promptPreview, .detail = LocalTimeLabel(checkpoint.timestamp)});
            }
            picker_.emplace(count == 0 ? "No turns recorded yet" : "Rewind to before which turn?", std::move(items),
                            [this, count](std::size_t offset) { acpManager_->RewindTo(count - 1 - offset); });
            break;
        }
        case editor::acp::PanelPicker::Mode: {
            if (const Manager::ConfigOption* option = acpManager_->ConfigOptionByCategory("mode")) {
                picker_.emplace(ConfigValuePicker(option, ""));
                break;
            }
            std::vector<acppanel::ChoiceItem> items;
            std::vector<std::string>          ids;
            for (const Manager::SessionMode& mode : acpManager_->Modes()) {
                items.push_back({.label = mode.name, .detail = mode.description, .current = mode.id == acpManager_->CurrentModeId()});
                ids.push_back(mode.id);
            }
            picker_.emplace(items.empty() ? "This agent offers no modes" : "Mode", std::move(items),
                            [this, ids](std::size_t index) { acpManager_->SetMode(ids[index]); });
            break;
        }
        case editor::acp::PanelPicker::Model:
            picker_.emplace(ConfigValuePicker(acpManager_->ConfigOptionByCategory("model"), "This agent offers no model choice"));
            break;
        case editor::acp::PanelPicker::Options: {
            std::vector<acppanel::ChoiceItem> items;
            std::vector<std::string>          ids;
            for (const Manager::ConfigOption& option : acpManager_->ConfigOptions()) {
                std::string current = option.currentValue;
                for (const Manager::ConfigChoice& choice : option.choices) {
                    if (choice.value == option.currentValue) {
                        current = choice.name;
                    }
                }
                items.push_back({.label = option.name, .detail = current});
                ids.push_back(option.id);
            }
            picker_.emplace(items.empty() ? "This agent offers no settings" : "Setting", std::move(items), [this, ids](std::size_t index) {
                const auto& options = acpManager_->ConfigOptions();
                const auto  it      = std::find_if(options.begin(), options.end(), [&](const Manager::ConfigOption& o) { return o.id == ids[index]; });
                pendingPicker_.emplace(ConfigValuePicker(it == options.end() ? nullptr : &*it, "That setting is gone"));
            });
            break;
        }
        case editor::acp::PanelPicker::Sessions: {
            picker_.emplace("Loading sessions…", std::vector<acppanel::ChoiceItem>{}, nullptr);
            const std::size_t request = ++sessionsRequest_;
            awaitingSessions_         = true;
            acpManager_->ListSessions([this, request](std::vector<Manager::SessionSummary> sessions, std::string error) {
                if (!awaitingSessions_ || request != sessionsRequest_) {
                    return; // dismissed, or another picker replaced it
                }
                awaitingSessions_ = false;
                if (!error.empty()) {
                    picker_.emplace(error, std::vector<acppanel::ChoiceItem>{}, nullptr);
                    return;
                }
                std::vector<acppanel::ChoiceItem> items;
                for (const Manager::SessionSummary& session : sessions) {
                    items.push_back({.label   = session.title.empty() ? "(untitled)" : session.title,
                                     .detail  = RelativeAge(session.updatedAt),
                                     .current = session.sessionId == acpManager_->SessionId()});
                }
                const bool deletable = acpManager_->CanDeleteSessions() && !items.empty();
                picker_.emplace(items.empty() ? "No earlier sessions" : deletable ? "Resume which session? (Del deletes)"
                                                                                  : "Resume which session?",
                                std::move(items), [this, sessions](std::size_t index) {
                                    acpManager_->LoadSession(sessions[index].sessionId, sessions[index].title);
                                    scroll_.FollowTail();
                                });
                if (deletable) {
                    picker_->SetOnDelete([this, sessions](std::size_t index) {
                        if (sessions[index].sessionId == acpManager_->SessionId()) {
                            ShowNotice("That's the session you're in");
                            return false;
                        }
                        const std::string title = sessions[index].title.empty() ? std::string("(untitled)") : sessions[index].title;
                        acpManager_->DeleteSession(sessions[index].sessionId, [this, title](const std::string& error) {
                            ShowNotice(error.empty() ? "Deleted " + title : error);
                        });
                        return true;
                    });
                }
            });
            break;
        }
        case editor::acp::PanelPicker::Login: {
            std::vector<acppanel::ChoiceItem>       items;
            const std::vector<Manager::AuthMethod>& methods = acpManager_->AuthMethods();
            for (const Manager::AuthMethod& method : methods) {
                items.push_back({.label = method.name, .detail = method.description});
            }
            picker_.emplace(items.empty() ? "This agent offers no way to log in from here" : "Log in how?", std::move(items),
                            [this, methods](std::size_t index) { StartLogin(methods[index]); });
            break;
        }
        case editor::acp::PanelPicker::Review: {
            // Newest first, and only turns that changed something.
            std::vector<acppanel::ChoiceItem> items;
            std::vector<std::size_t>          turns;
            for (std::size_t index = acpManager_->CheckpointCount(); index-- > 0;) {
                const Manager::Checkpoint& checkpoint = acpManager_->CheckpointAt(index);
                const std::size_t          changed    = static_cast<std::size_t>(
                    std::count_if(checkpoint.files.begin(), checkpoint.files.end(),
                                  [](const editor::acp::TurnFile& file) { return file.after && *file.after != file.before; }));
                if (changed == 0) {
                    continue;
                }
                items.push_back({.label  = checkpoint.promptPreview,
                                 .detail = std::to_string(changed) + (changed == 1 ? " file · " : " files · ") + LocalTimeLabel(checkpoint.timestamp)});
                turns.push_back(index);
            }
            picker_.emplace(items.empty() ? "No turn has changed files yet" : "Review which turn's changes?", std::move(items),
                            [this, turns](std::size_t choice) {
                                const Manager::Checkpoint& checkpoint = acpManager_->CheckpointAt(turns[choice]);
                                if (onReviewRequest_) {
                                    onReviewRequest_(checkpoint.promptPreview, checkpoint.files);
                                }
                            });
            break;
        }
        case editor::acp::PanelPicker::Copy: {
            constexpr std::size_t                kMaxReplies = 10;
            std::vector<acppanel::CopyCandidate> candidates  = acppanel::CopyCandidates(acpManager_->Transcript(), kMaxReplies);
            std::vector<acppanel::ChoiceItem>    items;
            for (const acppanel::CopyCandidate& candidate : candidates) {
                items.push_back({.label = candidate.label, .detail = candidate.detail});
            }
            picker_.emplace(items.empty() ? "Nothing to copy yet" : "Copy what?", std::move(items),
                            [this, candidates = std::move(candidates)](std::size_t index) { Copy(candidates[index].text); });
            break;
        }
        case editor::acp::PanelPicker::Fork: {
            const std::vector<Manager::ForkPoint> points = acpManager_->ForkPoints();
            // Runs inside the picker's own key handling, so it leaves the
            // view switch to the next SyncSessionView.
            auto fork = [this](const std::optional<Manager::ForkPoint>& point) { ShowNotice(acpManager_->ForkSession(point)); };
            if (points.empty()) {
                picker_.reset();
                fork(std::nullopt);
                break;
            }
            std::vector<acppanel::ChoiceItem> items{{.label = "Now", .detail = "the whole conversation"}};
            for (const Manager::ForkPoint& point : points) {
                items.push_back({.label = point.preview, .detail = "after its reply"});
            }
            picker_.emplace("Fork from where?", std::move(items), [points, fork](std::size_t index) {
                fork(index == 0 ? std::nullopt : std::optional<Manager::ForkPoint>(points[index - 1]));
            });
            break;
        }
        case editor::acp::PanelPicker::SaveImage: {
            std::vector<acppanel::ImageCandidate> candidates = acppanel::ImageCandidates(acpManager_->Transcript());
            std::vector<acppanel::ChoiceItem>     items;
            for (const acppanel::ImageCandidate& candidate : candidates) {
                items.push_back({.label = candidate.label, .detail = candidate.detail});
            }
            picker_.emplace(items.empty() ? "No pictures yet" : "Save which picture?", std::move(items),
                            [this, candidates = std::move(candidates)](std::size_t index) { SaveImage(candidates[index].image); });
            break;
        }
    }
}

acppanel::ChoicePicker AcpPanel::ConfigValuePicker(const editor::acp::Manager::ConfigOption* option, std::string_view missing) {
    if (!option) {
        return acppanel::ChoicePicker(std::string(missing), {}, nullptr);
    }
    std::vector<acppanel::ChoiceItem> items;
    std::vector<std::string>          values;
    if (option->type == "boolean") {
        for (const char* value : {"true", "false"}) {
            items.push_back({.label = value == std::string_view("true") ? "On" : "Off", .current = option->currentValue == value});
            values.emplace_back(value);
        }
    }
    else {
        for (const editor::acp::Manager::ConfigChoice& choice : option->choices) {
            items.push_back({.label = choice.name, .detail = choice.description, .current = choice.value == option->currentValue});
            values.push_back(choice.value);
        }
    }
    return acppanel::ChoicePicker(option->name, std::move(items), [this, id = option->id, values](std::size_t index) {
        acpManager_->SetConfigOption(id, values[index]);
    });
}

std::pair<std::string, std::string> AcpPanel::StatusLine() const {
    const bool noticeShowing = !notice_.empty() && std::chrono::steady_clock::now() < noticeUntil_;
    if (!acpManager_ || acpManager_->State() == editor::acp::Manager::SessionState::Inactive) {
        return noticeShowing ? std::pair<std::string, std::string>{std::string(), notice_} : std::pair<std::string, std::string>{};
    }
    using Manager   = editor::acp::Manager;
    auto choiceName = [](const Manager::ConfigOption* option) -> std::string {
        if (!option) {
            return {};
        }
        for (const Manager::ConfigChoice& choice : option->choices) {
            if (choice.value == option->currentValue) {
                return choice.name;
            }
        }
        return option->currentValue;
    };

    std::string left;
    auto        add = [](std::string& to, const std::string& part) {
        if (!part.empty()) {
            to += (to.empty() ? "" : " · ") + part;
        }
    };
    std::string mode = choiceName(acpManager_->ConfigOptionByCategory("mode"));
    if (mode.empty()) {
        for (const Manager::SessionMode& candidate : acpManager_->Modes()) {
            if (candidate.id == acpManager_->CurrentModeId()) {
                mode = candidate.name;
            }
        }
    }
    add(left, mode.empty() ? std::string() : "⏵ " + mode);
    add(left, choiceName(acpManager_->ConfigOptionByCategory("model")));
    add(left, choiceName(acpManager_->ConfigOptionByCategory("thought_level")));
    if (editor::acp::GetAcpFollowAgent()) {
        add(left, "⇢ follow");
    }

    std::string right;
    if (noticeShowing) {
        add(right, notice_);
    }
    if (const auto started = acpManager_->PromptStartedAt()) {
        static constexpr std::string_view kFrames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
        const auto                        elapsed   = std::chrono::steady_clock::now() - *started;
        const auto                        frame     = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 100 % 10;
        add(right, std::string(kFrames[frame]) + " " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()) + "s");
    }
    if (const auto& usage = acpManager_->SessionUsage()) {
        if (usage->size > 0) {
            add(right, "ctx " + std::to_string(usage->used * 100 / usage->size) + "%");
        }
        if (usage->costAmount) {
            char cost[32];
            std::snprintf(cost, sizeof(cost), "%s%.2f", usage->costCurrency == "USD" ? "$" : (usage->costCurrency + " ").c_str(), *usage->costAmount);
            add(right, cost);
        }
    }
    return {left, right};
}

bool AcpPanel::Collapsed() const {
    return collapsed_;
}

void AcpPanel::SetCollapsed(bool collapsed) {
    if (collapsed_ == collapsed) {
        return;
    }
    collapsed_ = collapsed;
    if (onCollapseChanged_) {
        onCollapseChanged_();
    }
}

void AcpPanel::ToggleCollapsed() {
    SetCollapsed(!collapsed_);
}

void AcpPanel::SetOnCollapseChanged(std::function<void()> onCollapseChanged) {
    onCollapseChanged_ = std::move(onCollapseChanged);
}

void AcpPanel::SetTerminalSize(Size size) {
    terminalSize_ = size;
}

// ACP chat-feel round 2: shell-style prompt history, re-derived from the
// transcript's own Kind::UserMessage entries -- see this method pair's own
// doc comment in AcpPanel.h for why nothing is duplicated into a separate
// list here.
void AcpPanel::HistoryPrevious() {
    if (!acpManager_) {
        return;
    }
    std::vector<std::string> history;
    for (const auto& entry : acpManager_->Transcript()) {
        if (entry.kind == editor::acp::Manager::TranscriptEntry::Kind::UserMessage) {
            history.push_back(entry.text);
        }
    }
    if (history.empty()) {
        return;
    }
    if (!historyIndex_) {
        historyDraft_ = prompt_.Text();
        historyIndex_ = history.size() - 1;
    }
    else if (*historyIndex_ > 0) {
        --*historyIndex_;
    }
    prompt_.SetText(history[*historyIndex_]);
}

void AcpPanel::HistoryNext() {
    if (!historyIndex_) {
        return;
    }
    std::vector<std::string> history;
    if (acpManager_) {
        for (const auto& entry : acpManager_->Transcript()) {
            if (entry.kind == editor::acp::Manager::TranscriptEntry::Kind::UserMessage) {
                history.push_back(entry.text);
            }
        }
    }
    if (*historyIndex_ + 1 < history.size()) {
        ++*historyIndex_;
        prompt_.SetText(history[*historyIndex_]);
    }
    else {
        historyIndex_.reset();
        prompt_.SetText(historyDraft_);
    }
}

Brush AcpPanel::BrushForStyle(DisplayStyle style) const {
    switch (style) {
        case DisplayStyle::Dim:
            return Brush{.background = theme_.background, .foreground = theme_.commentForeground};
        case DisplayStyle::Warning:
            return Brush{.background = theme_.background, .foreground = theme_.diagnosticWarning};
        case DisplayStyle::Error:
            return Brush{.background = theme_.background, .foreground = theme_.diagnosticError};
        case DisplayStyle::Accent:
            return Brush{.background = theme_.background, .foreground = theme_.borderAccent.foreground};
        case DisplayStyle::Hint:
            return Brush{.background = theme_.background, .foreground = theme_.diagnosticHint};
        case DisplayStyle::DiffAdded:
            return Brush{.background = theme_.diffAddedBackground, .foreground = theme_.defaultForeground};
        case DisplayStyle::DiffRemoved:
            return Brush{.background = theme_.diffRemovedBackground, .foreground = theme_.defaultForeground};
        case DisplayStyle::Plain:
            break;
    }
    return Brush{.background = theme_.background, .foreground = theme_.defaultForeground};
}

Brush AcpPanel::SyntaxBrush(editor::SyntaxClass cls, editor::CaptureId captureId) const {
    // BufferView::ResolvedBrush's cache: flushed on any syntax-style change or theme switch.
    const std::size_t generation = editor::SyntaxThemeGeneration();
    if (generation != syntaxBrushesGeneration_ || theme_.name != syntaxBrushesTheme_) {
        syntaxBrushes_.clear();
        syntaxBrushesGeneration_ = generation;
        syntaxBrushesTheme_      = theme_.name;
    }
    const std::uint32_t key = (static_cast<std::uint32_t>(cls) << 16) | captureId;
    if (const auto it = syntaxBrushes_.find(key); it != syntaxBrushes_.end()) {
        return it->second;
    }
    return syntaxBrushes_.emplace(key, theme_.BrushFor(cls, captureId)).first->second;
}

std::vector<editor::HighlightSpan> AcpPanel::HighlightCode(std::string_view language, std::string_view code) {
    std::string key = std::string(language) + '\0' + std::string(code);
    if (const auto it = codeHighlights_.find(key); it != codeHighlights_.end()) {
        return it->second;
    }
    std::vector<editor::HighlightSpan> spans;
    if (const editor::HighlightFunction* highlight = editor::ResolveEmbeddedLanguageHighlight(language, codeLanguages_)) {
        try {
            spans = (*highlight)(code, editor::HighlightWindow{});
        }
        catch (const std::exception&) {
            spans.clear(); // unhighlighted code still reads as code
        }
    }
    // Every partial version of a streaming block lands here once; start
    // over rather than keep them all.
    constexpr std::size_t kMaxHighlightedBlocks = 256;
    if (codeHighlights_.size() >= kMaxHighlightedBlocks) {
        codeHighlights_.clear();
    }
    codeHighlights_.emplace(std::move(key), spans);
    return spans;
}

void AcpPanel::SetOnCopy(std::function<void(const std::string&)> onCopy) {
    onCopy_ = std::move(onCopy);
}

void AcpPanel::Copy(const std::string& text) {
    if (!onCopy_) {
        return;
    }
    onCopy_(text);
    const std::size_t lines = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
    ShowNotice("copied " + std::to_string(lines) + (lines == 1 ? " line" : " lines"));
}

void AcpPanel::SetOnContextMenuRequest(std::function<void(std::string, std::vector<MenuItem>, Point)> onMenu) {
    onContextMenuRequest_ = std::move(onMenu);
}

void AcpPanel::SetImageDirectory(std::filesystem::path directory) {
    imageDirectory_ = std::move(directory);
}

void AcpPanel::SetImageCopier(std::function<bool(std::string_view, std::string_view)> copier) {
    imageCopier_ = std::move(copier);
}

void AcpPanel::SaveImage(const editor::acp::Manager::TranscriptImage& image) {
    const std::optional<std::string> bytes = text::Base64Decode(image.data);
    if (!bytes) {
        ShowNotice("couldn't read the picture");
        return;
    }
    const std::optional<std::filesystem::path> directory =
        imageDirectory_.empty() ? editor::image::DownloadDirectory() : std::optional<std::filesystem::path>(imageDirectory_);
    if (!directory) {
        ShowNotice("nowhere to save pictures: HOME isn't set");
        return;
    }
    const std::time_t now = std::time(nullptr);
    std::tm           local{};
    localtime_r(&now, &local);
    std::array<char, 32> stem{};
    std::strftime(stem.data(), stem.size(), "ned-image-%Y%m%d-%H%M%S", &local);
    const editor::image::WrittenFile written =
        editor::image::WriteNewFile(*directory, stem.data(), editor::image::ImageFileExtension(image.mimeType, *bytes), *bytes);
    ShowNotice(written.path.empty() ? written.error : "saved " + HomeRelative(written.path));
}

void AcpPanel::CopyImage(const editor::acp::Manager::TranscriptImage& image) {
    const std::optional<std::string> bytes = text::Base64Decode(image.data);
    if (!bytes) {
        ShowNotice("couldn't read the picture");
        return;
    }
    // A clipboard needs a type to offer it under; one the agent didn't give
    // is taken from the bytes.
    std::string mimeType = image.mimeType;
    if (mimeType.empty()) {
        const std::string extension = editor::image::ImageFileExtension("", *bytes);
        mimeType                    = extension == ".jpg" ? "image/jpeg" : "image/" + extension.substr(1);
    }
    const bool copied = imageCopier_ ? imageCopier_(mimeType, *bytes) : editor::CopyImageToSystemClipboard(mimeType, *bytes);
    ShowNotice(copied ? "copied the picture" : "no clipboard tool here can take a picture (wl-copy or xclip)");
}

void AcpPanel::SetUrlOpener(std::function<bool(const std::string&)> opener) {
    urlOpener_ = std::move(opener);
}

void AcpPanel::SyncElicitation() {
    const std::optional<editor::acp::Manager::Elicitation>* pending = acpManager_ ? &acpManager_->PendingElicitation() : nullptr;
    if (pending == nullptr || !pending->has_value()) {
        form_.reset();
        return;
    }
    const editor::acp::Manager::Elicitation& elicitation = **pending;
    if (form_ && formElicitationId_ == elicitation.id) {
        return;
    }
    formElicitationId_ = elicitation.id;
    form_.emplace(elicitation.mode == "url" ? acppanel::ElicitationForm::ForUrl(elicitation.message, elicitation.url)
                                            : acppanel::ElicitationForm(elicitation.message, elicitation.schema, elicitation.fieldOrder));
    picker_.reset();
    mentionPickerOpen_ = false;
}

void AcpPanel::SubmitElicitation() {
    if (!form_ || !acpManager_) {
        return;
    }
    if (!form_->Url().empty()) {
        const bool opened = urlOpener_ ? urlOpener_(form_->Url()) : editor::link::OpenUrl(form_->Url());
        if (!opened) {
            ShowNotice("couldn't open the link");
            return;
        }
        acpManager_->AnswerElicitation("accept");
        return;
    }
    std::string                            problem;
    const std::optional<editor::acp::Json> content = form_->Content(problem);
    if (!content) {
        ShowNotice(problem);
        return;
    }
    acpManager_->AnswerElicitation("accept", *content, form_->Summary());
}

void AcpPanel::SetOnReviewRequest(std::function<void(std::string, std::vector<editor::acp::TurnFile>)> onReview) {
    onReviewRequest_ = std::move(onReview);
}

void AcpPanel::SetOnForwardChord(std::function<bool(const editor::KeyChord&)> forward) {
    forwardChord_ = std::move(forward);
}

void AcpPanel::SetClipboardSource(std::function<std::optional<editor::ClipboardImage>()> image,
                                  std::function<std::optional<std::string>()>            text) {
    clipboardImage_ = std::move(image);
    clipboardText_  = std::move(text);
}

namespace {

    std::string ByteSizeLabel(std::size_t bytes) {
        char label[32];
        if (bytes >= 1024 * 1024) {
            std::snprintf(label, sizeof(label), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        }
        else {
            std::snprintf(label, sizeof(label), "%zu KB", (bytes + 1023) / 1024);
        }
        return label;
    }

} // namespace

void AcpPanel::PasteFromClipboard() {
    const std::optional<editor::ClipboardImage> image = clipboardImage_ ? clipboardImage_() : editor::PasteImageFromSystemClipboard();
    if (image) {
        // Anthropic's API refuses an image over 5 MB once encoded.
        constexpr std::size_t kMaxImageBytes = 3 * 1024 * 1024 + 768 * 1024;
        if (!acpManager_ || !acpManager_->SupportsImages()) {
            ShowNotice("this agent doesn't accept images");
        }
        else if (image->bytes.size() > kMaxImageBytes) {
            ShowNotice("image too large (" + ByteSizeLabel(image->bytes.size()) + ")");
        }
        else {
            const std::string subtype = image->mimeType.substr(image->mimeType.find('/') + 1);
            const std::string name    = "image " + std::to_string(++imagesPasted_) + " (" + subtype + ", " + ByteSizeLabel(image->bytes.size()) + ")";
            pendingImages_.push_back({.name = name, .mimeType = image->mimeType, .text = text::Base64Encode(image->bytes), .image = true});
            ShowNotice("attached " + name);
        }
        return;
    }
    const std::optional<std::string> pasted = clipboardText_ ? clipboardText_() : editor::PasteFromSystemClipboard();
    if (!pasted || pasted->empty()) {
        ShowNotice("nothing to paste");
        return;
    }
    for (std::size_t pos = 0; pos < pasted->size(); pos = text::NextCodepointBoundary(*pasted, pos)) {
        const char32_t codepoint = text::DecodeCodepointUtf8(*pasted, pos);
        if (codepoint != U'\r') {
            prompt_.InsertChar(codepoint);
        }
    }
    RefreshMentionState();
}

std::string AcpPanel::PendingImagesLine() const {
    std::string line;
    for (const editor::acp::Manager::PromptAttachment& image : pendingImages_) {
        line += (line.empty() ? "▣ " : " · ") + image.name;
    }
    return line;
}

void AcpPanel::ToggleFollowAgent() {
    const bool follow = !editor::acp::GetAcpFollowAgent();
    editor::acp::SetAcpFollowAgent(follow);
    ShowNotice(follow ? "following the agent" : "not following the agent");
}

void AcpPanel::ShowNotice(std::string notice) {
    notice_      = std::move(notice);
    noticeUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
}

void AcpPanel::PaintStyledRow(Canvas& canvas, int x, int y, std::string_view text, const std::vector<InlineSpan>& spans,
                              const Brush& baseBrush, int maxColumns) const {
    if (spans.empty() || maxColumns <= 0) {
        PaintUtf8Row(canvas, x, y, text, baseBrush, maxColumns);
        return;
    }
    // Column -> byte offset table, built once per row so each span's substr
    // is a direct lookup rather than a fresh scan from the row's own start.
    // Every column of a glyph maps to its first byte; spans begin and end on
    // glyph boundaries, so only those entries are ever cut at.
    std::vector<std::size_t> offsets;
    offsets.reserve(text.size() + 1);
    std::size_t pos = 0;
    while (pos < text.size()) {
        const text::Glyph glyph = text::GlyphAt(text, pos);
        for (int i = 0; i < std::max(1, text::GlyphColumns(glyph, static_cast<int>(offsets.size()), 1)); ++i) {
            offsets.push_back(pos);
        }
        pos += glyph.byteLength;
    }
    offsets.push_back(text.size());
    const int totalColumns = static_cast<int>(offsets.size()) - 1;

    int painted = 0; // columns painted so far, i.e. the x offset relative to `x`
    int col     = 0;
    for (const InlineSpan& span : spans) {
        if (painted >= maxColumns) {
            break;
        }
        const int plainEnd = std::min(span.startColumn, totalColumns);
        if (col < plainEnd) {
            const std::string_view segment = text.substr(offsets[col], offsets[plainEnd] - offsets[col]);
            painted += PaintUtf8Row(canvas, x + painted, y, segment, baseBrush, maxColumns - painted);
            col = plainEnd;
        }
        const int spanEnd = std::min(span.startColumn + span.columnCount, totalColumns);
        if (col < spanEnd && painted < maxColumns) {
            Brush spanBrush = baseBrush;
            if (span.syntaxClass) {
                spanBrush            = SyntaxBrush(*span.syntaxClass, span.captureId);
                spanBrush.background = baseBrush.background;
            }
            if (span.code) {
                spanBrush.background = theme_.documentHighlightBackground;
            }
            if (span.bold) {
                spanBrush.bold = true;
            }
            const std::string_view segment = text.substr(offsets[col], offsets[spanEnd] - offsets[col]);
            painted += PaintUtf8Row(canvas, x + painted, y, segment, spanBrush, maxColumns - painted);
            col = spanEnd;
        }
    }
    if (col < totalColumns && painted < maxColumns) {
        const std::string_view segment = text.substr(offsets[col], offsets[totalColumns] - offsets[col]);
        PaintUtf8Row(canvas, x + painted, y, segment, baseBrush, maxColumns - painted);
    }
}

const std::vector<acppanel::PhysicalLine>& AcpPanel::TranscriptRows(int width) {
    if (!acpManager_) {
        transcriptLines_.clear();
        transcriptRows_.clear();
        return transcriptRows_;
    }
    const auto& transcript = acpManager_->Transcript();
    if (transcript.size() < lastTranscriptSize_) {
        // A rewind dropped entries; a new entry landing on a freed index
        // must not inherit its predecessor's expansion.
        toggledEntries_.erase(toggledEntries_.lower_bound(transcript.size()), toggledEntries_.end());
        ++viewGeneration_;
    }
    lastTranscriptSize_ = transcript.size();

    const bool                         pending   = acpManager_->PendingPermissionPrompt().has_value();
    const editor::acp::ToolCallDisplay toolCalls = editor::acp::GetAcpToolCallDisplay();
    const editor::acp::ThinkingDisplay thinking  = editor::acp::GetAcpThinkingDisplay();
    // A running tool call's elapsed time changes the rows once a second.
    const auto now          = std::chrono::steady_clock::now();
    bool       toolsRunning = false;
    for (auto it = transcript.rbegin(); it != transcript.rend() && it->kind != editor::acp::Manager::TranscriptEntry::Kind::UserMessage; ++it) {
        toolsRunning = toolsRunning || (it->startedAt && !it->finishedAt);
    }
    const long long tick = toolsRunning ? std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() : -1;
    if (acpManager_->TranscriptGeneration() != transcriptRowsGeneration_ || width != transcriptRowsWidth_ ||
        pending != transcriptRowsPending_ || viewGeneration_ != transcriptRowsViewGeneration_ || toolCalls != transcriptRowsToolCalls_ ||
        thinking != transcriptRowsThinking_ || tick != transcriptRowsTick_) {
        transcriptLines_              = acppanel::FormatTranscript(transcript, acpManager_->PendingPermissionPrompt(),
                                                                   {.width        = width,
                                                                    .expanded     = [this](std::size_t index) { return EntryExpanded(index); },
                                                                    .hideThinking = thinking == editor::acp::ThinkingDisplay::Hidden,
                                                                    .projectRoot  = editor::ProjectRoot(),
                                                                    .highlightCode =
                                                                        [this](std::string_view language, std::string_view code) {
                                                               return HighlightCode(language, code);
                                                                        },
                                                                    .now      = now,
                                                                    .imageFit = [this](const editor::acp::Manager::TranscriptImage& image,
                                                                                       int                                          maxColumns) { return images_.Fit(image, maxColumns); }});
        transcriptRows_               = acppanel::WrapDisplayLines(transcriptLines_, width);
        transcriptRowsGeneration_     = acpManager_->TranscriptGeneration();
        transcriptRowsWidth_          = width;
        transcriptRowsPending_        = pending;
        transcriptRowsViewGeneration_ = viewGeneration_;
        transcriptRowsToolCalls_      = toolCalls;
        transcriptRowsThinking_       = thinking;
        transcriptRowsTick_           = tick;
    }
    return transcriptRows_;
}

void AcpPanel::PaintImages(Canvas& canvas, const std::vector<acppanel::PhysicalLine>& rows, int firstRow, int visibleRows, int titleRows,
                           int width) {
    const auto& transcript = acpManager_->Transcript();
    auto        imageOf    = [&transcript](const acppanel::DisplayLine& line) -> const editor::acp::Manager::TranscriptImage* {
        if (!line.image || line.entryIndex >= transcript.size()) {
            return nullptr;
        }
        const auto& images = transcript[line.entryIndex].images;
        const auto  found  = std::find_if(images.begin(), images.end(), [&line](const auto& image) { return image.id == line.image->id; });
        return found == images.end() ? nullptr : &*found;
    };
    for (int row = 0; row < visibleRows; ++row) {
        const std::size_t index = static_cast<std::size_t>(firstRow + row);
        if (index >= rows.size()) {
            break;
        }
        const acppanel::DisplayLine&                 line  = transcriptLines_[rows[index].lineIndex];
        const editor::acp::Manager::TranscriptImage* image = imageOf(line);
        if (image == nullptr) {
            continue;
        }
        const acppanel::ImageRow& place = *line.image;
        if (const std::vector<Cell>* cells = images_.Cells(*image, place.columns, place.rows, theme_.background)) {
            for (int x = 0; x < place.columns && place.column + x < width; ++x) {
                canvas[{.x = place.column + x, .y = titleRows + row}] =
                    (*cells)[static_cast<std::size_t>(place.row) * static_cast<std::size_t>(place.columns) + static_cast<std::size_t>(x)];
            }
        }
        // A picture whose first row this is, and whose every row is on
        // screen, also gets its real pixels where the terminal can show them.
        if (place.row != 0 || row + place.rows > visibleRows || place.column + place.columns > width) {
            continue;
        }
        const Box box{.x_min = Box_().x_min + place.column,
                      .x_max = Box_().x_min + place.column + place.columns - 1,
                      .y_min = Box_().y_min + titleRows + row,
                      .y_max = Box_().y_min + titleRows + row + place.rows - 1};
        if (!occlusionTest_ || !occlusionTest_(box)) {
            images_.ShowPixels(*image, box, theme_.background);
        }
    }
}

void AcpPanel::SetEventLoop(EventLoop* eventLoop) {
    images_.SetEventLoop(eventLoop);
}

void AcpPanel::SetOcclusionTest(std::function<bool(Box)> covered) {
    occlusionTest_ = std::move(covered);
}

void AcpPanel::EndFrame() {
    images_.EndFrame();
}

bool AcpPanel::EntryExpanded(std::size_t index) const {
    if (!acpManager_ || index >= acpManager_->Transcript().size()) {
        return false;
    }
    using Kind           = editor::acp::Manager::TranscriptEntry::Kind;
    const Kind kind      = acpManager_->Transcript()[index].kind;
    bool       byDefault = verbose_;
    if (kind == Kind::ToolCall) {
        byDefault = byDefault || editor::acp::GetAcpToolCallDisplay() == editor::acp::ToolCallDisplay::Expanded;
    }
    else if (kind == Kind::AgentThought) {
        byDefault = byDefault || editor::acp::GetAcpThinkingDisplay() == editor::acp::ThinkingDisplay::Expanded;
    }
    return byDefault != toggledEntries_.contains(index);
}

const acppanel::DisplayLine* AcpPanel::LineAt(int y) const {
    if (!lastShowedTranscript_) {
        return nullptr;
    }
    const int row = lastFirstRow_ + (y - lastTitleRows_);
    if (y < lastTitleRows_ || y >= lastTitleRows_ + lastViewportRows_ || row < 0 || row >= static_cast<int>(transcriptRows_.size())) {
        return nullptr;
    }
    return &transcriptLines_[transcriptRows_[static_cast<std::size_t>(row)].lineIndex];
}

std::optional<editor::acp::Manager::TranscriptImage> AcpPanel::ImageAt(int y) const {
    const acppanel::DisplayLine* line = LineAt(y);
    if (line == nullptr || !acpManager_ || line->entryIndex >= acpManager_->Transcript().size()) {
        return std::nullopt;
    }
    const editor::acp::Manager::TranscriptEntry& entry = acpManager_->Transcript()[line->entryIndex];
    for (const editor::acp::Manager::TranscriptImage& image : entry.images) {
        if (line->image ? image.id == line->image->id : entry.kind == editor::acp::Manager::TranscriptEntry::Kind::AgentContent) {
            return image;
        }
    }
    return std::nullopt;
}

bool AcpPanel::OpenImageMenu(int y, Point anchor) {
    std::optional<editor::acp::Manager::TranscriptImage> image = ImageAt(y);
    if (!image || !onContextMenuRequest_) {
        return false;
    }
    std::string directory = "Downloads";
    if (!imageDirectory_.empty()) {
        directory = imageDirectory_.filename().string();
    }
    else if (const std::optional<std::filesystem::path> downloads = editor::image::DownloadDirectory()) {
        directory = downloads->filename().string();
    }
    std::vector<MenuItem> items;
    items.push_back({.label = "Save to " + directory, .action = [this, image = *image] { SaveImage(image); }});
    items.push_back({.label = "Copy Picture", .action = [this, image = *image] { CopyImage(image); }});
    onContextMenuRequest_("Picture", std::move(items), anchor);
    return true;
}

bool AcpPanel::ActivateRowAt(int y) {
    const acppanel::DisplayLine* found = LineAt(y);
    if (found == nullptr) {
        return false;
    }
    const acppanel::DisplayLine& line = *found;
    switch (line.action) {
        case acppanel::LineAction::ToggleExpand:
            if (line.entryIndex == acppanel::kNoEntry) {
                return false;
            }
            if (!toggledEntries_.erase(line.entryIndex)) {
                toggledEntries_.insert(line.entryIndex);
            }
            ++viewGeneration_;
            return true;
        case acppanel::LineAction::OpenLocation:
            if (line.location && onOpenLocation_) {
                onOpenLocation_(line.location->path, line.location->line.value_or(1));
            }
            return line.location.has_value();
        case acppanel::LineAction::Copy:
            Copy(line.copyText);
            return true;
        case acppanel::LineAction::Review:
            // The turn this line closes: the last one starting before it.
            for (std::size_t index = acpManager_->CheckpointCount(); index-- > 0;) {
                const editor::acp::Manager::Checkpoint& checkpoint = acpManager_->CheckpointAt(index);
                if (checkpoint.transcriptIndex < line.entryIndex) {
                    if (onReviewRequest_) {
                        onReviewRequest_(checkpoint.promptPreview, checkpoint.files);
                    }
                    return true;
                }
            }
            return false;
        case acppanel::LineAction::OpenUrl:
            if (!(urlOpener_ ? urlOpener_(line.url) : editor::link::OpenUrl(line.url))) {
                ShowNotice("Couldn't open " + line.url);
            }
            return true;
        case acppanel::LineAction::None:
            break;
    }
    return false;
}

void AcpPanel::SetOnComposeRequest(std::function<void(std::string, editor::acp::ComposeCallbacks)> onCompose) {
    onComposeRequest_ = std::move(onCompose);
}

void AcpPanel::SetOnRefocusRequest(std::function<void()> onRefocus) {
    onRefocusRequest_ = std::move(onRefocus);
}

void AcpPanel::SetDesktopNotifier(std::function<void(const std::string&, const std::string&)> notifier) {
    desktopNotifier_ = std::move(notifier);
}

void AcpPanel::NoteAttention(editor::acp::Manager::Attention attention, std::chrono::steady_clock::duration turnElapsed) {
    if (!acpManager_) {
        return;
    }
    if (!Focused()) {
        attention_ = true;
    }
    // Long enough that the user has likely switched to something else.
    constexpr std::chrono::seconds kLongTurn{20};
    if (!desktopNotifier_ || (Focused() && turnElapsed < kLongTurn)) {
        return;
    }
    const std::string agent = acpManager_->AgentName().empty() ? std::string("ACP agent") : acpManager_->AgentName();
    std::string       body;
    if (attention == editor::acp::Manager::Attention::PermissionRequested) {
        body = acpManager_->PendingPermissionPrompt() ? acpManager_->PendingPermissionPrompt()->description : std::string();
    }
    else if (attention == editor::acp::Manager::Attention::QuestionAsked) {
        body = acpManager_->PendingElicitation() ? acpManager_->PendingElicitation()->message : std::string();
    }
    else {
        const auto& transcript = acpManager_->Transcript();
        for (auto it = transcript.rbegin(); it != transcript.rend(); ++it) {
            if (it->kind == editor::acp::Manager::TranscriptEntry::Kind::AgentText) {
                const std::size_t start = it->text.find_first_not_of(" \n");
                if (start != std::string::npos) {
                    body = it->text.substr(start, it->text.find('\n', start) - start);
                }
                break;
            }
        }
    }
    constexpr std::size_t kMaxBodyBytes = 160;
    if (body.size() > kMaxBodyBytes) {
        body = body.substr(0, text::SnapDownToCodepointBoundary(body, kMaxBodyBytes)) + "…";
    }
    desktopNotifier_(attention == editor::acp::Manager::Attention::PermissionRequested ? agent + " needs permission"
                     : attention == editor::acp::Manager::Attention::QuestionAsked     ? agent + " has a question"
                                                                                       : agent + " finished",
                     body);
}

void AcpPanel::SetOnOpenLocation(std::function<void(const std::filesystem::path&, std::size_t)> onOpenLocation) {
    onOpenLocation_ = std::move(onOpenLocation);
}

int AcpPanel::TranscriptViewportRows() const {
    return std::max(1, lastViewportRows_);
}

void AcpPanel::JumpToPrompt(int direction) {
    if (!acpManager_ || transcriptRowsWidth_ <= 0) {
        return;
    }
    using Kind                                            = editor::acp::Manager::TranscriptEntry::Kind;
    const std::vector<acppanel::PhysicalLine>& rows       = TranscriptRows(transcriptRowsWidth_);
    const auto&                                transcript = acpManager_->Transcript();
    const int                                  totalRows  = static_cast<int>(rows.size());
    const int                                  viewport   = TranscriptViewportRows();
    const int                                  top        = scroll_.FirstVisibleRow(totalRows, viewport);

    auto startsPrompt = [&](int row) {
        const acppanel::PhysicalLine& physical       = rows[static_cast<std::size_t>(row)];
        const std::size_t             entry          = transcriptLines_[physical.lineIndex].entryIndex;
        const bool                    firstRowOfLine = row == 0 || rows[static_cast<std::size_t>(row - 1)].lineIndex != physical.lineIndex;
        return firstRowOfLine && entry < transcript.size() && transcript[entry].kind == Kind::UserMessage &&
               (row == 0 || transcriptLines_[rows[static_cast<std::size_t>(row - 1)].lineIndex].entryIndex != entry);
    };

    if (direction < 0) {
        for (int row = std::min(top, totalRows) - 1; row >= 0; --row) {
            if (startsPrompt(row)) {
                scroll_.ScrollToRow(row, totalRows, viewport);
                return;
            }
        }
        scroll_.ScrollToTop();
        return;
    }
    for (int row = top + 1; row < totalRows; ++row) {
        if (startsPrompt(row)) {
            scroll_.ScrollToRow(row, totalRows, viewport);
            return;
        }
    }
    scroll_.FollowTail();
}

// Prose-check-the-composer follow-up -- see this method's own doc comment in
// AcpPanel.h.
void AcpPanel::RequestProseCheckIfNeeded() {
    if (!lspManager_ || prompt_.Text() == lastProseCheckedText_) {
        return;
    }
    lastProseCheckedText_ = prompt_.Text();
    // AcpPanel and the Manager it's wired to are both constructed once in
    // main.cpp and live for the process's whole lifetime (destroyed together
    // during main()'s own teardown, well after EventLoop::Run has returned
    // and stopped posting callbacks) -- capturing `this` here carries the
    // exact same lifetime contract every other Manager callback in this
    // codebase already relies on (RequestHover/RequestCompletion/...).
    lspManager_->CheckComposerProseText(lastProseCheckedText_, [this](std::vector<text::Buffer::Diagnostic> diagnostics) {
        composerProseDiagnostics_ = std::move(diagnostics);
    });
}

// @-file-mention autocomplete follow-up -- see this method's own doc comment
// in AcpPanel.h. Purely derived from (prompt_.Text(), prompt_.CursorByteOffset()):
// the current "word" is the run of non-whitespace immediately before the
// cursor; a mention is active iff that word starts with '@'.
void AcpPanel::RefreshMentionState() {
    const std::string& text   = prompt_.Text();
    const std::size_t  cursor = prompt_.CursorByteOffset();
    std::size_t        start  = cursor;
    while (start > 0 && text[start - 1] != ' ' && text[start - 1] != '\n' && text[start - 1] != '\t') {
        --start;
    }
    // A slash command is only a command as the prompt's first word.
    const bool file    = start < cursor && text[start] == '@';
    const bool command = start == 0 && start < cursor && text[0] == '/' && acpManager_ && !acpManager_->AvailableCommands().empty();
    if (!file && !command) {
        mentionPickerOpen_ = false;
        return;
    }
    const CompletionKind kind   = file ? CompletionKind::File : CompletionKind::Command;
    const bool           reopen = !mentionPickerOpen_ || kind != completionKind_;
    completionKind_             = kind;
    mentionStartByte_           = start;
    mentionQuery_               = text.substr(start + 1, cursor - start - 1);
    mentionPickerOpen_          = true;
    if (reopen) {
        RefreshMentionCandidates(); // fresh each time the picker (re)opens -- see its own doc comment
    }
    const std::size_t count = editor::FuzzyFilterAndRank(mentionCandidates_, mentionQuery_).size();
    mentionSelection_       = count == 0 ? 0 : std::min(mentionSelection_, count - 1);
}

void AcpPanel::RefreshMentionCandidates() {
    mentionCandidates_.clear();
    mentionSelection_ = 0;
    if (completionKind_ == CompletionKind::Command) {
        for (const editor::acp::Manager::AvailableCommand& command : acpManager_->AvailableCommands()) {
            mentionCandidates_.push_back(command.name);
        }
        return;
    }
    // ACP context auto-attach follow-up: two built-in mentions alongside
    // every real project file -- "@buffer" resolves at send time
    // (ResolveMentionAttachments) regardless of activeBufferProvider_ being
    // set (it just won't find anything to resolve without one); "@selection"
    // is only offered here when there's actually a selection to attach, so
    // it never appears as a choice with nothing behind it.
    mentionCandidates_.push_back("buffer");
    if (activeBufferProvider_ && activeBufferProvider_().Get().HasMark()) {
        mentionCandidates_.push_back("selection");
    }
    const std::filesystem::path root = editor::ProjectRoot();
    for (const editor::ProjectTreeEntry& entry : editor::BuildProjectTree(root)) {
        if (!entry.isDirectory) {
            mentionCandidates_.push_back(std::filesystem::relative(entry.path, root).generic_string());
        }
    }
}

bool AcpPanel::MoveComposerVertically(int direction) {
    const std::string              statusText   = prompt_.StatusText();
    const std::size_t              labelByteLen = statusText.size() - prompt_.Text().size();
    const acppanel::ComposerLayout layout       = acppanel::LayoutComposer(statusText, labelByteLen + prompt_.CursorByteOffset(), lastComposerWidth_);
    const int                      target       = layout.caretRow + direction;
    if (target < 0 || target >= static_cast<int>(layout.rows.size())) {
        return false;
    }
    const std::size_t byte = acppanel::ByteAtColumn(statusText, layout.rows[static_cast<std::size_t>(target)], layout.caretColumn);
    prompt_.SetCursorByteOffset(byte > labelByteLen ? byte - labelByteLen : 0);
    RefreshMentionState();
    return true;
}

void AcpPanel::SubmitComposer(bool steer) {
    if (!acpManager_ || (prompt_.Text().empty() && pendingImages_.empty())) {
        return;
    }
    editor::acp::Manager::QueuedPrompt prompt{.text = prompt_.Text(), .draft = prompt_.Text()};
    prompt.attachments = ResolveMentionAttachments(prompt.text);
    prompt.attachments.insert(prompt.attachments.end(), pendingImages_.begin(), pendingImages_.end());
    pendingImages_.clear();
    prompt_.SetText("");
    historyIndex_.reset();
    historyDraft_.clear();
    mentionPickerOpen_ = false;
    if (steer) {
        acpManager_->Steer(std::move(prompt));
    }
    else if (acpManager_->PromptInFlight()) {
        acpManager_->QueuePrompt(std::move(prompt));
    }
    else {
        acpManager_->SendPrompt(prompt.text, prompt.attachments);
    }
    scroll_.FollowTail();
}

std::optional<std::string> AcpPanel::AcceptMentionCandidate() {
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(mentionCandidates_, mentionQuery_);
    mentionPickerOpen_                    = false;
    if (ranked.empty()) {
        return std::nullopt;
    }
    const std::size_t  index  = std::min(mentionSelection_, ranked.size() - 1);
    const std::string& text   = prompt_.Text();
    const std::size_t  cursor = prompt_.CursorByteOffset();
    const char         sigil  = completionKind_ == CompletionKind::Command ? '/' : '@';
    // MinibufferPrompt::SetText's own documented "cursor moves to the end"
    // behavior applies here (see AcpPanel.h's own doc comment on this method).
    prompt_.SetText(text.substr(0, mentionStartByte_) + sigil + ranked[index] + " " + text.substr(cursor));
    return ranked[index];
}

const editor::acp::Manager::AvailableCommand* AcpPanel::FindCommand(std::string_view name) const {
    if (!acpManager_) {
        return nullptr;
    }
    for (const editor::acp::Manager::AvailableCommand& command : acpManager_->AvailableCommands()) {
        if (command.name == name) {
            return &command;
        }
    }
    return nullptr;
}

std::vector<AcpPanel::DisplayLine> AcpPanel::FormatMentionPicker() const {
    const bool               commands = completionKind_ == CompletionKind::Command;
    std::vector<DisplayLine> lines;
    lines.push_back({commands ? "Command (Enter to run, Tab to complete, Esc to cancel):" : "Mention a file (Enter/Tab to insert, Esc to cancel):",
                     DisplayStyle::Warning});
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(mentionCandidates_, mentionQuery_);
    if (ranked.empty()) {
        lines.push_back({commands ? "  (no matching commands)" : "  (no matching files)", DisplayStyle::Dim});
        return lines;
    }
    // Windowed so the selection stays visible past the first few.
    const std::size_t shown = std::min(ranked.size(), kMaxMentionChoices);
    const std::size_t first = mentionSelection_ >= shown ? mentionSelection_ - shown + 1 : 0;
    for (std::size_t i = first; i < first + shown; ++i) {
        const bool  selected = i == mentionSelection_;
        std::string label    = ranked[i];
        if (commands) {
            label = "/" + label;
            if (const editor::acp::Manager::AvailableCommand* command = FindCommand(ranked[i])) {
                if (!command->inputHint.empty()) {
                    label += " <" + command->inputHint + ">";
                }
                if (!command->description.empty()) {
                    label += "  -- " + command->description;
                }
            }
        }
        // ACP context auto-attach follow-up: the two built-in mentions get
        // a short description instead of rendering as a bare word, so they
        // read as distinct built-ins rather than a stray same-named file.
        else if (label == "buffer") {
            label = "buffer  -- current file";
        }
        else if (label == "selection") {
            label = "selection  -- current selection";
        }
        lines.push_back({(selected ? "> " : "  ") + label, selected ? DisplayStyle::Accent : DisplayStyle::Plain});
    }
    if (ranked.size() > shown) {
        lines.push_back({"  (" + std::to_string(ranked.size() - shown) + " more...)", DisplayStyle::Dim});
    }
    return lines;
}

namespace {

    // ACP context auto-attach follow-up: finds the first occurrence of
    // `token` (e.g. "@buffer") in `text` that starts a word -- start of
    // string or preceded by whitespace, and followed by end of string or
    // whitespace -- the same "@ must start a word" rule
    // RefreshMentionState's own doc comment already states for opening the
    // picker in the first place. Returns nullopt if no such occurrence
    // exists.
    std::optional<std::size_t> FindMentionToken(const std::string& text, std::string_view token) {
        std::size_t searchFrom = 0;
        while (true) {
            const std::size_t pos = text.find(token, searchFrom);
            if (pos == std::string::npos) {
                return std::nullopt;
            }
            const bool        wordStart = (pos == 0) || std::isspace(static_cast<unsigned char>(text[pos - 1]));
            const std::size_t afterPos  = pos + token.size();
            const bool        wordEnd   = (afterPos == text.size()) || std::isspace(static_cast<unsigned char>(text[afterPos]));
            if (wordStart && wordEnd) {
                return pos;
            }
            searchFrom = pos + 1;
        }
    }

} // namespace

std::vector<editor::acp::Manager::PromptAttachment> AcpPanel::ResolveMentionAttachments(std::string& text) const {
    std::vector<editor::acp::Manager::PromptAttachment> attachments;
    AppendFileMentionLinks(text, attachments);
    if (!activeBufferProvider_) {
        return attachments; // nothing to resolve against -- any "@buffer"/"@selection" token stays as literal text
    }

    // Feature-local cap, deliberately not text::ITextStorage::IsHuge()'s
    // own much larger threshold -- "too big to hold as a Rope" and "too
    // big to paste into one chat prompt" are different questions.
    constexpr std::size_t kMaxAttachmentBytes = 32 * 1024;

    if (const std::optional<std::size_t> pos = FindMentionToken(text, "@buffer")) {
        text::Buffer&     buffer  = activeBufferProvider_().Get();
        const std::string name    = buffer.Path() ? buffer.Path()->filename().string() : buffer.Name();
        const std::string uri     = buffer.Path() ? text::PathToFileUri(*buffer.Path()) : "ned-buffer://" + buffer.Name();
        std::string       content = buffer.Text();
        if (content.size() > kMaxAttachmentBytes) {
            const std::size_t totalBytes = content.size();
            content.resize(kMaxAttachmentBytes);
            content += "\n... [truncated, " + std::to_string(totalBytes) + " bytes total]";
        }
        attachments.push_back({.uri = uri, .name = name, .mimeType = "", .text = content});
        text.replace(*pos, std::string_view("@buffer").size(), "[attached: " + name + "]");
    }

    if (const std::optional<std::size_t> pos = FindMentionToken(text, "@selection")) {
        text::Buffer& buffer = activeBufferProvider_().Get();
        if (buffer.HasMark()) {
            const auto [start, end]     = buffer.Region();
            const std::string selection = buffer.Content().Substring(start, end - start);
            const std::size_t startLine = buffer.Content().ByteOffsetToLine(start) + 1;
            const std::size_t endLine   = buffer.Content().ByteOffsetToLine(end) + 1;
            const std::string fileLabel = buffer.Path() ? buffer.Path()->filename().string() : buffer.Name();
            const std::string name      = fileLabel + "#L" + std::to_string(startLine) + "-" + std::to_string(endLine);
            const std::string uri       = buffer.Path() ? text::PathToFileUri(*buffer.Path()) : "ned-buffer://" + buffer.Name();
            attachments.push_back({.uri = uri, .name = name, .mimeType = "", .text = selection});
            text.replace(*pos, std::string_view("@selection").size(), "[attached: " + name + "]");
        }
        // else: no active selection -- leave "@selection" as literal text, nothing to attach
    }

    return attachments;
}

void AcpPanel::AppendFileMentionLinks(const std::string& text, std::vector<editor::acp::Manager::PromptAttachment>& attachments) {
    const std::filesystem::path root = editor::ProjectRoot();
    std::vector<std::string>    seen;
    std::size_t                 pos = 0;
    while ((pos = text.find('@', pos)) != std::string::npos) {
        const bool  wordStart = pos == 0 || std::isspace(static_cast<unsigned char>(text[pos - 1]));
        std::size_t end       = pos + 1;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        const std::string mention = text.substr(pos + 1, end - pos - 1);
        pos                       = end;
        if (!wordStart || mention.empty() || mention == "buffer" || mention == "selection" ||
            std::find(seen.begin(), seen.end(), mention) != seen.end()) {
            continue;
        }
        std::error_code             ec;
        const std::filesystem::path path = std::filesystem::path(mention).is_absolute() ? std::filesystem::path(mention) : root / mention;
        if (!std::filesystem::is_regular_file(path, ec)) {
            continue; // not a file -- an @-handle or plain text, left alone
        }
        seen.push_back(mention);
        attachments.push_back({.uri = text::PathToFileUri(path.lexically_normal()), .name = mention, .link = true});
    }
}

bool AcpPanel::CloseButtonAt(Point local) const {
    const int width = size().width;
    if (local.y != 0 || width < kMinWidthForCloseButton) {
        return false;
    }
    return local.x >= width - kCloseOffset && local.x <= width - kCloseOffset + 2;
}

bool AcpPanel::MinimizeButtonAt(Point local) const {
    const int width = size().width;
    if (local.y != 0 || width < kMinWidthForMinimizeButton) {
        return false;
    }
    return local.x >= width - kMinimizeOffset && local.x <= width - kMinimizeOffset + 2;
}

// ACP chat-feel round 2: the thin strip Collapsed() renders instead of the
// full panel -- the placement lambda (main.cpp) is expected to hand this a
// short Box (one row for a bottom dock, a couple of columns for a right
// dock); this just fills whatever it's given. The whole strip is a single
// click-to-reopen target (mirroring ProjectSidebar's own collapsed-strip
// convention), not just a small button, since there's very little to aim at
// on a genuinely thin strip.
void AcpPanel::PaintCollapsedStrip(Canvas& canvas, int width, int height) const {
    if (editor::acp::GetAcpPanelDock() == editor::acp::PanelDock::Right) {
        // A right-docked strip is narrow but tall, carrying no readable text
        // -- just a single expand glyph, so the border brush (tuned for thin
        // decorative lines, not text legibility) is fine here.
        const Brush& frameBrush = Focused() ? theme_.borderAccent : theme_.border;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                Cell& cell     = canvas[{.x = x, .y = y}];
                cell.character = " ";
                frameBrush.ApplyTo(cell);
            }
        }
        // Roughly centered vertically, same spirit as ProjectSidebar's own
        // kCollapsedTriangle. Points left ("expand this way"), the opposite
        // direction from the sidebar's own right-pointing glyph, since this
        // panel sits on the right edge of the screen.
        if (width > 0) {
            Cell& hint     = canvas[{.x = 0, .y = height / 2}];
            hint.character = text::EncodeCodepointUtf8(U'◂');
            frameBrush.ApplyTo(hint);
        }
        return;
    }
    // A bottom-docked strip is wide but one row tall -- room enough to show
    // the same agent-name/state title text the full panel's own title row
    // shows, so minimizing doesn't lose that at-a-glance status. Painted
    // with theme_.echoArea, not the border brush -- reported live as nearly
    // unreadable when painted with theme_.border/borderAccent (a color
    // tuned for a thin decorative line, not a full row of text); the
    // composer's own input row hit this identical problem and was fixed the
    // same way -- see this file's own comment on that fix, in Paint()'s
    // input-row section below.
    const Brush stripBrush = theme_.echoArea;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Cell& cell     = canvas[{.x = x, .y = y}];
            cell.character = " ";
            stripBrush.ApplyTo(cell);
        }
    }
    const std::string agentName = acpManager_ && !acpManager_->AgentName().empty() ? acpManager_->AgentName() : std::string("ACP agent");
    const std::string title =
        agentName + " [" + StateLabel(acpManager_) + "] (minimized)";
    DrawBorderTitle(canvas, title, stripBrush);
}

void AcpPanel::BeginResize(Point globalMouse) {
    resizing_           = true;
    resizeAnchorGlobal_ = globalMouse;
    resizeStartPercent_ = editor::acp::PanelSizePercent();
}

void AcpPanel::UpdateResize(Point globalMouse) {
    const bool rightDock = editor::acp::GetAcpPanelDock() == editor::acp::PanelDock::Right;
    // Dragging the resize edge away from the composer grows it in both
    // docks: leftward for a right-docked panel (its own left edge is the
    // handle), upward for a bottom-docked one (its own top border is the
    // handle) -- both expressed as "anchor minus current" so a move in the
    // growing direction yields a positive delta.
    const int deltaPixels       = rightDock ? resizeAnchorGlobal_.x - globalMouse.x : resizeAnchorGlobal_.y - globalMouse.y;
    const int terminalDimension = rightDock ? terminalSize_.width : terminalSize_.height;
    if (terminalDimension <= 0) {
        return; // SetTerminalSize never called yet -- see its own doc comment
    }
    const int deltaPercent = deltaPixels * 100 / terminalDimension;
    editor::acp::SetAcpPanelSizePercent(resizeStartPercent_ + deltaPercent);
    if (deltaPixels < -1 || deltaPixels > 1) {
        // A real drag, not a slightly-wobbly click -- stop it counting as
        // the first half of a collapse double-click (see OnEvent).
        dividerClickPending_ = false;
    }
}

void AcpPanel::EndResize() {
    resizing_ = false;
}

void AcpPanel::Paint(Canvas canvas) {
    const int width  = canvas.size().width;
    int       height = canvas.size().height;
    stripRow_        = -1;
    if (width <= 0 || height <= 0) {
        return;
    }

    SyncSessionView();
    RequestProseCheckIfNeeded();
    SyncElicitation();
    if (Focused()) {
        attention_ = false;
    }

    if (collapsed_) {
        PaintCollapsedStrip(canvas, width, height);
        return;
    }

    // Opaque fill first -- this panel floats over BufferView the same way
    // TerminalPanel does, so every cell in its Box must be painted
    // regardless of content, or the buffer beneath would show through.
    const Brush plainBrush = BrushForStyle(DisplayStyle::Plain);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Cell& cell     = canvas[{.x = x, .y = y}];
            cell.character = " ";
            plainBrush.ApplyTo(cell);
        }
    }

    // Title/divider row -- standalone mode only. PanelDock.h's shared tab
    // strip owns this chrome (including the close/minimize buttons) when
    // dockHosted_, and TitleText() supplies the same text this would draw.
    if (!dockHosted_) {
        const Brush&      frameBrush = Focused() ? theme_.borderAccent : theme_.border;
        const std::string horizontal = text::EncodeCodepointUtf8(RoundedBorderGlyphs().horizontal);
        for (int x = 0; x < width; ++x) {
            Cell& cell     = canvas[{.x = x, .y = 0}];
            cell.character = horizontal;
            frameBrush.ApplyTo(cell);
        }
        DrawBorderTitle(canvas, TitleText(), frameBrush);
        if (width >= kMinWidthForCloseButton) {
            const std::string glyphs[3] = {"[", text::EncodeCodepointUtf8(kCloseIcon), "]"};
            for (int i = 0; i < 3; ++i) {
                Cell& cell     = canvas[{.x = width - kCloseOffset + i, .y = 0}];
                cell.character = glyphs[i];
                frameBrush.ApplyTo(cell);
            }
        }
        if (width >= kMinWidthForMinimizeButton) {
            // TerminalPanel's own kMinimizeIcon (▼), not a plain "-" -- see
            // kMinimizeIcon's own doc comment.
            const std::string glyphs[3] = {"[", text::EncodeCodepointUtf8(kMinimizeIcon), "]"};
            for (int i = 0; i < 3; ++i) {
                Cell& cell     = canvas[{.x = width - kMinimizeOffset + i, .y = 0}];
                cell.character = glyphs[i];
                frameBrush.ApplyTo(cell);
            }
        }
    }

    int titleRows = dockHosted_ ? 0 : 1;
    if (height < titleRows + 1) {
        return;
    }
    // The session strip takes a row of its own once there's more than one
    // conversation, and only while the panel has room to spare for it.
    const editor::acp::SessionTabsPosition stripPosition = editor::acp::GetAcpSessionTabs();
    if (acpManager_ && stripPosition != editor::acp::SessionTabsPosition::Hidden && acpManager_->SessionTabs().size() > 1 &&
        height >= titleRows + 3) {
        if (stripPosition == editor::acp::SessionTabsPosition::Top) {
            PaintSessionStrip(canvas, titleRows, width);
            ++titleRows;
        }
        else {
            --height;
            PaintSessionStrip(canvas, height, width);
        }
    }

    // The composer grows to however many wrapped rows its text needs (capped
    // at kMaxInputRows, beyond which it scrolls internally like the content
    // area does) -- smart-wrapping follow-up: previously a fixed single row,
    // so a prompt longer than the panel's width just ran off-screen with no
    // way to see or correct the hidden part.
    const std::string                         statusText    = prompt_.StatusText();
    const std::size_t                         labelByteLen  = statusText.size() - prompt_.Text().size();
    const acppanel::ComposerLayout            layout        = acppanel::LayoutComposer(statusText, labelByteLen + prompt_.CursorByteOffset(), width);
    const std::vector<acppanel::ComposerRow>& inputRows     = layout.rows;
    const int                                 caretRow      = layout.caretRow;
    const int                                 caretColInRow = layout.caretColumn;
    lastComposerWidth_                                      = width;

    const int allottedInputRows =
        std::max(1, std::min({static_cast<int>(inputRows.size()), kMaxInputRows, std::max(1, height - titleRows)}));
    int inputWindowStart = 0;
    if (static_cast<int>(inputRows.size()) > allottedInputRows) {
        // Scroll the visible window to always include the caret's row --
        // the only way the cursor can leave the allotted rows is Left/Home
        // inside a prompt long enough to be capped.
        inputWindowStart = std::clamp(caretRow - allottedInputRows + 1, 0, static_cast<int>(inputRows.size()) - allottedInputRows);
    }

    const auto [statusLeft, statusRight] = StatusLine();
    const int statusRows                 = (!statusLeft.empty() || !statusRight.empty()) && height - titleRows - allottedInputRows >= 2 ? 1 : 0;
    // Pasted images wait on their own row, right above the composer.
    const int imageRows = !pendingImages_.empty() && height - titleRows - allottedInputRows - statusRows >= 2 ? 1 : 0;
    // Queued prompts sit between the transcript and the status row, one row
    // each, up to a few -- while leaving the transcript at least a row.
    constexpr int     kMaxQueuedRows = 3;
    const std::size_t queued         = acpManager_ ? acpManager_->QueuedPrompts().size() : 0;
    const int         queuedRows     = std::clamp(std::min(static_cast<int>(queued), kMaxQueuedRows), 0,
                                                  std::max(0, height - titleRows - allottedInputRows - statusRows - imageRows - 1));
    const int         contentRows    = std::max(0, height - titleRows - allottedInputRows - statusRows - imageRows - queuedRows);
    for (int row = 0; row < queuedRows; ++row) {
        const bool  overflow = row == queuedRows - 1 && static_cast<int>(queued) > queuedRows;
        std::string text     = overflow ? "⧗ +" + std::to_string(queued - static_cast<std::size_t>(row)) + " more queued"
                                        : "⧗ " + acpManager_->QueuedPrompts()[static_cast<std::size_t>(row)].text;
        std::replace(text.begin(), text.end(), '\n', ' ');
        PaintUtf8Row(canvas, 0, titleRows + contentRows + row, text, BrushForStyle(DisplayStyle::Hint), width);
    }
    lastViewportRows_     = contentRows;
    lastTitleRows_        = titleRows;
    lastShowedTranscript_ = contentRows > 0 && !picker_ && !mentionPickerOpen_ && !form_;
    if (imageRows > 0) {
        PaintUtf8Row(canvas, 0, titleRows + contentRows + queuedRows + statusRows, PendingImagesLine(), BrushForStyle(DisplayStyle::Hint), width);
    }
    if (statusRows > 0) {
        const int   y     = titleRows + contentRows + queuedRows;
        const Brush brush = BrushForStyle(DisplayStyle::Dim);
        const int rightColumns = ColumnCount(statusRight);
        if (rightColumns > 0 && ColumnCount(statusLeft) + 1 + rightColumns <= width) {
            PaintUtf8Row(canvas, 0, y, statusLeft, brush, width);
            PaintUtf8Row(canvas, width - rightColumns, y, statusRight, brush, rightColumns);
        }
        else if (rightColumns > 0 && !notice_.empty() && std::chrono::steady_clock::now() < noticeUntil_) {
            // A notice that doesn't fit beside the rest has the row to
            // itself while it shows -- it's the one thing just said.
            PaintUtf8Row(canvas, 0, y, statusRight, brush, width);
        }
        else {
            PaintUtf8Row(canvas, 0, y, statusLeft, brush, width);
        }
    }
    if (contentRows > 0) {
        auto paintRows = [&](const std::vector<acppanel::PhysicalLine>& rows, int firstRow) {
            for (int row = 0; row < contentRows; ++row) {
                const std::size_t index = static_cast<std::size_t>(firstRow + row);
                if (index >= rows.size()) {
                    break;
                }
                const acppanel::PhysicalLine& line = rows[index];
                PaintStyledRow(canvas, 0, row + titleRows, line.text, line.spans, BrushForStyle(line.style), width);
            }
        };
        if (form_) {
            paintRows(acppanel::WrapDisplayLines(form_->Format(contentRows), width), 0);
        }
        else if (picker_) {
            paintRows(acppanel::WrapDisplayLines(picker_->Format(contentRows), width), 0);
        }
        else if (mentionPickerOpen_) {
            // One clipped row per candidate (a command's input hint can run
            // long), bottom-aligned right above the composer it completes;
            // too tall, it keeps its head -- the title and the selection,
            // which FormatMentionPicker already windows around.
            std::vector<acppanel::PhysicalLine> rows;
            for (acppanel::PhysicalLine& row : acppanel::WrapDisplayLines(FormatMentionPicker(), width)) {
                if (rows.empty() || rows.back().lineIndex != row.lineIndex) {
                    rows.push_back(std::move(row));
                }
            }
            const int shown = std::min(static_cast<int>(rows.size()), contentRows);
            for (int row = 0; row < shown; ++row) {
                const acppanel::PhysicalLine& line = rows[static_cast<std::size_t>(row)];
                PaintStyledRow(canvas, 0, titleRows + contentRows - shown + row, line.text, line.spans, BrushForStyle(line.style), width);
            }
        }
        else {
            const std::vector<acppanel::PhysicalLine>& rows      = TranscriptRows(width);
            const int                                  totalRows = static_cast<int>(rows.size());
            const int                                  firstRow  = scroll_.FirstVisibleRow(totalRows, contentRows);
            lastFirstRow_                                        = firstRow;
            paintRows(rows, firstRow);
            const int below = totalRows - (firstRow + contentRows);
            PaintImages(canvas, rows, firstRow, contentRows - (below > 0 ? 1 : 0), titleRows, width);
            if (below > 0) {
                const std::string marker  = " ↓ " + std::to_string(below) + " more (C-End) ";
                const int         columns = ColumnCount(marker);
                if (columns < width) {
                    PaintUtf8Row(canvas, width - columns, titleRows + contentRows - 1, marker, theme_.echoArea, columns);
                }
            }
        }
    }

    // Input rows. Painted with theme_.echoArea rather than plainBrush -- the
    // same "you're being prompted, type here" brush every other prompt in
    // the editor (find-file, M-x, goto-line, ...) already uses via EchoArea,
    // so the composer reads as a distinct input field instead of blending
    // into the transcript above it (reported live as barely
    // visible/unreadable when painted the same as the rest of the panel --
    // see this panel's own ROADMAP.md entry; the underlying cursor-position
    // editing gap that entry also describes is unaffected by this, only the
    // row's legibility).
    const Brush inputBrush = theme_.echoArea;

    // Prose-check-the-composer follow-up: composerProseDiagnostics_'s byte
    // ranges (into prompt_.Text()) converted once, up front, into
    // statusText's own column space (BufferView's underline-only, no-
    // foreground-change treatment -- diagnostics-UX follow-up -- reused
    // verbatim rather than inventing a second visual language for the same
    // "the problem is HERE" cue). labelByteLen is StatusText()'s own
    // "label_ + text_" concatenation (MinibufferPrompt.cpp), so
    // labelByteLen + a byte offset into Text() is the matching offset into
    // statusText. Widens a zero-length span by one column, same as
    // BufferView's own inline-diagnostic pass.
    // Byte ranges into statusText (the label, then Text()).
    std::vector<std::pair<std::size_t, std::size_t>> proseRanges;
    for (const text::Buffer::Diagnostic& diagnostic : composerProseDiagnostics_) {
        const std::size_t size  = prompt_.Text().size();
        const std::size_t start = std::min(diagnostic.startByte, size);
        const std::size_t end   = std::max(start, std::min(diagnostic.endByte, size));
        proseRanges.emplace_back(labelByteLen + start, labelByteLen + end);
    }

    for (int j = 0; j < allottedInputRows; ++j) {
        const int screenRow = height - allottedInputRows + j;
        for (int x = 0; x < width; ++x) {
            Cell& cell     = canvas[{.x = x, .y = screenRow}];
            cell.character = " ";
            inputBrush.ApplyTo(cell);
        }
        const std::size_t rowIndex = static_cast<std::size_t>(inputWindowStart + j);
        if (rowIndex < inputRows.size()) {
            const acppanel::ComposerRow& row = inputRows[rowIndex];
            PaintUtf8Row(canvas, 0, screenRow, row.text, inputBrush, width);
            for (const auto& [rangeStart, rangeEnd] : proseRanges) {
                const std::size_t start = std::max(rangeStart, row.byteStart);
                const std::size_t end   = std::min(rangeEnd, row.byteEnd);
                // A zero-length diagnostic still underlines one column.
                const bool empty = rangeStart == rangeEnd;
                if (empty ? (rangeStart < row.byteStart || rangeStart > row.byteEnd) : start >= end) {
                    continue;
                }
                const int startColumn = ColumnCount(std::string_view(statusText).substr(row.byteStart, start - row.byteStart));
                const int endColumn   = std::max(startColumn + 1, ColumnCount(std::string_view(statusText).substr(row.byteStart, end - row.byteStart)));
                for (int x = startColumn; x < std::min(endColumn, width); ++x) {
                    canvas[{.x = x, .y = screenRow}].underlined = true;
                }
            }
        }
    }
    // minibuffer-composer-cursor-editing follow-up: the caret sits at the
    // prompt's real cursor position now, not always at the end of the typed
    // text -- CursorDisplayColumn() (display columns, as PaintUtf8Row
    // paints the text) is what stays in sync with
    // InsertChar/DeleteBackward/DeleteForward/Move* below, mapped through
    // the wrap above into (caretRow, caretColInRow) so it still lands on the
    // right glyph once the composer spans multiple rows. A real solid block
    // cursor, not a video-invert: character is left untouched (so whatever's
    // already there -- a real typed character, or the row fill's blank when
    // the cursor sits past the end of the text -- stays visible through it)
    // and only recolored, explicitly, to a fixed high-contrast pair rather
    // than swapping whatever foreground happened to be underneath --
    // inverting inputBrush's own default-ish foreground left the character
    // under the caret nearly unreadable against the yellow block (reported
    // live after moving the cursor back over typed text).
    if (caretRow >= inputWindowStart && caretRow < inputWindowStart + allottedInputRows && caretColInRow < width) {
        const int screenRow = height - allottedInputRows + (caretRow - inputWindowStart);
        Cell&     cell      = canvas[{.x = caretColInRow, .y = screenRow}];
        Brush{.background = inputBrush.foreground, .foreground = theme_.background}.ApplyTo(cell);
    }
}

bool AcpPanel::OnEvent(const Event& event) {
    SyncSessionView();
    if (event.is_mouse()) {
        const MouseEvent rawMouse = event.mouse();

        // ProjectSidebar::OnEvent's own exact resize-drag shape: Moved/
        // Released are handled against the *global* mouse position before
        // the local-bounds hit test below, since a fast drag can carry the
        // cursor outside this panel's own Box mid-session (BufferView
        // cooperates the same way once a sidebar drag crosses out of its
        // bounds -- see Widget.h's own header comment on this).
        if (rawMouse.motion == MouseEvent::Motion::Moved && resizing_) {
            UpdateResize(rawMouse.at);
            return true;
        }
        if (rawMouse.motion == MouseEvent::Motion::Released && resizing_) {
            EndResize();
            return true;
        }

        const std::optional<MouseEvent> mouse = LocalMouseEvent(event);
        if (!mouse) {
            return false;
        }

        if (!collapsed_ && acpManager_ && stripRow_ >= 0 && mouse->at.y == stripRow_ && mouse->motion == MouseEvent::Motion::Pressed &&
            (mouse->button == MouseEvent::Button::Left || mouse->button == MouseEvent::Button::Right)) {
            TakeFocus();
            ClickSessionStrip(mouse->at.x, mouse->button == MouseEvent::Button::Right, rawMouse.at);
            SyncSessionView();
            return true;
        }

        if (!collapsed_ && (mouse->button == MouseEvent::Button::WheelUp || mouse->button == MouseEvent::Button::WheelDown) &&
            mouse->motion == MouseEvent::Motion::Pressed) {
            const int totalRows = static_cast<int>(transcriptRows_.size());
            scroll_.ScrollBy(mouse->button == MouseEvent::Button::WheelUp ? -3 : 3, totalRows, TranscriptViewportRows());
            return true;
        }

        if (!collapsed_ && mouse->button == MouseEvent::Button::Right && mouse->motion == MouseEvent::Motion::Pressed) {
            return OpenImageMenu(mouse->at.y, rawMouse.at);
        }

        // Collapsed/close-button/minimize-button/resize-divider hit-testing
        // is standalone-mode-only -- PanelDock.h's shared tab strip owns all
        // of that when dockHosted_ (collapse has no dock-hosted equivalent
        // at all, superseded by switching tabs).
        if (!dockHosted_) {
            // Collapsed: the whole strip is a single click-to-reopen target
            // -- there's very little to aim at on a genuinely thin strip, so
            // no separate button hit-test the way the full panel has.
            if (collapsed_) {
                if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
                    SetCollapsed(false);
                    TakeFocus();
                }
                return true;
            }

            if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
                if (CloseButtonAt(mouse->at)) {
                    if (onToggleRequest_) {
                        onToggleRequest_();
                    }
                    return true;
                }
                if (MinimizeButtonAt(mouse->at)) {
                    SetCollapsed(true);
                    return true;
                }
                // The resize divider: the title row for a bottom dock (its
                // own dedicated row, not shared with any content), the
                // panel's own left edge column for a right dock (the
                // boundary shared with BufferView beneath it --
                // ProjectSidebar's own right-edge divider, mirrored).
                const bool rightDock = editor::acp::GetAcpPanelDock() == editor::acp::PanelDock::Right;
                if (rightDock ? mouse->at.x == 0 : mouse->at.y == 0) {
                    // A second press within the double-click window
                    // collapses instead of starting a resize -- ProjectSidebar/
                    // VcsPanel's own divider convention, previously missing
                    // here (this panel only had the explicit [-] button/M-m).
                    const auto now = std::chrono::steady_clock::now();
                    if (dividerClickPending_ && (now - lastDividerPressTime_) < kDoubleClickWindow) {
                        dividerClickPending_ = false;
                        SetCollapsed(true);
                        return true;
                    }
                    dividerClickPending_  = true;
                    lastDividerPressTime_ = now;
                    BeginResize(rawMouse.at);
                    TakeFocus();
                    return true;
                }
                ActivateRowAt(mouse->at.y);
                TakeFocus();
                return true;
            }
            return false;
        }

        if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
            ActivateRowAt(mouse->at.y);
            TakeFocus();
            return true;
        }
        return false;
    }

    const std::optional<editor::KeyChord> chord = TranslateKey(event);
    if (!chord) {
        return false;
    }
    if (forwardingSequence_) {
        forwardingSequence_ = forwardChord_ && forwardChord_(*chord);
        return true;
    }
    SyncElicitation();

    // ACP round-1-live-validation follow-up: while a permission prompt is
    // pending, this panel resolves it directly rather than leaving
    // resolution to BufferView's separate echo-area InputMode::
    // AcpPermissionPrompt flow -- the "deliberate v1 cut" this class's own
    // header comment used to document. WindowManager's SetAcpPanelFocusChecker
    // wiring skips routing a new request to the focused pane's echo area
    // whenever this panel has focus, so this is the only place such a
    // keystroke lands in that case. Digit keys only, matching the numbered
    // list FormatTranscript already renders -- no moving selection cursor
    // the way BufferView's own flow has, so Enter is deliberately left alone
    // (no obvious default option to pick without one).
    if (acpManager_ && acpManager_->PendingPermissionPrompt()) {
        const editor::acp::Manager::PermissionPrompt& pending = *acpManager_->PendingPermissionPrompt();
        if (chord->Special == editor::SpecialKey::Escape) {
            acpManager_->CancelPermissionPrompt();
            return true;
        }
        if (IsPlainCharacter(*chord) && chord->Codepoint >= U'1' && chord->Codepoint <= U'9') {
            const std::size_t index = static_cast<std::size_t>(chord->Codepoint - U'1');
            if (index < pending.options.size()) {
                acpManager_->ResolvePermissionPrompt(pending.options[index].optionId);
            }
            return true; // out-of-range digit: stay put, same as BufferView's own flow
        }
        // Anything else (typing into the composer, non-digit keys) falls
        // through unhandled by this block.
    }

    // C-c prefix: C-c ' continues the composer's text in a full buffer
    // (org-edit-special's key); C-c C-s steers, for terminals that can't
    // tell C-RET from RET; C-c C-f toggles following the agent; C-c C-r
    // reviews a turn's file changes; C-c C-w saves a picture. Any other
    // sequence belongs to the editor's keymap, as do C-x sequences.
    if (controlCPending_) {
        controlCPending_ = false;
        if (chord->Control && !chord->Meta && chord->Codepoint == U's') {
            SubmitComposer(true);
            return true;
        }
        if (chord->Control && !chord->Meta && chord->Codepoint == U'f') {
            ToggleFollowAgent();
            return true;
        }
        if (chord->Control && !chord->Meta && chord->Codepoint == U'r') {
            OpenPicker(editor::acp::PanelPicker::Review);
            return true;
        }
        if (chord->Control && !chord->Meta && chord->Codepoint == U'w') {
            OpenPicker(editor::acp::PanelPicker::SaveImage);
            return true;
        }
        if (chord->Control && !chord->Meta && chord->Codepoint == U'c' && form_) {
            SubmitElicitation();
            return true;
        }
        if (IsPlainCharacter(*chord) && chord->Codepoint == U'\'' && onComposeRequest_) {
            onComposeRequest_(prompt_.Text(), editor::acp::ComposeCallbacks{
                                                  .onSend =
                                                      [this](std::string text) {
                                                          prompt_.SetText(std::move(text));
                                                          SubmitComposer();
                                                          if (onRefocusRequest_) {
                                                              onRefocusRequest_();
                                                          }
                                                      },
                                                  .onCancel =
                                                      [this] {
                                                          if (onRefocusRequest_) {
                                                              onRefocusRequest_();
                                                          }
                                                      },
                                              });
            return true;
        }
        if (forwardChord_) {
            forwardChord_(controlCChord_);
            forwardingSequence_ = forwardChord_(*chord);
            return true;
        }
    }
    else if (chord->Control && !chord->Meta && chord->Codepoint == U'c') {
        controlCPending_ = true;
        controlCChord_   = *chord;
        return true;
    }
    else if (chord->Control && !chord->Meta && chord->Codepoint == U'x' && forwardChord_) {
        forwardingSequence_ = forwardChord_(*chord);
        return true;
    }

    // An open question owns the keyboard until it's answered.
    if (form_) {
        switch (form_->HandleKey(*chord)) {
            case acppanel::ElicitationForm::KeyResult::Submit:
                SubmitElicitation();
                break;
            case acppanel::ElicitationForm::KeyResult::Decline:
                acpManager_->AnswerElicitation("decline");
                break;
            case acppanel::ElicitationForm::KeyResult::Cancel:
                acpManager_->AnswerElicitation("cancel");
                break;
            case acppanel::ElicitationForm::KeyResult::Handled:
                break;
        }
        SyncElicitation();
        return true;
    }

    // An open picker owns every keystroke; nothing lands in the composer
    // behind it.
    if (picker_) {
        if (picker_->HandleKey(*chord) != acppanel::ChoicePicker::KeyResult::Handled) {
            picker_ = std::move(pendingPicker_);
            pendingPicker_.reset();
            StopAwaitingSessions();
        }
        return true;
    }

    // S-Tab cycles the session's mode, M-p opens the model picker -- Claude
    // Code's own keys for both.
    if (chord->Special == editor::SpecialKey::Tab && chord->Shift && acpManager_) {
        acpManager_->CycleMode();
        return true;
    }
    if (chord->Meta && !chord->Control && chord->Codepoint == U'p') {
        OpenPicker(editor::acp::PanelPicker::Model);
        return true;
    }
    // C-v pastes an image as an attachment (Claude Code's key), or text.
    if (chord->Control && !chord->Meta && chord->Codepoint == U'v') {
        PasteFromClipboard();
        return true;
    }
    // M-w: the composer has no region to copy, so it copies from the transcript.
    if (chord->Meta && !chord->Control && chord->Codepoint == U'w') {
        OpenPicker(editor::acp::PanelPicker::Copy);
        return true;
    }

    // @-file-mention autocomplete follow-up: while a mention query is active
    // (RefreshMentionState, called after every composer edit below), Up/Down/
    // Enter/Tab/Escape are claimed for narrowing/accepting/dismissing the
    // suggestion list instead of their usual composer meaning (history
    // recall / send / panel-close). Deliberately no unconditional catch-all
    // `return true` at the bottom of this block, unlike picker_
    // above -- anything else (more query characters, Backspace, cursor
    // motion) must still fall through to the ordinary composer handling
    // below, which re-derives mention state itself after every edit.
    if (mentionPickerOpen_) {
        if (chord->Special == editor::SpecialKey::Escape) {
            mentionPickerOpen_ = false;
            return true;
        }
        if (chord->Special == editor::SpecialKey::Down || chord->Special == editor::SpecialKey::Up) {
            const std::size_t count = editor::FuzzyFilterAndRank(mentionCandidates_, mentionQuery_).size();
            if (count > 0) {
                mentionSelection_ = chord->Special == editor::SpecialKey::Down ? (mentionSelection_ + 1) % count
                                                                               : (mentionSelection_ + count - 1) % count;
            }
            return true;
        }
        if (chord->Special == editor::SpecialKey::Enter || chord->Special == editor::SpecialKey::Tab) {
            const bool                       command  = completionKind_ == CompletionKind::Command;
            const std::optional<std::string> accepted = AcceptMentionCandidate();
            // Enter on a command that takes no input runs it outright.
            if (command && accepted && chord->Special == editor::SpecialKey::Enter) {
                const editor::acp::Manager::AvailableCommand* found = FindCommand(*accepted);
                if (found && found->inputHint.empty()) {
                    std::string text = prompt_.Text();
                    while (!text.empty() && text.back() == ' ') {
                        text.pop_back();
                    }
                    prompt_.SetText(text);
                    SubmitComposer();
                }
            }
            return true;
        }
    }

    if (chord->Special == editor::SpecialKey::Escape) {
        // chat-feel follow-up: interrupt a still-streaming reply first,
        // keeping whatever partial output already arrived (Claude Code's
        // own single-Esc-to-interrupt) -- a second Escape once nothing is
        // in flight falls through to the pre-existing close-panel behavior.
        if (acpManager_ && acpManager_->PromptInFlight()) {
            acpManager_->CancelPrompt();
            // Interrupting hands the queued prompts back to be edited, ahead
            // of anything already typed.
            std::string restored;
            for (const editor::acp::Manager::QueuedPrompt& queued : acpManager_->TakeQueue()) {
                restored += (restored.empty() ? "" : "\n") + queued.draft;
            }
            if (!restored.empty()) {
                prompt_.SetText(prompt_.Text().empty() ? restored : restored + "\n" + prompt_.Text());
            }
            return true;
        }
        if (onToggleRequest_) {
            onToggleRequest_();
        }
        return true;
    }
    // C-o: every tool call and thought expanded, or back to the defaults --
    // Claude Code's own verbose-transcript key.
    if (chord->Control && !chord->Meta && chord->Codepoint == U'o') {
        verbose_ = !verbose_;
        toggledEntries_.clear();
        ++viewGeneration_;
        return true;
    }
    // C-PageUp/C-PageDown switch conversations, like tabs elsewhere.
    if ((chord->Special == editor::SpecialKey::PageUp || chord->Special == editor::SpecialKey::PageDown) && chord->Control && acpManager_) {
        acpManager_->CycleSession(chord->Special == editor::SpecialKey::PageDown ? 1 : -1);
        SyncSessionView();
        return true;
    }
    // Transcript scrolling. C-Home/C-End rather than Home/End, which move
    // the composer's cursor.
    {
        const int totalRows = static_cast<int>(transcriptRows_.size());
        const int page      = std::max(1, TranscriptViewportRows() - 1);
        if (chord->Special == editor::SpecialKey::PageUp) {
            scroll_.ScrollBy(-page, totalRows, TranscriptViewportRows());
            return true;
        }
        if (chord->Special == editor::SpecialKey::PageDown) {
            scroll_.ScrollBy(page, totalRows, TranscriptViewportRows());
            return true;
        }
        if (chord->Special == editor::SpecialKey::Home && chord->Control) {
            scroll_.ScrollToTop();
            return true;
        }
        if (chord->Special == editor::SpecialKey::End && chord->Control) {
            scroll_.FollowTail();
            return true;
        }
        if ((chord->Special == editor::SpecialKey::Up || chord->Special == editor::SpecialKey::Down) && chord->Meta) {
            JumpToPrompt(chord->Special == editor::SpecialKey::Up ? -1 : 1);
            return true;
        }
    }
    if (chord->Special == editor::SpecialKey::Backspace) {
        // At the very start of the composer it takes back the last pasted image.
        if (prompt_.CursorByteOffset() == 0 && !pendingImages_.empty()) {
            ShowNotice("removed " + pendingImages_.back().name);
            pendingImages_.pop_back();
            return true;
        }
        prompt_.DeleteBackward();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Delete) {
        prompt_.DeleteForward();
        RefreshMentionState();
        return true;
    }
    // minibuffer-composer-cursor-editing round 2: word-wise motion, checked
    // ahead of the plain Left/Right handlers below so Control-Left/Right
    // don't fall through to a single-codepoint move.
    if (chord->Special == editor::SpecialKey::Left && chord->Control) {
        prompt_.MoveCursorWordLeft();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Right && chord->Control) {
        prompt_.MoveCursorWordRight();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Left) {
        prompt_.MoveCursorLeft();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Right) {
        prompt_.MoveCursorRight();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Home) {
        const std::size_t lineStart = prompt_.Text().rfind('\n', prompt_.CursorByteOffset() == 0 ? 0 : prompt_.CursorByteOffset() - 1);
        prompt_.SetCursorByteOffset(lineStart == std::string::npos || prompt_.CursorByteOffset() == 0 ? 0 : lineStart + 1);
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::End) {
        prompt_.SetCursorByteOffset(std::min(prompt_.Text().find('\n', prompt_.CursorByteOffset()), prompt_.Text().size()));
        RefreshMentionState();
        return true;
    }
    // Keyboard resize fallback, alongside the border-drag in the mouse
    // handling above -- Control-Up/Down, checked ahead of the plain Up/Down
    // history recall below the same way Control-Left/Right is checked ahead
    // of plain Left/Right above.
    if (chord->Special == editor::SpecialKey::Up && chord->Control) {
        editor::acp::SetAcpPanelSizePercent(editor::acp::PanelSizePercent() + 5);
        return true;
    }
    if (chord->Special == editor::SpecialKey::Down && chord->Control) {
        editor::acp::SetAcpPanelSizePercent(editor::acp::PanelSizePercent() - 5);
        return true;
    }
    // Keyboard minimize toggle, alongside the [-] title-bar button above --
    // M-m, unused elsewhere in this composer (a plain "m" keystroke still
    // types the letter as always; only the Meta-modified chord is claimed).
    // Standalone-mode only -- collapse has no dock-hosted equivalent (see
    // SetDockHosted's own doc comment).
    if (!dockHosted_ && chord->Meta && chord->Codepoint == U'm') {
        ToggleCollapsed();
        return true;
    }
    // Shell-style prompt history -- Up/Down are otherwise unbound in this
    // composer (a single logical line, word-wrapped but with no vertical
    // intra-composer cursor movement of its own), so there's no existing
    // affordance this takes away.
    // Up/Down move between the composer's rows first; history recall only
    // from its first/last row.
    if ((chord->Special == editor::SpecialKey::Up || chord->Special == editor::SpecialKey::Down) && MoveComposerVertically(chord->Special == editor::SpecialKey::Up ? -1 : 1)) {
        return true;
    }
    if (chord->Special == editor::SpecialKey::Up && prompt_.Text().empty() && acpManager_ && !acpManager_->QueuedPrompts().empty()) {
        prompt_.SetText(acpManager_->TakeLastQueued()->draft);
        return true;
    }
    if (chord->Special == editor::SpecialKey::Up) {
        HistoryPrevious();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Down) {
        HistoryNext();
        RefreshMentionState();
        return true;
    }
    // M-RET/S-RET start a new line (S-RET needs a terminal that reports
    // Shift on Enter, e.g. under the kitty keyboard protocol).
    if (chord->Special == editor::SpecialKey::Enter && (chord->Meta || chord->Shift)) {
        prompt_.InsertChar(U'\n');
        RefreshMentionState();
        return true;
    }
    // Enter sends, or queues behind a running turn; C-RET steers the
    // running turn instead.
    if (chord->Special == editor::SpecialKey::Enter) {
        SubmitComposer(chord->Control);
        return true;
    }
    if (IsPlainCharacter(*chord)) {
        prompt_.InsertChar(chord->Codepoint);
        RefreshMentionState();
        return true;
    }
    return false;
}

} // namespace ned::ui
