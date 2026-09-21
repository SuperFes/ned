#include "DebugPanel.h"

#include <algorithm>
#include <utility>

#include "BreakpointGlyph.h"

namespace ned::ui {

namespace {

    // What a breakpoint's own qualifiers read as beside its line number --
    // the gutter can only show one glyph, so this is where a condition
    // actually becomes legible.
    std::string QualifierSuffix(const editor::dap::Manager::Breakpoint& breakpoint) {
        std::string suffix;
        if (!breakpoint.condition.empty()) {
            suffix += "  if " + breakpoint.condition;
        }
        if (!breakpoint.hitCondition.empty()) {
            suffix += "  hits " + breakpoint.hitCondition;
        }
        if (!breakpoint.logMessage.empty()) {
            suffix += "  log " + breakpoint.logMessage;
        }
        return suffix;
    }

    // A value column is one row wide; a multi-line or very long value would
    // otherwise push everything else off the right edge.
    std::string OneLine(std::string text, std::size_t limit = 40) {
        const auto newline = text.find('\n');
        if (newline != std::string::npos) {
            text.resize(newline);
        }
        if (text.size() > limit) {
            text.resize(limit);
            text += "…";
        }
        return text;
    }

    const char* TitleFor(int section) {
        switch (section) {
            case 0:
                return "Call stack";
            case 1:
                return "Variables";
            case 2:
                return "Watches";
            case 3:
                return "Breakpoints";
            case 4:
                return "Function breakpoints";
            case 5:
                return "Data breakpoints";
            case 6:
                return "Exception breakpoints";
            case 7:
                return "Loaded sources";
            default:
                return "Modules";
        }
    }

} // namespace

DebugPanel::DebugPanel(const Theme& theme, editor::dap::Manager& dapManager) : theme_(theme), dapManager_(dapManager), tree_(theme) {
    tree_.SetOnSelectionChanged([this](std::size_t index) { selectedIndex_ = index; });
    tree_.SetOnActivate([this](std::size_t index) { HandleActivate(index); });
    tree_.SetOnToggleExpand([this](std::size_t index) { HandleToggleExpand(index); });
    tree_.SetOnCollapseRequested([this](std::size_t index) { HandleCollapse(index); });
    tree_.SetOnKey([this](const editor::KeyChord& chord) { HandleKey(chord); });
    tree_.SetOnCancel([this] {
        if (onCancel_) {
            onCancel_();
        }
    });
    Refresh();
}

TreeView& DebugPanel::Tree() {
    return tree_;
}

void DebugPanel::SetOnVisitLocation(std::function<void(const std::filesystem::path&, std::size_t)> handler) {
    onVisitLocation_ = std::move(handler);
}

void DebugPanel::SetOnTextEntryRequest(
    std::function<void(std::string, std::string, std::function<void(std::string)>)> handler) {
    onTextEntryRequest_ = std::move(handler);
}

void DebugPanel::SetOnMessage(std::function<void(std::string)> handler) {
    onMessage_ = std::move(handler);
}

void DebugPanel::SetOnCancel(std::function<void()> handler) {
    onCancel_ = std::move(handler);
}

void DebugPanel::Report(std::string message) {
    if (onMessage_) {
        onMessage_(std::move(message));
    }
}

void DebugPanel::Prompt(std::string label, std::string initialText, std::function<void(std::string)> onAccept) {
    if (!onTextEntryRequest_) {
        return;
    }
    onTextEntryRequest_(std::move(label), std::move(initialText), std::move(onAccept));
}

// ---------------------------------------------------------------- fetching

void DebugPanel::NotifySessionStateChanged() {
    FetchSessionData();
}

void DebugPanel::FetchSessionData() {
    // Everything held here describes one stop. Invalidate first so a late
    // answer from the previous one cannot land, then drop it all -- a
    // running debuggee has no stack to show, and showing the last one
    // would be a lie rather than a stale-but-true reading.
    ++generation_;
    threads_.clear();
    frames_.clear();
    scopes_.clear();
    variables_.clear();
    expandedVariables_.clear();
    loadedSources_.clear();
    modules_.clear();
    FetchWatchValues(); // clears the values whether or not it re-requests them

    if (dapManager_.State() != editor::dap::Manager::SessionState::Stopped) {
        Refresh();
        return;
    }

    const std::uint64_t generation = generation_;
    dapManager_.RequestThreads([this, generation](std::vector<editor::dap::Manager::Thread> threads) {
        if (generation != generation_) {
            return;
        }
        threads_ = std::move(threads);
        // The stopped thread's own frames are what the user is looking at,
        // so they are fetched without being asked for; any other thread's
        // cost a request and wait to be expanded.
        const int focused = dapManager_.FocusedThreadId();
        expandedThreads_.insert(focused);
        FetchFramesFor(focused);
        Refresh();
    });
    FetchInventory();
}

void DebugPanel::FetchInventory() {
    const std::uint64_t generation = generation_;
    dapManager_.RequestLoadedSources([this, generation](std::vector<editor::dap::Manager::LoadedSource> sources) {
        if (generation != generation_) {
            return;
        }
        loadedSources_ = std::move(sources);
        Refresh();
    });
    dapManager_.RequestModules([this, generation](std::vector<editor::dap::Manager::Module> modules) {
        if (generation != generation_) {
            return;
        }
        modules_ = std::move(modules);
        Refresh();
    });
}

void DebugPanel::FetchFramesFor(int threadId) {
    const std::uint64_t generation = generation_;
    dapManager_.RequestStackTrace(
        [this, generation, threadId](std::vector<editor::dap::Manager::StackFrame> frames) {
            if (generation != generation_) {
                return;
            }
            // The scopes shown belong to whichever frame the session is
            // focused on, which every stop seeds to the top frame -- so
            // fetching the focused thread's frames is also what makes the
            // Variables section appear.
            const bool isFocusedThread = threadId == dapManager_.FocusedThreadId();
            frames_[threadId]          = std::move(frames);
            if (isFocusedThread && !frames_[threadId].empty()) {
                FetchScopesFor(frames_[threadId].front().id);
            }
            Refresh();
        },
        threadId);
}

void DebugPanel::FetchScopesFor(int frameId) {
    const std::uint64_t generation = generation_;
    dapManager_.RequestScopes(frameId, [this, generation](std::vector<editor::dap::Manager::Scope> scopes) {
        if (generation != generation_) {
            return;
        }
        scopes_ = std::move(scopes);
        // A scope's own variables are fetched up front -- that is one
        // request per scope, not per variable, and it is what makes the
        // section useful at a glance rather than a list of things to open.
        for (const editor::dap::Manager::Scope& scope : scopes_) {
            if (scope.variablesReference != 0) {
                expandedVariables_.insert(scope.variablesReference);
                FetchVariablesFor(scope.variablesReference);
            }
        }
        Refresh();
    });
}

void DebugPanel::FetchVariablesFor(int variablesReference) {
    const std::uint64_t generation = generation_;
    dapManager_.RequestVariables(variablesReference,
                                 [this, generation, variablesReference](std::vector<editor::dap::Manager::Variable> variables) {
                                     if (generation != generation_) {
                                         return;
                                     }
                                     variables_[variablesReference] = std::move(variables);
                                     Refresh();
                                 });
}

void DebugPanel::FetchWatchValues() {
    const std::vector<std::string> watches = dapManager_.Watches();
    watchValues_.assign(watches.size(), std::string());
    if (dapManager_.State() != editor::dap::Manager::SessionState::Stopped) {
        return; // the expressions still list; their values are simply unknown
    }
    const std::uint64_t generation = generation_;
    for (std::size_t i = 0; i < watches.size(); ++i) {
        dapManager_.Evaluate(
            watches[i],
            [this, generation, i](bool success, std::string text) {
                if (generation != generation_ || i >= watchValues_.size()) {
                    return;
                }
                watchValues_[i] = success ? std::move(text) : ("<" + text + ">");
                Refresh();
            },
            "watch");
    }
}

// ----------------------------------------------------------------- rebuild

void DebugPanel::Refresh() {
    // The row under the selection is what the user is looking at, not its
    // index -- a breakpoint added above it must not move what a following
    // keystroke acts on. Re-found by identity after the rebuild.
    const bool hadSelection = selectedIndex_ < rows_.size();
    const Row  previous     = hadSelection ? rows_[selectedIndex_] : Row{};
    BuildRows();
    selectedIndex_ = 0;
    if (hadSelection) {
        const auto match = std::find(rows_.begin(), rows_.end(), previous);
        if (match != rows_.end()) {
            selectedIndex_ = static_cast<std::size_t>(std::distance(rows_.begin(), match));
        }
    }
    PushModel();
}

void DebugPanel::AppendVariableRows(Section section, int variablesReference, std::size_t depth) {
    const auto it = variables_.find(variablesReference);
    if (it == variables_.end()) {
        return; // not fetched (or fetched empty) -- the parent row keeps its affordance
    }
    for (std::size_t i = 0; i < it->second.size(); ++i) {
        const editor::dap::Manager::Variable& variable = it->second[i];
        rows_.push_back(Row{.kind               = Row::Kind::Variable,
                            .section            = section,
                            .key                = variable.name,
                            .index              = i,
                            .childrenReference  = variable.variablesReference,
                            .containerReference = variablesReference});
        if (variable.variablesReference != 0 && expandedVariables_.contains(variable.variablesReference)) {
            AppendVariableRows(section, variable.variablesReference, depth + 1);
        }
    }
}

void DebugPanel::BuildRows() {
    rows_.clear();

    const bool stopped = dapManager_.State() == editor::dap::Manager::SessionState::Stopped;

    const auto open       = [this](Section section) { return !collapsedSections_.contains(section); };
    const auto pushHeader = [this](Section section) {
        rows_.push_back(Row{.kind = Row::Kind::SectionHeader, .section = section});
    };
    const auto pushPlaceholder = [this](Section section) {
        rows_.push_back(Row{.kind = Row::Kind::Placeholder, .section = section});
    };

    // Call stack and variables are only present while stopped -- a running
    // debuggee has no stack to report, and an inactive session has no
    // threads at all. Keeping the sections but emptying them would claim
    // there is nothing on the stack, which is a different statement.
    if (stopped) {
        pushHeader(Section::CallStack);
        if (open(Section::CallStack)) {
            if (threads_.empty()) {
                pushPlaceholder(Section::CallStack);
            }
            for (const editor::dap::Manager::Thread& thread : threads_) {
                rows_.push_back(Row{.kind = Row::Kind::Thread, .section = Section::CallStack, .key = thread.name, .id = thread.id});
                if (!expandedThreads_.contains(thread.id)) {
                    continue;
                }
                const auto frames = frames_.find(thread.id);
                if (frames == frames_.end()) {
                    continue; // request still in flight
                }
                for (const editor::dap::Manager::StackFrame& frame : frames->second) {
                    rows_.push_back(Row{.kind = Row::Kind::Frame, .section = Section::CallStack, .key = frame.name, .id = frame.id});
                }
            }
        }

        pushHeader(Section::Variables);
        if (open(Section::Variables)) {
            if (scopes_.empty()) {
                pushPlaceholder(Section::Variables);
            }
            for (const editor::dap::Manager::Scope& scope : scopes_) {
                rows_.push_back(Row{.kind              = Row::Kind::Scope,
                                    .section           = Section::Variables,
                                    .key               = scope.name,
                                    .childrenReference = scope.variablesReference});
                if (scope.variablesReference != 0 && expandedVariables_.contains(scope.variablesReference)) {
                    AppendVariableRows(Section::Variables, scope.variablesReference, 2);
                }
            }
        }
    }

    // Watches outlive a session, so unlike the two above this section is
    // always present -- without one, a restored watch list has nowhere to
    // be seen until something is running.
    const std::vector<std::string> watches = dapManager_.Watches();
    pushHeader(Section::Watches);
    if (open(Section::Watches)) {
        if (watches.empty()) {
            pushPlaceholder(Section::Watches);
        }
        for (std::size_t i = 0; i < watches.size(); ++i) {
            rows_.push_back(Row{.kind = Row::Kind::Watch, .section = Section::Watches, .key = watches[i], .index = i});
        }
    }

    // Source breakpoints, grouped by file -- AllBreakpoints() is already
    // keyed by normalized path and sorted by line within each.
    const auto sourceBreakpoints = dapManager_.AllBreakpoints();
    pushHeader(Section::SourceBreakpoints);
    if (open(Section::SourceBreakpoints)) {
        if (sourceBreakpoints.empty()) {
            pushPlaceholder(Section::SourceBreakpoints);
        }
        for (const auto& [pathKey, breakpoints] : sourceBreakpoints) {
            rows_.push_back(Row{.kind = Row::Kind::SourceFile, .section = Section::SourceBreakpoints, .key = pathKey});
            if (collapsedFiles_.contains(pathKey)) {
                continue;
            }
            for (const auto& breakpoint : breakpoints) {
                rows_.push_back(Row{.kind    = Row::Kind::SourceBreakpoint,
                                    .section = Section::SourceBreakpoints,
                                    .key     = pathKey,
                                    .line    = breakpoint.line});
            }
        }
    }

    const auto& functionBreakpoints = dapManager_.FunctionBreakpoints();
    pushHeader(Section::FunctionBreakpoints);
    if (open(Section::FunctionBreakpoints)) {
        if (functionBreakpoints.empty()) {
            pushPlaceholder(Section::FunctionBreakpoints);
        }
        for (const auto& breakpoint : functionBreakpoints) {
            rows_.push_back(
                Row{.kind = Row::Kind::FunctionBreakpoint, .section = Section::FunctionBreakpoints, .key = breakpoint.name});
        }
    }

    const auto& dataBreakpoints = dapManager_.DataBreakpoints();
    pushHeader(Section::DataBreakpoints);
    if (open(Section::DataBreakpoints)) {
        if (dataBreakpoints.empty()) {
            pushPlaceholder(Section::DataBreakpoints);
        }
        for (std::size_t i = 0; i < dataBreakpoints.size(); ++i) {
            rows_.push_back(Row{.kind    = Row::Kind::DataBreakpoint,
                                .section = Section::DataBreakpoints,
                                .key     = dataBreakpoints[i].dataId,
                                .index   = i});
        }
    }

    // Both inventory sections are shown only when the adapter answered
    // them at all -- an empty "Loaded sources" would otherwise claim the
    // debuggee loaded none, when the truth is that this adapter doesn't
    // implement the request.
    if (!loadedSources_.empty()) {
        pushHeader(Section::LoadedSources);
        if (open(Section::LoadedSources)) {
            for (std::size_t i = 0; i < loadedSources_.size(); ++i) {
                rows_.push_back(Row{.kind    = Row::Kind::LoadedSource,
                                    .section = Section::LoadedSources,
                                    .key     = loadedSources_[i].name,
                                    .index   = i});
            }
        }
    }
    if (!modules_.empty()) {
        pushHeader(Section::Modules);
        if (open(Section::Modules)) {
            for (std::size_t i = 0; i < modules_.size(); ++i) {
                rows_.push_back(
                    Row{.kind = Row::Kind::Module, .section = Section::Modules, .key = modules_[i].name, .index = i});
            }
        }
    }

    // Exception filters exist only once an adapter has advertised them --
    // unlike the stores above, this section is genuinely empty before the
    // first session rather than merely unpopulated.
    const auto& filters = dapManager_.AvailableExceptionFilters();
    pushHeader(Section::ExceptionFilters);
    if (open(Section::ExceptionFilters)) {
        if (filters.empty()) {
            pushPlaceholder(Section::ExceptionFilters);
        }
        for (const auto& filter : filters) {
            rows_.push_back(Row{.kind = Row::Kind::ExceptionFilter, .section = Section::ExceptionFilters, .key = filter.id});
        }
    }
}

// ------------------------------------------------------------------ render

void DebugPanel::PushModel() {
    TreeViewModel model;
    model.title = "Debug";
    model.rows.reserve(rows_.size());

    const auto& dataBreakpoints = dapManager_.DataBreakpoints();
    const auto& enabledFilters  = dapManager_.EnabledExceptionFilters();
    const auto  sourceStore     = dapManager_.AllBreakpoints();

    std::size_t sourceCount = 0;
    for (const auto& [pathKey, entries] : sourceStore) {
        sourceCount += entries.size();
    }

    // A variable row's depth is its position in the same recursion
    // AppendVariableRows walked: each row sits one deeper than the
    // container it came from, and a container is always an earlier row.
    std::map<int, std::size_t> depthForReference;

    for (const Row& row : rows_) {
        TreeRow out;
        switch (row.kind) {
            case Row::Kind::SectionHeader: {
                std::size_t count = 0;
                switch (row.section) {
                    case Section::CallStack:
                        count = threads_.size();
                        break;
                    case Section::Variables:
                        count = scopes_.size();
                        break;
                    case Section::Watches:
                        count = dapManager_.Watches().size();
                        break;
                    case Section::SourceBreakpoints:
                        count = sourceCount;
                        break;
                    case Section::FunctionBreakpoints:
                        count = dapManager_.FunctionBreakpoints().size();
                        break;
                    case Section::DataBreakpoints:
                        count = dataBreakpoints.size();
                        break;
                    case Section::ExceptionFilters:
                        count = dapManager_.AvailableExceptionFilters().size();
                        break;
                    case Section::LoadedSources:
                        count = loadedSources_.size();
                        break;
                    case Section::Modules:
                        count = modules_.size();
                        break;
                }
                out.label           = TitleFor(static_cast<int>(row.section));
                out.depth           = 0;
                out.hasChildren     = true;
                out.expanded        = !collapsedSections_.contains(row.section);
                out.right           = count > 0 ? std::to_string(count) : std::string();
                out.rightForeground = theme_.indentGuideForeground;
                break;
            }
            case Row::Kind::Thread: {
                const bool focused  = row.id == dapManager_.FocusedThreadId();
                out.kindGlyph       = focused ? "→" : " ";
                out.kindForeground  = theme_.executionMarker;
                out.label           = row.key;
                out.depth           = 1;
                out.hasChildren     = true;
                out.expanded        = expandedThreads_.contains(row.id);
                out.loading         = out.expanded && !frames_.contains(row.id);
                out.right           = "#" + std::to_string(row.id);
                out.rightForeground = theme_.indentGuideForeground;
                break;
            }
            case Row::Kind::Frame: {
                const std::filesystem::path* path = nullptr;
                std::size_t                  line = 0;
                for (const auto& [threadId, frames] : frames_) {
                    const auto it =
                        std::find_if(frames.begin(), frames.end(), [&row](const auto& frame) { return frame.id == row.id; });
                    if (it != frames.end()) {
                        path = it->path ? &*it->path : nullptr;
                        line = it->line;
                        break;
                    }
                }
                const auto focusedFrame = dapManager_.FocusedFrameId();
                const bool isFocused    = focusedFrame && *focusedFrame == row.id;
                out.kindGlyph           = isFocused ? "▸" : " ";
                out.kindForeground      = theme_.executionMarker;
                out.label               = row.key;
                out.depth               = 2;
                out.hasChildren         = false;
                if (path != nullptr) {
                    out.right = path->filename().string() + ":" + std::to_string(line);
                }
                else {
                    // A frame with no source is real and common (a libc
                    // frame, a JIT frame) -- saying so beats a blank column.
                    out.right           = "no source";
                    out.labelForeground = theme_.indentGuideForeground;
                }
                out.rightForeground = theme_.indentGuideForeground;
                break;
            }
            case Row::Kind::Scope: {
                out.label       = row.key;
                out.depth       = 1;
                out.hasChildren = row.childrenReference != 0;
                out.expanded    = expandedVariables_.contains(row.childrenReference);
                out.loading     = out.expanded && !variables_.contains(row.childrenReference);
                if (row.childrenReference != 0) {
                    depthForReference[row.childrenReference] = 2;
                }
                break;
            }
            case Row::Kind::Variable: {
                const auto container = depthForReference.find(row.containerReference);
                out.depth            = container != depthForReference.end() ? container->second : 2;
                if (row.childrenReference != 0) {
                    depthForReference[row.childrenReference] = out.depth + 1;
                }
                const auto stored = variables_.find(row.containerReference);
                if (stored == variables_.end() || row.index >= stored->second.size()) {
                    continue;
                }
                const editor::dap::Manager::Variable& variable = stored->second[row.index];
                out.label                                      = variable.name;
                out.hasChildren                                = variable.variablesReference != 0;
                out.expanded                                   = expandedVariables_.contains(variable.variablesReference);
                out.loading                                    = out.expanded && !variables_.contains(variable.variablesReference);
                out.right                                      = OneLine(variable.value);
                out.rightForeground                            = theme_.indentGuideForeground;
                break;
            }
            case Row::Kind::Watch: {
                out.label       = row.key;
                out.depth       = 1;
                out.hasChildren = false;
                if (row.index < watchValues_.size() && !watchValues_[row.index].empty()) {
                    out.right = OneLine(watchValues_[row.index]);
                }
                else {
                    // Not "no value" -- the expression is fine, there is
                    // just nothing stopped to evaluate it against.
                    out.right           = "--";
                    out.labelForeground = theme_.indentGuideForeground;
                }
                out.rightForeground = theme_.indentGuideForeground;
                break;
            }
            case Row::Kind::SourceFile: {
                const std::filesystem::path path(row.key);
                out.label       = path.filename().string();
                out.depth       = 1;
                out.hasChildren = true;
                out.expanded    = !collapsedFiles_.contains(row.key);
                // The directory, not the count: two same-named files in
                // different directories is the case this section actually
                // has to disambiguate.
                out.right           = path.parent_path().filename().string();
                out.rightForeground = theme_.indentGuideForeground;
                break;
            }
            case Row::Kind::SourceBreakpoint: {
                const auto breakpoints = dapManager_.BreakpointsForKey(row.key);
                const auto it          = std::find_if(breakpoints.begin(), breakpoints.end(),
                                                      [&row](const auto& bp) { return bp.line == row.line; });
                if (it == breakpoints.end()) {
                    continue; // removed between BuildRows and here -- nothing to draw
                }
                out.kindGlyph      = BreakpointGlyph(*it);
                out.kindForeground = BreakpointGlyphColor(theme_, it->verified, it->enabled);
                out.label          = std::to_string(it->line) + QualifierSuffix(*it);
                out.depth          = 2;
                out.hasChildren    = false;
                if (!it->enabled) {
                    out.labelForeground = theme_.indentGuideForeground;
                }
                // Said in words, not only in colour: TreeView's selection
                // brush deliberately wins over a row's own colours, so the
                // row being acted on is exactly the one whose state a
                // colour alone cannot show.
                if (!it->enabled) {
                    out.right           = "off";
                    out.rightForeground = theme_.indentGuideForeground;
                    break;
                }
                // The adapter's snapped line when it moved the breakpoint,
                // else its unverified complaint -- both are facts about
                // this one row that the gutter can only express as a color.
                if (it->actualLine != 0 && it->actualLine != it->line) {
                    out.right           = "→" + std::to_string(it->actualLine);
                    out.rightForeground = theme_.indentGuideForeground;
                }
                else if (!it->verified) {
                    out.right           = "unverified";
                    out.rightForeground = theme_.unverifiedBreakpointMarker;
                }
                break;
            }
            case Row::Kind::FunctionBreakpoint: {
                const auto& functionBreakpoints = dapManager_.FunctionBreakpoints();
                const auto  it                  = std::find_if(functionBreakpoints.begin(), functionBreakpoints.end(),
                                                               [&row](const auto& bp) { return bp.name == row.key; });
                if (it == functionBreakpoints.end()) {
                    continue;
                }
                out.kindGlyph      = "●";
                out.kindForeground = BreakpointGlyphColor(theme_, /*verified=*/true, it->enabled);
                out.label          = it->name;
                out.depth          = 1;
                out.hasChildren    = false;
                if (!it->enabled) {
                    out.labelForeground = theme_.indentGuideForeground;
                    out.right           = "off"; // see the source-breakpoint row above
                    out.rightForeground = theme_.indentGuideForeground;
                }
                break;
            }
            case Row::Kind::DataBreakpoint: {
                if (row.index >= dataBreakpoints.size()) {
                    continue;
                }
                const auto& breakpoint = dataBreakpoints[row.index];
                out.kindGlyph          = "◈"; // a watchpoint watches data, not a line -- its own mark
                out.kindForeground     = BreakpointGlyphColor(theme_, breakpoint.verified, breakpoint.enabled);
                out.label              = breakpoint.description;
                if (!breakpoint.accessType.empty()) {
                    out.label += " (" + breakpoint.accessType + ")";
                }
                out.depth       = 1;
                out.hasChildren = false;
                if (!breakpoint.enabled) {
                    out.labelForeground = theme_.indentGuideForeground;
                    out.right           = "off"; // see the source-breakpoint row above
                    out.rightForeground = theme_.indentGuideForeground;
                }
                else if (!breakpoint.verified) {
                    out.right           = breakpoint.message.empty() ? "unverified" : breakpoint.message;
                    out.rightForeground = theme_.unverifiedBreakpointMarker;
                }
                break;
            }
            case Row::Kind::ExceptionFilter: {
                const auto& filters = dapManager_.AvailableExceptionFilters();
                const auto  it =
                    std::find_if(filters.begin(), filters.end(), [&row](const auto& filter) { return filter.id == row.key; });
                if (it == filters.end()) {
                    continue;
                }
                const bool enabled = enabledFilters.contains(it->id);
                // A checkbox, not a breakpoint -- and unlike the three
                // stores above, its glyph already says on/off in shape
                // rather than in colour, so it needs no "off" text.
                out.kindGlyph      = enabled ? "▣" : "▢";
                out.kindForeground = BreakpointGlyphColor(theme_, /*verified=*/true, enabled);
                out.label          = it->label;
                out.depth          = 1;
                out.hasChildren    = false;
                if (!enabled) {
                    out.labelForeground = theme_.indentGuideForeground;
                }
                break;
            }
            case Row::Kind::LoadedSource: {
                if (row.index >= loadedSources_.size()) {
                    continue;
                }
                const auto& source = loadedSources_[row.index];
                out.label          = source.name;
                out.depth          = 1;
                out.hasChildren    = false;
                if (source.path) {
                    out.right = source.path->parent_path().filename().string();
                }
                else {
                    // A source the adapter knows but that has no file --
                    // generated or in-memory. Enter has nothing to open.
                    out.right           = "no file";
                    out.labelForeground = theme_.indentGuideForeground;
                }
                out.rightForeground = theme_.indentGuideForeground;
                break;
            }
            case Row::Kind::Module: {
                if (row.index >= modules_.size()) {
                    continue;
                }
                const auto& module = modules_[row.index];
                out.label          = module.name;
                out.depth          = 1;
                out.hasChildren    = false;
                // Symbol status is the whole reason to look at this list: a
                // module without symbols is why a breakpoint inside it
                // never binds.
                out.right           = module.symbolStatus;
                out.rightForeground = theme_.indentGuideForeground;
                if (!module.isUserCode) {
                    out.labelForeground = theme_.indentGuideForeground;
                }
                break;
            }
            case Row::Kind::Placeholder:
                out.label           = "(none)";
                out.depth           = 1;
                out.hasChildren     = false;
                out.labelForeground = theme_.indentGuideForeground;
                break;
        }
        model.rows.push_back(std::move(out));
    }

    if (!model.rows.empty()) {
        model.selectedIndex = std::min(selectedIndex_, model.rows.size() - 1);
    }
    tree_.SetModel(std::move(model));
}

// ---------------------------------------------------------- expand/collapse

void DebugPanel::SetSectionCollapsed(Section section, bool collapsed) {
    if (collapsed) {
        collapsedSections_.insert(section);
    }
    else {
        collapsedSections_.erase(section);
    }
    Refresh();
}

void DebugPanel::HandleToggleExpand(std::size_t index) {
    if (index >= rows_.size()) {
        return;
    }
    const Row row = rows_[index]; // by value: Refresh below rebuilds rows_
    switch (row.kind) {
        case Row::Kind::SectionHeader:
            SetSectionCollapsed(row.section, false);
            return;
        case Row::Kind::SourceFile:
            collapsedFiles_.erase(row.key);
            Refresh();
            return;
        case Row::Kind::Thread:
            expandedThreads_.insert(row.id);
            if (!frames_.contains(row.id)) {
                FetchFramesFor(row.id);
            }
            Refresh();
            return;
        case Row::Kind::Scope:
        case Row::Kind::Variable:
            if (row.childrenReference == 0) {
                return;
            }
            expandedVariables_.insert(row.childrenReference);
            if (!variables_.contains(row.childrenReference)) {
                FetchVariablesFor(row.childrenReference);
            }
            Refresh();
            return;
        default:
            return;
    }
}

void DebugPanel::HandleCollapse(std::size_t index) {
    if (index >= rows_.size()) {
        return;
    }
    const Row row = rows_[index];
    switch (row.kind) {
        case Row::Kind::SectionHeader:
            SetSectionCollapsed(row.section, true);
            return;
        case Row::Kind::SourceFile:
            collapsedFiles_.insert(row.key);
            Refresh();
            return;
        case Row::Kind::Thread:
            expandedThreads_.erase(row.id);
            Refresh();
            return;
        case Row::Kind::Scope:
        case Row::Kind::Variable:
            // The fetched children stay cached -- re-opening the same node
            // within one stop costs nothing, and the whole cache is dropped
            // at the next state change anyway.
            expandedVariables_.erase(row.childrenReference);
            Refresh();
            return;
        default:
            return;
    }
}

// ----------------------------------------------------------------- actions

void DebugPanel::HandleActivate(std::size_t index) {
    if (index >= rows_.size()) {
        return;
    }
    selectedIndex_ = index;
    const Row row  = rows_[index];
    switch (row.kind) {
        case Row::Kind::SectionHeader:
        case Row::Kind::SourceFile:
        case Row::Kind::Thread:
        case Row::Kind::Scope:
            // Enter on anything with children is the expand/collapse
            // gesture, matching what a click on its disclosure glyph does.
            if ((row.kind == Row::Kind::SectionHeader && !collapsedSections_.contains(row.section)) ||
                (row.kind == Row::Kind::SourceFile && !collapsedFiles_.contains(row.key)) ||
                (row.kind == Row::Kind::Thread && expandedThreads_.contains(row.id)) ||
                (row.kind == Row::Kind::Scope && expandedVariables_.contains(row.childrenReference))) {
                HandleCollapse(index);
            }
            else {
                HandleToggleExpand(index);
            }
            return;
        case Row::Kind::Variable:
            if (row.childrenReference != 0) {
                if (expandedVariables_.contains(row.childrenReference)) {
                    HandleCollapse(index);
                }
                else {
                    HandleToggleExpand(index);
                }
                return;
            }
            Report("Press 'c' to change this variable's value, 'w' to watch it.");
            return;
        case Row::Kind::Frame: {
            // Selecting a frame re-scopes every evaluation to it -- watches
            // included -- which is the difference between a stack listing
            // and a stack you can actually inspect from.
            dapManager_.SelectFrame(row.id);
            scopes_.clear();
            variables_.clear();
            FetchScopesFor(row.id);
            FetchWatchValues();
            for (const auto& [threadId, frames] : frames_) {
                const auto it = std::find_if(frames.begin(), frames.end(), [&row](const auto& f) { return f.id == row.id; });
                if (it != frames.end() && it->path && onVisitLocation_) {
                    onVisitLocation_(*it->path, it->line);
                    break;
                }
            }
            Refresh();
            return;
        }
        case Row::Kind::SourceBreakpoint: {
            const auto breakpoints = dapManager_.BreakpointsForKey(row.key);
            const auto it          = std::find_if(breakpoints.begin(), breakpoints.end(),
                                                  [&row](const auto& bp) { return bp.line == row.line; });
            // The line to visit is where the breakpoint actually landed, not
            // where it was requested -- the same display rule the gutter
            // follows (Breakpoint::actualLine).
            const std::size_t line = (it != breakpoints.end() && it->actualLine != 0) ? it->actualLine : row.line;
            if (onVisitLocation_) {
                onVisitLocation_(std::filesystem::path(row.key), line);
            }
            return;
        }
        case Row::Kind::ExceptionFilter:
            // Enter and Space agree here: a filter has nothing to open.
            ToggleEnabledAt(index);
            return;
        case Row::Kind::Watch:
            Report("Press 'c' to edit this watch, 'd' to remove it.");
            return;
        case Row::Kind::LoadedSource: {
            if (row.index < loadedSources_.size() && loadedSources_[row.index].path && onVisitLocation_) {
                onVisitLocation_(*loadedSources_[row.index].path, 1);
                return;
            }
            Report("That source has no file to open.");
            return;
        }
        case Row::Kind::Module: {
            // A module names a binary, not a source -- opening it would be
            // opening an object file. Its path is still worth reporting,
            // since "which build is this" is usually the question.
            if (row.index < modules_.size()) {
                const auto& module = modules_[row.index];
                Report(module.path.empty() ? (module.name + " (no path reported)") : module.path);
            }
            return;
        }
        case Row::Kind::FunctionBreakpoint:
        case Row::Kind::DataBreakpoint:
            // Neither names a location -- that is the whole reason they have
            // no gutter representation and need this panel.
            Report(row.kind == Row::Kind::FunctionBreakpoint ? "Function breakpoints name no line to visit."
                                                             : "Data breakpoints name no line to visit.");
            return;
        case Row::Kind::Placeholder:
            return;
    }
}

void DebugPanel::ToggleEnabledAt(std::size_t index) {
    if (index >= rows_.size()) {
        return;
    }
    const Row row = rows_[index];
    switch (row.kind) {
        case Row::Kind::SourceBreakpoint: {
            const auto breakpoints = dapManager_.BreakpointsForKey(row.key);
            const auto it          = std::find_if(breakpoints.begin(), breakpoints.end(),
                                                  [&row](const auto& bp) { return bp.line == row.line; });
            if (it == breakpoints.end()) {
                return;
            }
            const bool nowEnabled = !it->enabled;
            dapManager_.SetBreakpointEnabled(std::filesystem::path(row.key), row.line, nowEnabled);
            Report(nowEnabled ? "Breakpoint enabled." : "Breakpoint disabled.");
            return;
        }
        case Row::Kind::FunctionBreakpoint: {
            const auto& functionBreakpoints = dapManager_.FunctionBreakpoints();
            const auto  it                  = std::find_if(functionBreakpoints.begin(), functionBreakpoints.end(),
                                                           [&row](const auto& bp) { return bp.name == row.key; });
            if (it == functionBreakpoints.end()) {
                return;
            }
            const bool nowEnabled = !it->enabled;
            dapManager_.SetFunctionBreakpointEnabled(row.key, nowEnabled);
            Report(nowEnabled ? "Function breakpoint enabled." : "Function breakpoint disabled.");
            return;
        }
        case Row::Kind::DataBreakpoint: {
            const auto& dataBreakpoints = dapManager_.DataBreakpoints();
            if (row.index >= dataBreakpoints.size()) {
                return;
            }
            const bool nowEnabled = !dataBreakpoints[row.index].enabled;
            dapManager_.SetDataBreakpointEnabled(row.index, nowEnabled);
            Report(nowEnabled ? "Data breakpoint enabled." : "Data breakpoint disabled.");
            return;
        }
        case Row::Kind::ExceptionFilter: {
            std::set<std::string> enabled    = dapManager_.EnabledExceptionFilters();
            const bool            nowEnabled = !enabled.contains(row.key);
            if (nowEnabled) {
                enabled.insert(row.key);
            }
            else {
                enabled.erase(row.key);
            }
            dapManager_.SetExceptionBreakpointFilters(std::move(enabled));
            Report(nowEnabled ? "Exception filter enabled." : "Exception filter disabled.");
            return;
        }
        default:
            Report("Nothing to enable or disable on this row.");
            return;
    }
}

void DebugPanel::RemoveAt(std::size_t index) {
    if (index >= rows_.size()) {
        return;
    }
    const Row row = rows_[index];
    switch (row.kind) {
        case Row::Kind::SourceBreakpoint:
            dapManager_.RemoveBreakpoint(std::filesystem::path(row.key), row.line);
            Report("Breakpoint removed.");
            return;
        case Row::Kind::FunctionBreakpoint:
            dapManager_.RemoveFunctionBreakpoint(row.key);
            Report("Function breakpoint removed.");
            return;
        case Row::Kind::DataBreakpoint:
            dapManager_.RemoveDataBreakpointAt(row.index);
            Report("Data breakpoint removed.");
            return;
        case Row::Kind::Watch:
            dapManager_.RemoveWatchAt(row.index);
            FetchWatchValues();
            Refresh();
            Report("Watch removed.");
            return;
        case Row::Kind::SourceFile: {
            // A file row's own removal is every breakpoint under it -- the
            // "I am done with this file" gesture the per-line loop is the
            // wrong tool for.
            for (const auto& breakpoint : dapManager_.BreakpointsForKey(row.key)) {
                dapManager_.RemoveBreakpoint(std::filesystem::path(row.key), breakpoint.line);
            }
            Report("File's breakpoints removed.");
            return;
        }
        case Row::Kind::ExceptionFilter:
            // An adapter owns this list; only its enabled-ness is ours.
            Report("An exception filter is the adapter's -- disable it with Space instead.");
            return;
        default:
            Report("Nothing to remove on this row.");
            return;
    }
}

void DebugPanel::EditAt(std::size_t index) {
    if (index >= rows_.size()) {
        Report("Nothing to edit on this row.");
        return;
    }
    const Row row = rows_[index];
    switch (row.kind) {
        case Row::Kind::SourceBreakpoint: {
            const auto breakpoints = dapManager_.BreakpointsForKey(row.key);
            const auto it          = std::find_if(breakpoints.begin(), breakpoints.end(),
                                                  [&row](const auto& bp) { return bp.line == row.line; });
            Prompt("Condition (empty to clear)", it != breakpoints.end() ? it->condition : std::string(),
                   [this, path = std::filesystem::path(row.key), line = row.line](std::string condition) {
                       Report(dapManager_.SetBreakpointCondition(path, line, std::move(condition)));
                   });
            return;
        }
        case Row::Kind::Watch:
            Prompt("Watch expression", row.key, [this, index = row.index](std::string expression) {
                if (expression.empty()) {
                    Report("No expression given.");
                    return;
                }
                // Removed and re-added at the same index would be simpler,
                // but AddWatch appends -- so this deliberately accepts that
                // an edited watch moves to the end of the list.
                dapManager_.RemoveWatchAt(index);
                dapManager_.AddWatch(std::move(expression));
                FetchWatchValues();
                Refresh();
            });
            return;
        case Row::Kind::Variable:
            AssignAt(index);
            return;
        default:
            Report("Nothing to edit on this row.");
            return;
    }
}

void DebugPanel::AssignAt(std::size_t index) {
    if (index >= rows_.size()) {
        Report("Nothing to assign to on this row.");
        return;
    }
    const Row row = rows_[index];
    if (row.kind == Row::Kind::Watch) {
        // A watch has no container to name it in, so the only way to assign
        // to one is setExpression -- which is exactly the case DAP added
        // that request for.
        Prompt("New value for " + row.key, std::string(), [this, expression = row.key](std::string value) {
            dapManager_.SetExpression(expression, value,
                                      [this, expression](const editor::dap::Manager::SetVariableResult& result) {
                                          if (!result.success) {
                                              Report("Could not set " + expression + ": " + result.errorMessage);
                                              return;
                                          }
                                          Report(expression + " = " + result.value);
                                          FetchSessionData();
                                      });
        });
        return;
    }
    if (row.kind != Row::Kind::Variable) {
        Report("Nothing to assign to on this row.");
        return;
    }
    const auto stored = variables_.find(row.containerReference);
    if (stored == variables_.end() || row.index >= stored->second.size()) {
        return;
    }
    const editor::dap::Manager::Variable& variable = stored->second[row.index];
    Prompt("New value for " + row.key, variable.value,
           [this, container = row.containerReference, name = row.key, evaluateName = variable.evaluateName](std::string value) {
               auto report = [this, name](const editor::dap::Manager::SetVariableResult& result) {
                   if (!result.success) {
                       Report("Could not set " + name + ": " + result.errorMessage);
                       return;
                   }
                   Report(name + " = " + result.value);
                   // Everything is refetched rather than the one row
                   // patched: a single assignment can move siblings (a
                   // union, a proxy object) and can change what a watch
                   // evaluates to.
                   FetchSessionData();
               };
               // setVariable is the direct route and the one more adapters
               // implement; setExpression is the fallback, and needs the
               // adapter's own evaluateName because a nested field's bare
               // name is not an expression ("key" vs "node->key").
               if (dapManager_.SupportsSetVariable() || evaluateName.empty()) {
                   dapManager_.SetVariable(container, name, value, report);
                   return;
               }
               dapManager_.SetExpression(evaluateName, value, report);
           });
}

void DebugPanel::SetHitConditionAt(std::size_t index) {
    if (index >= rows_.size() || rows_[index].kind != Row::Kind::SourceBreakpoint) {
        Report("Select a source breakpoint first.");
        return;
    }
    const Row  row         = rows_[index];
    const auto breakpoints = dapManager_.BreakpointsForKey(row.key);
    const auto it          = std::find_if(breakpoints.begin(), breakpoints.end(), [&row](const auto& bp) { return bp.line == row.line; });
    Prompt("Hit condition (empty to clear)", it != breakpoints.end() ? it->hitCondition : std::string(),
           [this, path = std::filesystem::path(row.key), line = row.line](std::string hitCondition) {
               Report(dapManager_.SetBreakpointHitCondition(path, line, std::move(hitCondition)));
           });
}

void DebugPanel::SetLogMessageAt(std::size_t index) {
    if (index >= rows_.size() || rows_[index].kind != Row::Kind::SourceBreakpoint) {
        Report("Select a source breakpoint first.");
        return;
    }
    const Row  row         = rows_[index];
    const auto breakpoints = dapManager_.BreakpointsForKey(row.key);
    const auto it          = std::find_if(breakpoints.begin(), breakpoints.end(), [&row](const auto& bp) { return bp.line == row.line; });
    Prompt("Log message (empty to clear)", it != breakpoints.end() ? it->logMessage : std::string(),
           [this, path = std::filesystem::path(row.key), line = row.line](std::string logMessage) {
               Report(dapManager_.SetBreakpointLogMessage(path, line, std::move(logMessage)));
           });
}

void DebugPanel::AddFunctionBreakpointPrompt() {
    Prompt("Function breakpoint name", std::string(), [this](std::string name) {
        if (name.empty()) {
            Report("No function name given.");
            return;
        }
        const bool nowSet = dapManager_.ToggleFunctionBreakpoint(name);
        Report((nowSet ? "Function breakpoint added: " : "Function breakpoint removed: ") + name);
    });
}

void DebugPanel::AddWatchPrompt() {
    Prompt("Add watch", std::string(), [this](std::string expression) {
        if (expression.empty()) {
            Report("No expression given.");
            return;
        }
        dapManager_.AddWatch(expression);
        FetchWatchValues();
        Refresh();
        Report("Watch added: " + expression);
    });
}

void DebugPanel::WatchVariableAt(std::size_t index) {
    if (index >= rows_.size() || rows_[index].kind != Row::Kind::Variable) {
        // Anywhere else 'w' still means "add a watch" -- it just has to ask
        // what to watch.
        AddWatchPrompt();
        return;
    }
    // The expression that reproduces this variable, not its bare name: a
    // nested field's name means nothing outside its container.
    const Row   row    = rows_[index];
    const auto  stored = variables_.find(row.containerReference);
    std::string name   = row.key;
    if (stored != variables_.end() && row.index < stored->second.size() && !stored->second[row.index].evaluateName.empty()) {
        name = stored->second[row.index].evaluateName;
    }
    dapManager_.AddWatch(name);
    FetchWatchValues();
    Refresh();
    Report("Watch added: " + name);
}

void DebugPanel::ClearSectionAt(std::size_t index) {
    if (index >= rows_.size()) {
        return;
    }
    switch (rows_[index].section) {
        case Section::SourceBreakpoints:
            dapManager_.ClearSourceBreakpoints();
            Report("All source breakpoints removed.");
            return;
        case Section::FunctionBreakpoints:
            dapManager_.ClearFunctionBreakpoints();
            Report("All function breakpoints removed.");
            return;
        case Section::DataBreakpoints:
            dapManager_.ClearDataBreakpoints();
            Report("All data breakpoints removed.");
            return;
        case Section::ExceptionFilters:
            dapManager_.SetExceptionBreakpointFilters({});
            Report("All exception filters disabled.");
            return;
        case Section::Watches: {
            // Back to front: RemoveWatchAt is index-addressed, so removing
            // from the front would renumber everything still to go.
            const std::size_t count = dapManager_.Watches().size();
            for (std::size_t i = count; i > 0; --i) {
                dapManager_.RemoveWatchAt(i - 1);
            }
            FetchWatchValues();
            Refresh();
            Report("All watches removed.");
            return;
        }
        case Section::CallStack:
        case Section::Variables:
        case Section::LoadedSources:
        case Section::Modules:
            Report("This section is the adapter's -- nothing here to clear.");
            return;
    }
}

void DebugPanel::HandleKey(const editor::KeyChord& chord) {
    if (chord.Control || chord.Meta) {
        return; // the pane's own chords, not this panel's single-letter set
    }
    const std::size_t index = tree_.SelectedRow().value_or(selectedIndex_);
    switch (chord.Codepoint) {
        case U' ':
            ToggleEnabledAt(index);
            break;
        case U'd':
            RemoveAt(index);
            break;
        case U'c':
            EditAt(index);
            break;
        case U'h':
            SetHitConditionAt(index);
            break;
        case U'l':
            SetLogMessageAt(index);
            break;
        case U'a':
            AddFunctionBreakpointPrompt();
            break;
        case U'w':
            WatchVariableAt(index);
            break;
        case U'=':
            AssignAt(index);
            break;
        case U'X':
            ClearSectionAt(index);
            break;
        case U'g':
            // Re-asks the adapter as well as rebuilding -- the manual
            // escape hatch for anything the change feeds missed.
            FetchSessionData();
            break;
        default:
            return;
    }
}

} // namespace ned::ui
