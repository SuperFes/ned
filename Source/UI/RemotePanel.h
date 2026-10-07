//
// The left dock's Remote panel: each host the editor is connected to, and
// each remote root opened here, as a tree of that host's directories listed
// as they're expanded. A controller over a TreeView, like DebugPanel;
// main.cpp registers Tree() with the dock and routes the panel's requests
// (opening a file, prompting for a root) to the focused pane.
//
// A connected host with no root of its own shows its login's home. Roots
// stay listed while their host is disconnected; expanding one connects.
// Dotfiles show unless '.' hides them. n, +, R and D make a file or a
// directory beside the selection, or rename or delete it, through the
// focused pane's prompts. Its keys are remote-panel-mode's
// (Editor/PanelMode.h), so init.janet can rebind them. Expanded directories
// are watched on their hosts (ListedDirectories), and a change relists them.
//

#ifndef NED_UI_REMOTEPANEL_H
#define NED_UI_REMOTEPANEL_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Host/FsCodec.h"
#include "Editor/Key.h"
#include "Editor/Lifetime.h"
#include "Editor/Remote/Hosts.h"
#include "PanelKeys.h"
#include "Text/RemoteFile.h"
#include "Theme.h"
#include "TreeView.h"

namespace ned::ui {

class RemotePanel {
  public:
    // theme and hosts must outlive this panel.
    RemotePanel(const Theme& theme, editor::remote::Hosts& hosts);

    RemotePanel(const RemotePanel&)            = delete;
    RemotePanel& operator=(const RemotePanel&) = delete;

    [[nodiscard]] TreeView& Tree();

    // Rebuilds the rows from the roots and the hosts' connections, keeping
    // what's expanded and selected. Wire to Hosts::SetOnChanged.
    void Refresh();

    // Adds typed -- host:/dir, or a host alone for its login's home -- as a
    // root, connecting to resolve a home, and expands it. An open root is
    // only expanded again.
    void OpenRoot(const std::string& typed);

    [[nodiscard]] const std::vector<text::RemoteFile>& Roots() const;
    // Roots remembered from before; nothing connects until one is expanded.
    void SetRoots(std::vector<text::RemoteFile> roots);

    // Enter on a file.
    void SetOnOpenFile(std::function<void(const text::RemoteFile& file)> handler);
    // 'o': the panel has no prompt of its own.
    void SetOnRequestOpenRoot(std::function<void()> handler);
    // After a root is added or removed, to remember them.
    void SetOnRootsChanged(std::function<void()> handler);
    // Status-line feedback.
    void SetOnMessage(std::function<void(std::string)> handler);
    // Escape.
    void SetOnCancel(std::function<void()> handler);
    // A pending multi-key sequence's next keys, or nullopt to hide them.
    void SetOnPrefixHint(std::function<void(std::optional<WhichKeyHint>)> handler);

    enum class FileAction { NewFile,
                            NewDirectory,
                            Rename,
                            Delete };
    // n, +, R, D: the panel has no prompt of its own. A new file or
    // directory is offered the selected directory, or a file's own.
    void SetOnFileAction(std::function<void(FileAction action, const text::RemoteFile& target)> handler);

    // The host's tree changed at path: the directory holding it is listed
    // again wherever it's shown, keeping what's expanded under it.
    void Changed(const text::RemoteFile& path);

    // The directories shown expanded, each once, for their hosts to watch.
    [[nodiscard]] std::vector<text::RemoteFile> ListedDirectories() const;
    // Whenever ListedDirectories changes.
    void SetOnListedChanged(std::function<void()> handler);

  private:
    struct Node {
        // A home whose path the host hasn't resolved yet has an empty path.
        text::RemoteFile                 location;
        std::string                      name;
        editor::host::FileKind           kind     = editor::host::FileKind::Directory;
        bool                             link     = false;
        bool                             root     = false; // one of roots_, rather than a connected host's home
        bool                             expanded = false;
        bool                             loading  = false;
        std::optional<std::vector<Node>> children; // unset until listed
        std::string                      error;
        std::uint64_t                    listing = 0; // which listing an answer belongs to
    };
    struct Row {
        Node*       node;
        std::size_t depth;
    };

    [[nodiscard]] static std::string TopKey(const Node& node);
    [[nodiscard]] Node*              FindTop(const std::string& key);
    [[nodiscard]] Node*              FindNode(const std::string& topKey, const std::filesystem::path& path);
    [[nodiscard]] Node*              SelectedNode();
    [[nodiscard]] std::string        TopKeyOfRow(std::size_t index) const;
    [[nodiscard]] bool               Shown(const Node& node) const;

    // Selects the row at path under topKey, or else its nearest shown ancestor.
    void SelectNearest(const std::string& topKey, std::filesystem::path path);

    void Expand(Node& node);
    void Relist(const std::string& topKey, Node& node);
    void List(const std::string& topKey, const text::RemoteFile& directory, std::uint64_t listing);
    void Settle(const std::string& topKey, const std::filesystem::path& path, std::uint64_t listing, std::string error,
                std::optional<std::vector<editor::host::DirEntry>> entries);
    void HandleActivate(std::size_t index);
    void HandleCollapse(std::size_t index);
    // One of remote-panel-mode's commands, against the selected row.
    void Run(std::string_view command);
    void HandleFileAction(FileAction action, std::size_t depth, const Node& node);
    void Report(std::string message);
    void PushModel();

    const Theme&                  theme_;
    editor::remote::Hosts&        hosts_;
    TreeView                      tree_;
    PanelKeys                     keys_;
    std::vector<text::RemoteFile> roots_;
    std::vector<Node>             tops_;
    std::vector<Row>              rows_;
    std::size_t                   selectedIndex_ = 0;
    std::uint64_t                 nextListing_   = 0;
    bool                          showDotfiles_  = true;

    std::function<void(const text::RemoteFile&)> onOpenFile_;
    std::function<void()>                        onRequestOpenRoot_;
    std::function<void()>                        onRootsChanged_;
    std::function<void(std::string)>             onMessage_;
    std::function<void()>                        onCancel_;
    std::function<void(FileAction, const text::RemoteFile&)> onFileAction_;
    std::function<void()>                                    onListedChanged_;
    std::vector<text::RemoteFile>                            listed_;

    editor::LifetimeGuard lifetime_; // last, so it expires first
};

} // namespace ned::ui

#endif // NED_UI_REMOTEPANEL_H
