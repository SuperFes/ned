#include "RemotePanel.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

#include "Editor/FileIcons.h"
#include "Editor/Remote/Fs.h"

namespace ned::ui {

namespace {

    // Plenty for a sidebar; a bigger directory says so rather than filling it.
    constexpr std::size_t kMaxListedEntries = 50000;

} // namespace

RemotePanel::RemotePanel(const Theme& theme, editor::remote::Hosts& hosts) : theme_(theme), hosts_(hosts), tree_(theme), keys_(editor::RemotePanelMode()) {
    tree_.SetOnSelectionChanged([this](std::size_t index) { selectedIndex_ = index; });
    tree_.SetOnActivate([this](std::size_t index) { HandleActivate(index); });
    tree_.SetOnToggleExpand([this](std::size_t index) {
        if (index < rows_.size()) {
            Expand(*rows_[index].node);
        }
    });
    tree_.SetOnCollapseRequested([this](std::size_t index) { HandleCollapse(index); });
    tree_.SetOnKey([this](const editor::KeyChord& chord) {
        if (const std::optional<std::string> command = keys_.Feed(chord)) {
            Run(*command);
        }
    });
    keys_.SetOnMessage([this](std::string message) { Report(std::move(message)); });
    tree_.SetOnCancel([this] {
        keys_.Reset();
        if (onCancel_) {
            onCancel_();
        }
    });
    Refresh();
}

TreeView& RemotePanel::Tree() {
    return tree_;
}

const std::vector<text::RemoteFile>& RemotePanel::Roots() const {
    return roots_;
}

void RemotePanel::SetRoots(std::vector<text::RemoteFile> roots) {
    roots_ = std::move(roots);
    Refresh();
}

void RemotePanel::SetOnOpenFile(std::function<void(const text::RemoteFile&)> handler) {
    onOpenFile_ = std::move(handler);
}

void RemotePanel::SetOnRequestOpenRoot(std::function<void()> handler) {
    onRequestOpenRoot_ = std::move(handler);
}

void RemotePanel::SetOnRootsChanged(std::function<void()> handler) {
    onRootsChanged_ = std::move(handler);
}

void RemotePanel::SetOnMessage(std::function<void(std::string)> handler) {
    onMessage_ = std::move(handler);
}

void RemotePanel::SetOnCancel(std::function<void()> handler) {
    onCancel_ = std::move(handler);
}

void RemotePanel::SetOnPrefixHint(std::function<void(std::optional<WhichKeyHint>)> handler) {
    keys_.SetOnHint(std::move(handler));
}

void RemotePanel::SetOnFileAction(std::function<void(FileAction, const text::RemoteFile&)> handler) {
    onFileAction_ = std::move(handler);
}

void RemotePanel::Report(std::string message) {
    if (onMessage_) {
        onMessage_(std::move(message));
    }
}

std::string RemotePanel::TopKey(const Node& node) {
    return node.root ? node.location.ToString() : node.location.destination;
}

RemotePanel::Node* RemotePanel::FindTop(const std::string& key) {
    const auto it = std::ranges::find(tops_, key, &RemotePanel::TopKey);
    return it != tops_.end() ? &*it : nullptr;
}

RemotePanel::Node* RemotePanel::FindNode(const std::string& topKey, const std::filesystem::path& path) {
    Node* top = FindTop(topKey);
    if (top == nullptr) {
        return nullptr;
    }
    std::vector<Node*> pending{top};
    while (!pending.empty()) {
        Node* node = pending.back();
        pending.pop_back();
        if (node->location.path == path) {
            return node;
        }
        if (node->children) {
            for (Node& child : *node->children) {
                pending.push_back(&child);
            }
        }
    }
    return nullptr;
}

std::string RemotePanel::TopKeyOfRow(std::size_t index) const {
    while (index > 0 && rows_[index].depth > 0) {
        --index;
    }
    return TopKey(*rows_[index].node);
}

RemotePanel::Node* RemotePanel::SelectedNode() {
    return selectedIndex_ < rows_.size() ? rows_[selectedIndex_].node : nullptr;
}

void RemotePanel::Refresh() {
    // The selection, found again afterwards by its top and its path.
    std::optional<std::pair<std::string, std::filesystem::path>> selected;
    if (const Node* node = SelectedNode()) {
        selected.emplace(TopKeyOfRow(selectedIndex_), node->location.path);
    }

    std::vector<Node> previous = std::move(tops_);
    tops_.clear();
    const auto add = [&previous, this](Node fresh) {
        const std::string key = TopKey(fresh);
        if (const auto it = std::ranges::find(previous, key, &RemotePanel::TopKey); it != previous.end()) {
            tops_.push_back(std::move(*it));
            previous.erase(it);
            return;
        }
        tops_.push_back(std::move(fresh));
    };
    for (const text::RemoteFile& root : roots_) {
        add(Node{.location = root, .name = {}, .root = true});
    }
    for (const std::string& destination : hosts_.Destinations()) {
        if (std::ranges::none_of(roots_, [&destination](const text::RemoteFile& root) { return root.destination == destination; })) {
            add(Node{.location = {.destination = destination, .path = {}}, .name = {}, .root = false});
        }
    }
    PushModel();

    if (selected) {
        SelectNearest(selected->first, selected->second);
    }
}

void RemotePanel::SelectNearest(const std::string& topKey, std::filesystem::path path) {
    while (true) {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].node->location.path == path && TopKeyOfRow(i) == topKey) {
                selectedIndex_ = i;
                PushModel();
                return;
            }
        }
        if (path == path.parent_path()) {
            return;
        }
        path = path.parent_path();
    }
}

bool RemotePanel::Shown(const Node& node) const {
    return showDotfiles_ || !node.name.starts_with('.');
}

void RemotePanel::OpenRoot(const std::string& typed) {
    if (std::optional<text::RemoteFile> root = text::ParseRemoteFile(typed)) {
        if (std::ranges::find(roots_, *root) == roots_.end()) {
            roots_.push_back(*root);
            if (onRootsChanged_) {
                onRootsChanged_();
            }
        }
        Refresh();
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].depth == 0 && rows_[i].node->root && rows_[i].node->location == *root) {
                selectedIndex_ = i;
                Expand(*rows_[i].node);
                break;
            }
        }
        return;
    }
    std::string destination = typed.substr(0, typed.find(':'));
    if (destination.empty()) {
        Report("Open a remote root as host:/path, or a host for its home.");
        return;
    }
    Report("Connecting to " + destination + "...");
    hosts_.Connect(destination, lifetime_.Bind([this, destination](const std::expected<editor::remote::Connection*, std::string>& connection) {
        if (!connection) {
            Report("Couldn't connect to " + destination + ": " + connection.error());
            return;
        }
        editor::remote::ResolvePath(**connection, ".", lifetime_.Bind([this, destination](editor::remote::FsResult<std::filesystem::path> home) {
            if (!home) {
                Report("Couldn't find " + destination + "'s home: " + home.error().message);
                return;
            }
            OpenRoot(text::RemoteFile{.destination = destination, .path = *home}.ToString());
        }));
    }));
}

void RemotePanel::Expand(Node& node) {
    if (node.kind != editor::host::FileKind::Directory || node.loading) {
        return;
    }
    node.expanded = true;
    if (!node.children) {
        node.loading = true;
        node.error.clear();
        node.listing = ++nextListing_;
        // Found again by its top, since a refresh moves the tops.
        const auto row = std::ranges::find(rows_, &node, &Row::node);
        List(TopKeyOfRow(static_cast<std::size_t>(row - rows_.begin())), node.location, node.listing);
    }
    PushModel();
}

void RemotePanel::Changed(const text::RemoteFile& path) {
    for (Node& top : tops_) {
        if (top.location.destination != path.destination || top.location.path.empty()) {
            continue;
        }
        // The nearest shown directory above path: a new a/b/c lands in
        // whichever of its ancestors is listed.
        const std::string key = TopKey(top);
        for (std::filesystem::path directory = path.path.parent_path();; directory = directory.parent_path()) {
            if (Node* node = FindNode(key, directory)) {
                if (node->children && !node->loading) {
                    Relist(key, *node);
                }
                break;
            }
            if (directory == directory.parent_path()) {
                break;
            }
        }
    }
    PushModel();
}

void RemotePanel::Relist(const std::string& topKey, Node& node) {
    node.loading = true;
    node.error.clear();
    node.listing = ++nextListing_;
    List(topKey, node.location, node.listing);
}

void RemotePanel::List(const std::string& topKey, const text::RemoteFile& directory, std::uint64_t listing) {
    hosts_.Connect(directory.destination, lifetime_.Bind([this, topKey, directory, listing](const std::expected<editor::remote::Connection*, std::string>& connection) {
        if (!connection) {
            Settle(topKey, directory.path, listing, connection.error(), std::nullopt);
            return;
        }
        if (directory.path.empty()) {
            editor::remote::ResolvePath(**connection, ".", lifetime_.Bind([this, topKey, listing](editor::remote::FsResult<std::filesystem::path> home) {
                Node* top = FindTop(topKey);
                if (top == nullptr || top->listing != listing) {
                    return;
                }
                if (!home) {
                    Settle(topKey, {}, listing, home.error().message, std::nullopt);
                    return;
                }
                top->location.path = *home;
                List(topKey, top->location, listing);
            }));
            return;
        }
        editor::remote::ListDirectory(
            **connection, directory.path, kMaxListedEntries,
            lifetime_.Bind([this, topKey, path = directory.path, listing](editor::remote::FsResult<std::vector<editor::host::DirEntry>> listed) {
                if (!listed) {
                    Settle(topKey, path, listing, listed.error().message, std::nullopt);
                    return;
                }
                Settle(topKey, path, listing, {}, std::move(*listed));
            }));
    }));
}

void RemotePanel::Settle(const std::string& topKey, const std::filesystem::path& path, std::uint64_t listing, std::string error,
                         std::optional<std::vector<editor::host::DirEntry>> entries) {
    Node* node = FindNode(topKey, path);
    if (node == nullptr || node->listing != listing) {
        return;
    }
    std::optional<std::pair<std::string, std::filesystem::path>> selected;
    if (const Node* current = SelectedNode()) {
        selected.emplace(TopKeyOfRow(selectedIndex_), current->location.path);
    }
    node->loading = false;
    if (!entries) {
        node->error    = std::move(error);
        node->expanded = false;
        PushModel();
        return;
    }
    std::ranges::sort(*entries, [](const editor::host::DirEntry& a, const editor::host::DirEntry& b) {
        const bool aDirectory = a.kind == editor::host::FileKind::Directory;
        const bool bDirectory = b.kind == editor::host::FileKind::Directory;
        return aDirectory != bDirectory ? aDirectory : a.name < b.name;
    });
    std::vector<Node> children;
    children.reserve(entries->size());
    for (editor::host::DirEntry& entry : *entries) {
        text::RemoteFile location{.destination = node->location.destination, .path = node->location.path / entry.name};
        Node             child{.location = std::move(location), .name = std::move(entry.name), .kind = entry.kind, .link = entry.link};
        // Listed again: a directory that's still there keeps its own listing.
        if (node->children) {
            const auto kept = std::ranges::find_if(*node->children, [&child](const Node& old) { return old.name == child.name && old.kind == child.kind; });
            if (kept != node->children->end()) {
                child = std::move(*kept);
            }
        }
        children.push_back(std::move(child));
    }
    node->children = std::move(children);
    PushModel();
    if (selected) {
        SelectNearest(selected->first, selected->second);
    }
}

void RemotePanel::HandleActivate(std::size_t index) {
    if (index >= rows_.size()) {
        if (onRequestOpenRoot_) {
            onRequestOpenRoot_(); // the empty panel's hint row
        }
        return;
    }
    Node& node = *rows_[index].node;
    if (node.kind == editor::host::FileKind::File) {
        if (onOpenFile_) {
            onOpenFile_(node.location);
        }
        return;
    }
    if (node.expanded) {
        node.expanded = false;
        PushModel();
        return;
    }
    Expand(node);
}

void RemotePanel::HandleCollapse(std::size_t index) {
    if (index >= rows_.size()) {
        return;
    }
    Node& node = *rows_[index].node;
    if (node.expanded) {
        node.expanded = false;
        PushModel();
        return;
    }
    for (std::size_t i = index; i-- > 0;) {
        if (rows_[i].depth < rows_[index].depth) {
            selectedIndex_ = i;
            PushModel();
            return;
        }
    }
}

void RemotePanel::Run(std::string_view command) {
    if (command == "remote-panel-open-root") {
        if (onRequestOpenRoot_) {
            onRequestOpenRoot_();
        }
        return;
    }
    if (command == "remote-panel-toggle-dotfiles") {
        showDotfiles_ = !showDotfiles_;
        Refresh();
        Report(showDotfiles_ ? "Showing dotfiles" : "Hiding dotfiles");
        return;
    }
    Node* node = SelectedNode();
    if (node == nullptr) {
        return;
    }
    if (command == "remote-panel-new-file") {
        HandleFileAction(FileAction::NewFile, rows_[selectedIndex_].depth, *node);
    }
    else if (command == "remote-panel-new-directory") {
        HandleFileAction(FileAction::NewDirectory, rows_[selectedIndex_].depth, *node);
    }
    else if (command == "remote-panel-rename") {
        HandleFileAction(FileAction::Rename, rows_[selectedIndex_].depth, *node);
    }
    else if (command == "remote-panel-delete") {
        HandleFileAction(FileAction::Delete, rows_[selectedIndex_].depth, *node);
    }
    else if (command == "remote-panel-refresh") {
        if (node->kind == editor::host::FileKind::Directory) {
            node->children.reset();
            node->loading = false;
            Expand(*node);
        }
    }
    else if (command == "remote-panel-disconnect") {
        const std::string destination = node->location.destination;
        Report(hosts_.Disconnect(destination) ? "Disconnected from " + destination : "Not connected to " + destination);
    }
    else if (command == "remote-panel-remove-root") {
        if (rows_[selectedIndex_].depth != 0 || !node->root) {
            Report("Only a root is removed from the panel");
            return;
        }
        const text::RemoteFile root = node->location;
        std::erase(roots_, root);
        if (onRootsChanged_) {
            onRootsChanged_();
        }
        Refresh();
        Report("Removed " + root.ToString() + " from the Remote panel");
    }
}

void RemotePanel::HandleFileAction(FileAction action, std::size_t depth, const Node& node) {
    if (!onFileAction_) {
        return;
    }
    if (node.location.path.empty()) {
        Report("Expand " + node.location.destination + " first");
        return;
    }
    if (action == FileAction::Rename || action == FileAction::Delete) {
        if (depth == 0) {
            Report(node.root ? "A root is removed from the panel, not renamed or deleted from here"
                             : "A host's home isn't renamed or deleted from here");
            return;
        }
        onFileAction_(action, node.location);
        return;
    }
    const bool       directory = node.kind == editor::host::FileKind::Directory;
    text::RemoteFile holder    = node.location;
    if (!directory) {
        holder.path = holder.path.parent_path();
    }
    onFileAction_(action, holder);
}

void RemotePanel::PushModel() {
    rows_.clear();
    const std::function<void(Node&, std::size_t)> flatten = [this, &flatten](Node& node, std::size_t depth) {
        rows_.push_back(Row{.node = &node, .depth = depth});
        if (node.expanded && node.children) {
            for (Node& child : *node.children) {
                if (Shown(child)) {
                    flatten(child, depth + 1);
                }
            }
        }
    };
    for (Node& top : tops_) {
        flatten(top, 0);
    }

    TreeViewModel model{.title = "Remote", .rows = {}, .selectedIndex = std::nullopt};
    for (const Row& row : rows_) {
        const Node& node      = *row.node;
        const bool  directory = node.kind == editor::host::FileKind::Directory;
        TreeRow     out{.kindGlyph       = "",
                        .kindForeground  = std::nullopt,
                        .label           = node.name,
                        .depth           = row.depth,
                        .labelForeground = std::nullopt,
                        .right           = node.error,
                        .rightForeground = theme_.indentGuideForeground,
                        .hasChildren     = directory && (!node.children || std::ranges::any_of(*node.children, [this](const Node& child) { return Shown(child); })),
                        .expanded        = node.expanded,
                        .loading         = node.loading};
        if (row.depth == 0) {
            const std::string host = text::ShortDestination(node.location.destination);
            out.label              = node.root ? host + ":" + node.location.path.string() : host + ":~";
            const bool connected   = hosts_.Find(node.location.destination) != nullptr;
            out.kindGlyph          = connected ? "●" : "○";
            out.kindForeground     = connected ? theme_.borderAccent.foreground : theme_.indentGuideForeground;
            if (node.error.empty() && hosts_.Connecting(node.location.destination)) {
                out.right = "connecting";
            }
        }
        else {
            out.kindGlyph = directory ? editor::FolderIcon(node.expanded) : editor::FileIcon(node.location.path);
            if (node.link) {
                out.label += " →";
            }
        }
        if (!directory && node.kind != editor::host::FileKind::File) {
            out.labelForeground = theme_.indentGuideForeground; // a dangling link, a socket, a device
        }
        model.rows.push_back(std::move(out));
    }
    if (rows_.empty()) {
        model.rows.push_back(TreeRow{.kindGlyph       = " ",
                                     .kindForeground  = std::nullopt,
                                     .label           = "No hosts -- o opens host:/path",
                                     .depth           = 0,
                                     .labelForeground = theme_.indentGuideForeground,
                                     .right           = {},
                                     .rightForeground = std::nullopt,
                                     .hasChildren     = false,
                                     .expanded        = false,
                                     .loading         = false});
    }
    selectedIndex_      = std::min(selectedIndex_, model.rows.size() - 1);
    model.selectedIndex = selectedIndex_;
    tree_.SetModel(std::move(model));

    if (std::vector<text::RemoteFile> listed = ListedDirectories(); listed != listed_) {
        listed_ = std::move(listed);
        if (onListedChanged_) {
            onListedChanged_();
        }
    }
}

std::vector<text::RemoteFile> RemotePanel::ListedDirectories() const {
    std::vector<text::RemoteFile> listed;
    for (const Row& row : rows_) {
        const Node& node = *row.node;
        if (node.kind == editor::host::FileKind::Directory && node.expanded && node.children && !node.location.path.empty() &&
            std::ranges::find(listed, node.location) == listed.end()) {
            listed.push_back(node.location);
        }
    }
    return listed;
}

void RemotePanel::SetOnListedChanged(std::function<void()> handler) {
    onListedChanged_ = std::move(handler);
}

} // namespace ned::ui
