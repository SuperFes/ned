//
// debug-panel (ROADMAP.md's "a VcsPanel-style standing breakpoint panel
// covering all three stores"): a persistent, collapsible-section view of
// everything the debugger knows, hosted as a LeftDock panel beside
// ProjectSidebar and VcsPanel.
//
// Shape: DapThreadsPanel's own controller-plus-widget split (a plain class
// owning one focusable widget, exposed via Tree(), driven through Set*
// callbacks rather than a bespoke Paint()/OnEvent() override) -- but over
// TreeView rather than ListPopup, because this view is sections containing
// files containing breakpoints, not a flat list. Registration, focus return
// and the rail glyph are main.cpp's job, exactly as for the other two dock
// panels.
//
// One panel rather than one per store, for the same reason VS Code's own
// Run-and-Debug sidebar is one: a rail glyph per DAP concern would be four
// or more glyphs that say nothing whenever no session is live, and the
// sections share a single refresh path (Manager::SetOnBreakpointsChanged
// plus the stop hook) that would otherwise be wired four times.
//
// The breakpoint sections work with NO session at all -- that is the point.
// Line and function breakpoints are process-wide and persisted, so the panel
// is how you review and arm them before starting, which is also why its
// remove/enable/condition actions go straight to Manager rather than
// requiring a live adapter.
//

#ifndef NED_UI_DEBUGPANEL_H
#define NED_UI_DEBUGPANEL_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Editor/Dap/Manager.h"
#include "Editor/Key.h"
#include "Theme.h"
#include "TreeView.h"

namespace ned::ui {

class DebugPanel {
  public:
    // theme and dapManager must outlive this panel -- the usual contract for
    // a themed/manager-referencing widget here.
    DebugPanel(const Theme& theme, editor::dap::Manager& dapManager);

    // The Widget to register with LeftDock::AddPanel and to call TakeFocus()
    // on -- DapThreadsPanel::Popup()'s own "this class is a controller, not a
    // Widget" shape.
    [[nodiscard]] TreeView& Tree();

    // Rebuilds every section from the stores, preserving the selected row
    // where it still exists. Safe (and cheap) to call when hidden: wired to
    // Manager::SetOnBreakpointsChanged and to the stop hook, neither of
    // which knows whether this panel is on screen.
    void Refresh();

    // Fired on Enter over a breakpoint that names a file -- the caller opens
    // it and moves point, the same contract ProjectSidebar's own open
    // callback has. line is 1-based.
    void SetOnVisitLocation(std::function<void(const std::filesystem::path&, std::size_t line)> handler);

    // Fired when a key needs text entry (a condition, a function name, a
    // new variable value): this widget has no minibuffer of its own, the
    // same reason VcsPanel reports its commit/branch actions rather than
    // prompting for them. What the text MEANS stays here, in the accept
    // callback -- the pane only runs the prompt. main.cpp routes this to
    // BufferView::BeginDebugPanelTextEntry.
    void SetOnTextEntryRequest(
        std::function<void(std::string label, std::string initialText, std::function<void(std::string)> onAccept)> handler);

    // Status-line feedback, BufferListPanel::SetOnMessage's own contract.
    void SetOnMessage(std::function<void(std::string)> handler);

    // Escape/C-g -- the caller returns focus to the pane (LeftDock's own
    // NoteFocusReturned pairing lives at the wiring site, ProjectSidebar's
    // precedent).
    void SetOnCancel(std::function<void()> handler);

    // The session's state changed: re-fetches everything that is only
    // meaningful while stopped (threads, frames, scopes, variables, watch
    // values) and drops it all when it isn't. Wired to
    // Manager::SetOnSessionStateChanged; also safe to call directly, which
    // is what 'g' does.
    void NotifySessionStateChanged();

  private:
    // In display order. The session-scoped sections come first -- while
    // something is actually stopped, the stack and its values are what the
    // panel is for; the breakpoint stores are the standing content
    // underneath them and are the only sections shown when nothing is
    // running. Watches sit with the session sections but are shown
    // regardless, because watch expressions outlive a session (see
    // Manager::RestoreWatches).
    enum class Section { CallStack,
                         Variables,
                         Watches,
                         SourceBreakpoints,
                         FunctionBreakpoints,
                         DataBreakpoints,
                         ExceptionFilters,
                         // Inventory, not state: what the debuggee actually
                         // loaded. Last because it is reference material --
                         // consulted when a breakpoint won't bind, not
                         // watched while stepping. Only present when the
                         // adapter advertises the request at all.
                         LoadedSources,
                         Modules };

    // What one flattened TreeView row points at. Index-parallel to the
    // model's own rows, DapThreadsPanel::rows_'s own arrangement.
    struct Row {
        enum class Kind { SectionHeader,
                          SourceFile,
                          SourceBreakpoint,
                          FunctionBreakpoint,
                          DataBreakpoint,
                          ExceptionFilter,
                          Thread,
                          Frame,
                          Scope,
                          Variable,
                          Watch,
                          LoadedSource,
                          Module,
                          Placeholder }; // "(none)" -- selectable but inert

        Kind        kind    = Kind::Placeholder;
        Section     section = Section::SourceBreakpoints;
        std::string key;       // SourceFile/SourceBreakpoint: the normalized path key; FunctionBreakpoint: the name; ExceptionFilter: the filter id; Variable: its name
        std::size_t line  = 0; // SourceBreakpoint, the requested line
        std::size_t index = 0; // DataBreakpoint: into DataBreakpoints(); Watch: into Watches(); Variable: into its parent's own child list
        int         id    = 0; // Thread: the thread id; Frame: the frame id
        // Variable/Scope: the reference this row's own children live under
        // (0 for a leaf), and for a Variable the reference of the container
        // holding it -- setVariable addresses a variable by its CONTAINER
        // plus its name, never by its own reference.
        int childrenReference  = 0;
        int containerReference = 0;

        bool operator==(const Row&) const = default;
    };

    const Theme&          theme_;
    editor::dap::Manager& dapManager_;
    TreeView              tree_;

    std::vector<Row> rows_;
    std::size_t      selectedIndex_ = 0;

    // Collapse is the panel's own state, not the model's: a section stays
    // shut across the refresh a stop event triggers. Stored as the set of
    // CLOSED things so a newly-appearing section (a file that just got its
    // first breakpoint) is open by default.
    std::set<Section>     collapsedSections_;
    std::set<std::string> collapsedFiles_;
    // Which threads' frames and which composite variables are open. Unlike
    // the two above these are "expanded" sets, not "collapsed" ones: a
    // thread's frames and a variable's fields cost a request each, so
    // nothing is open until asked for.
    std::set<int> expandedThreads_;
    std::set<int> expandedVariables_;

    // Everything below is one stop's worth of answers, dropped wholesale
    // the moment the session leaves Stopped -- see NotifySessionStateChanged.
    std::vector<editor::dap::Manager::Thread>                    threads_;
    std::map<int, std::vector<editor::dap::Manager::StackFrame>> frames_;      // by thread id
    std::vector<editor::dap::Manager::Scope>                     scopes_;      // of the focused frame
    std::map<int, std::vector<editor::dap::Manager::Variable>>   variables_;   // by variables reference
    std::vector<std::string>                                     watchValues_; // index-parallel to Manager::Watches()
    std::vector<editor::dap::Manager::LoadedSource>              loadedSources_;
    std::vector<editor::dap::Manager::Module>                    modules_;

    // Every in-flight request carries the generation it was issued under;
    // an answer from a previous stop is dropped rather than painted over
    // the current one. One counter covers all of them because they are all
    // invalidated by the same event.
    std::uint64_t generation_ = 0;

    std::function<void(const std::filesystem::path&, std::size_t)>                  onVisitLocation_;
    std::function<void(std::string, std::string, std::function<void(std::string)>)> onTextEntryRequest_;
    std::function<void(std::string)>                                                onMessage_;
    std::function<void()>                                                           onCancel_;

    void BuildRows();
    void PushModel();
    void Report(std::string message);

    void HandleActivate(std::size_t index);
    void HandleToggleExpand(std::size_t index);
    void HandleCollapse(std::size_t index);
    void HandleKey(const editor::KeyChord& chord);

    // Key actions, each a no-op reporting why when the selected row is the
    // wrong kind for it.
    void ToggleEnabledAt(std::size_t index);
    void RemoveAt(std::size_t index);
    void ClearSectionAt(std::size_t index);
    void EditAt(std::size_t index); // 'c' -- condition, or a variable's value, by row kind
    void SetHitConditionAt(std::size_t index);
    void SetLogMessageAt(std::size_t index);
    void AddFunctionBreakpointPrompt();
    void AddWatchPrompt();
    void WatchVariableAt(std::size_t index);
    void AssignAt(std::size_t index); // '=' -- assign to a watch, or to a variable, by row kind

    void SetSectionCollapsed(Section section, bool collapsed);
    void Prompt(std::string label, std::string initialText, std::function<void(std::string)> onAccept);

    // Re-issues every session-scoped request under a fresh generation. A
    // no-op beyond clearing when the session isn't stopped.
    void FetchSessionData();
    void FetchFramesFor(int threadId);
    void FetchScopesFor(int frameId);
    void FetchVariablesFor(int variablesReference);
    void FetchWatchValues();
    void FetchInventory(); // loadedSources + modules, both capability-gated inside Manager

    // Appends a variable subtree's visible rows, recursing through
    // expandedVariables_.
    void AppendVariableRows(Section section, int variablesReference, std::size_t depth);
};

} // namespace ned::ui

#endif // NED_UI_DEBUGPANEL_H
