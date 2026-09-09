//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// The VCS and DAP integrations: blame/diff/status/branches/commit/hunks, and
// watches/memory/disassembly/pointer-graph/thread+filter selection.
//

#include "UI/BufferView/Internal.h"

namespace ned::ui {

// The file-local helpers these definitions call live in BufferView/Internal.h
// now that several parts share them -- see that header. This using-directive is
// what let the split leave every call site untouched.
using namespace detail;

void BufferView::SetOnDapConsoleToggle(std::function<void()> handler) {
    onDapConsoleToggle_ = std::move(handler);
}

void BufferView::SetOnDapThreadsToggle(std::function<void()> handler) {
    onDapThreadsToggle_ = std::move(handler);
}

void BufferView::SetVcsRunner(editor::vcs::VcsRunner* vcsRunner) {
    vcsRunner_ = vcsRunner;
}

void BufferView::DispatchBlameForTesting(std::vector<editor::vcs::VcsBlameLine> lines) {
    text::Buffer& buffer = activeBuffer_.Get();
    blameLineInfo_.clear();
    blameLineInfo_.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        blameLineInfo_.emplace_back(i, std::move(lines[i]));
    }
    blameGutterCacheStamp_ = bufferview::CacheStamp::For(&buffer, {buffer.ContentGeneration()});
}

void BufferView::RequestBlameForCurrentBuffer() {
    // A real toggle: vcs-show-blame called again while blame is already
    // showing for this buffer turns it off instead of re-fetching -- the
    // reported, real gap this fixes is that there was previously no way to
    // turn it back off at all short of switching buffers and back (which
    // clears it as a side effect of Paint()'s own buffer-switch handling,
    // not a deliberate toggle). Guarded on blameGutterCacheStamp_'s buffer
    // specifically (not just BlameGutterActive()) so pressing the key
    // again for a *different* buffer than the one blame is currently
    // loaded for still fetches fresh, rather than clearing the wrong
    // buffer's (already-stale-by-definition, since it's a different
    // buffer) data.
    if (BlameGutterActive() && blameGutterCacheStamp_.IsFor(&activeBuffer_.Get())) {
        blameLineInfo_.clear();
        statusMessage_ = "blame hidden";
        return;
    }

    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    text::Buffer* buffer = &activeBuffer_.Get();
    vcsRunner_->RequestBlame(
        *buffer,
        [this, buffer](std::vector<editor::vcs::VcsBlameLine> lines) {
            if (&activeBuffer_.Get() != buffer) {
                return; // active buffer changed while the request was in flight -- discard, it's stale
            }
            DispatchBlameForTesting(std::move(lines)); // reused here too -- see its own doc comment
            statusMessage_ = "blame loaded";
        },
        [this](std::string error) { statusMessage_ = "vcs blame: " + error; });
}

void BufferView::ShowBlameDetailAtPoint() {
    if (!BlameGutterActive()) {
        statusMessage_ = "no blame data loaded -- run vcs-show-blame (C-c v b) first";
        return;
    }

    const text::Buffer& buffer = activeBuffer_.Get();
    const std::size_t   line   = buffer.Content().ByteOffsetToLine(buffer.Point());

    // Same lower_bound lookup Paint()'s own blame-gutter rendering uses --
    // blameLineInfo_ is sorted by line, one entry per blamed line.
    const auto it = std::lower_bound(blameLineInfo_.begin(), blameLineInfo_.end(), line,
                                     [](const auto& entry, std::size_t l) { return entry.first < l; });
    if (it == blameLineInfo_.end() || it->first != line) {
        statusMessage_ = "no blame data for this line";
        return;
    }

    const editor::vcs::VcsBlameLine& blame = it->second;
    statusMessage_                         = blame.commitHash + " " + blame.author + " (" + blame.date + "): " + blame.summary;
}

void BufferView::ScheduleDiffRefresh() {
    if (!eventLoop_ || !vcsRunner_) {
        return; // headless test, or no VcsRunner wired in -- see this method's own header comment
    }
    // Same "Arm re-cancels any still-pending previous fire" debounce shape
    // completionDebounceTimer_ already established for LSP completion --
    // rapid typing keeps pushing the debounce deadline out
    // rather than firing once per keystroke.
    diffRefreshTimer_.Arm(*eventLoop_, editor::DiffRefreshDebounce(), [this] { RequestDiffForCurrentBuffer(); });
}

void BufferView::DispatchDiffForTesting(std::vector<editor::vcs::VcsDiffHunk> hunks) {
    // initial-buffer-diff fix: mark the active buffer's diff as synced. In
    // production this is a no-op (the only real caller is the request
    // completion, which only runs after Paint's diffSyncBuffer_ branch
    // already set this); for a test injecting hunks directly it's what
    // keeps the next Paint() from immediately clearing them via that same
    // branch.
    diffSyncBuffer_ = &activeBuffer_.Get();
    std::vector<std::pair<std::size_t, DiffLineKind>> kinds;
    std::vector<std::size_t>                          hunkStartLines;
    for (const editor::vcs::VcsDiffHunk& hunk : hunks) {
        if (hunk.newCount == 0) {
            // Pure deletion -- a boundary, not a covered range. git's
            // newStart is already the 0-indexed line the deletion sits
            // immediately before (1-indexed "the line after the gap" ==
            // 0-indexed "that same line"), confirmed against real `git
            // diff -U0` output while building this (see
            // GitVcsPluginTest.cpp).
            kinds.emplace_back(hunk.newStart, DiffLineKind::Removed);
            hunkStartLines.push_back(hunk.newStart);
            continue;
        }
        const DiffLineKind kind = (hunk.oldCount == 0) ? DiffLineKind::Added : DiffLineKind::Modified;
        for (std::size_t i = 0; i < hunk.newCount; ++i) {
            kinds.emplace_back(hunk.newStart - 1 + i, kind); // newStart is 1-indexed
        }
        hunkStartLines.push_back(hunk.newStart - 1);
    }
    std::sort(kinds.begin(), kinds.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    diffLineKinds_ = std::move(kinds);
    std::sort(hunkStartLines.begin(), hunkStartLines.end());
    diffHunkStartLines_ = std::move(hunkStartLines);
}

void BufferView::JumpToNextHunk() {
    if (diffHunkStartLines_.empty()) {
        statusMessage_ = "No changes in this buffer.";
        return;
    }
    text::Buffer&     buffer      = activeBuffer_.Get();
    const std::size_t currentLine = buffer.Content().ByteOffsetToLine(buffer.Point());
    const auto        it          = std::upper_bound(diffHunkStartLines_.begin(), diffHunkStartLines_.end(), currentLine);
    if (it == diffHunkStartLines_.end()) {
        statusMessage_ = "No more changed hunks below point.";
        return;
    }
    buffer.SetPoint(buffer.Content().LineToByteOffset(*it));
    statusMessage_.clear();
    viewport_.ScrollToShowPoint();
}

void BufferView::JumpToPreviousHunk() {
    if (diffHunkStartLines_.empty()) {
        statusMessage_ = "No changes in this buffer.";
        return;
    }
    text::Buffer&     buffer      = activeBuffer_.Get();
    const std::size_t currentLine = buffer.Content().ByteOffsetToLine(buffer.Point());
    const auto        it          = std::lower_bound(diffHunkStartLines_.begin(), diffHunkStartLines_.end(), currentLine);
    if (it == diffHunkStartLines_.begin()) {
        statusMessage_ = "No more changed hunks above point.";
        return;
    }
    buffer.SetPoint(buffer.Content().LineToByteOffset(*(it - 1)));
    statusMessage_.clear();
    viewport_.ScrollToShowPoint();
}

void BufferView::RequestDiffForCurrentBuffer() {
    if (!vcsRunner_) {
        return; // silent -- see this method's own header comment
    }
    text::Buffer* buffer = &activeBuffer_.Get();
    vcsRunner_->RequestDiff(
        *buffer,
        [this, buffer](std::vector<editor::vcs::VcsDiffHunk> hunks) {
            if (&activeBuffer_.Get() != buffer) {
                return; // active buffer changed while the request was in flight -- discard, it's stale
            }
            DispatchDiffForTesting(std::move(hunks)); // reused here too -- see its own doc comment
        },
        [](const std::string&) {}); // silent -- see this method's own header comment
}

void BufferView::RefreshVcsDiff() {
    RequestDiffForCurrentBuffer();
}

void BufferView::VisitVcsResult() {
    VisitResultUnderPoint();
}

void BufferView::RequestVcsBlameBuffer() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    text::Buffer* buffer = &activeBuffer_.Get();
    if (!buffer->Path()) {
        statusMessage_ = "no file associated with this buffer";
        return;
    }
    const std::filesystem::path path = *buffer->Path();
    vcsRunner_->RequestBlame(
        *buffer,
        [this, buffer, path](std::vector<editor::vcs::VcsBlameLine> lines) {
            if (&activeBuffer_.Get() == buffer) {
                // Populates the gutter for the still-active source buffer
                // before BuildVcsBlameBuffer switches activeBuffer_ away
                // from it -- see DispatchBlameForTesting's own doc comment.
                DispatchBlameForTesting(lines);
            }
            BuildVcsBlameBuffer(path, lines);
        },
        [this](std::string error) { statusMessage_ = "vcs blame: " + error; });
}

void BufferView::RequestVcsLogBuffer() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.Path()) {
        statusMessage_ = "no file associated with this buffer";
        return;
    }
    const std::filesystem::path path = *buffer.Path();
    vcsRunner_->RequestLog(
        buffer, [this, path](std::vector<editor::vcs::VcsLogEntry> entries) { BuildVcsLogBuffer(path, entries); },
        [this](std::string error) { statusMessage_ = "vcs log: " + error; });
}

void BufferView::BuildVcsBlameBuffer(const std::filesystem::path& path, const std::vector<editor::vcs::VcsBlameLine>& lines) {
    std::string resultsText;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const editor::vcs::VcsBlameLine& line      = lines[i];
        const std::string                shortHash = line.commitHash.substr(0, std::min<std::size_t>(8, line.commitHash.size()));
        resultsText += path.string() + ":" + std::to_string(i + 1) + ": " + shortHash + " " + line.author + " " + line.date +
                       " | " + line.summary + "\n";
    }

    const std::string bufferName = "*vcs blame " + path.filename().string() + "*";
    text::Buffer&     results    = bufferList_.CreateBuffer(bufferName);
    results.InsertAtPoint(resultsText);
    results.SetPoint(0);
    results.SetReadOnly(true); // see BuildResultsBuffer's own doc comment for why
    editor::SetLastResultsBuffer(bufferName);
    activeBuffer_.Set(results);
}

void BufferView::BuildVcsLogBuffer(const std::filesystem::path& path, const std::vector<editor::vcs::VcsLogEntry>& entries) {
    std::string resultsText;
    for (const editor::vcs::VcsLogEntry& entry : entries) {
        const std::string shortHash = entry.commitHash.substr(0, std::min<std::size_t>(8, entry.commitHash.size()));
        resultsText += shortHash + " " + entry.date + " " + entry.author + ": " + entry.summary + "\n";
    }

    text::Buffer& results = bufferList_.CreateBuffer("*vcs log " + path.filename().string() + "*");
    results.InsertAtPoint(resultsText);
    results.SetPoint(0);
    results.SetReadOnly(true);
    activeBuffer_.Set(results);
}

// Full commit diff view follow-up: the shared tail of RequestVcsFullDiffBuffer
// and RequestVcsCommitDiffBuffer -- both hand this the same shape of raw
// `git diff`-style text (the whole working tree, or one commit's own
// changeset via `git show`), so turning it into a stitched multibuffer is
// identical either way, only the buffer name/empty-result message differ.
// Always switches activeBuffer_, even for an empty result (an explicit
// "nothing changed" is more informative than a silent no-op).

void BufferView::BuildDiffHunksMultibuffer(const std::string& rawDiff, const std::filesystem::path& root,
                                           const std::string& bufferName, const std::string& emptyMessage) {
    const std::vector<editor::vcs::DiffHunkText> hunks = editor::vcs::ParseDiffHunks(rawDiff);

    std::vector<editor::multibuffer::ExcerptSource> excerpts;
    excerpts.reserve(hunks.size());
    for (const editor::vcs::DiffHunkText& hunk : hunks) {
        std::vector<editor::multibuffer::LineTint> tints;
        std::string                                formattedBody = FormatDiffHunkBody(hunk, tints);

        // newCount == 0 is a pure deletion (see DiffPatch.h's own doc
        // comment on Covers) -- there's no real new-side line to jump to, so
        // this excerpt's header is still shown but sourceStartLine stays 0
        // ("no single source line applies", ExcerptSource's own documented
        // convention).
        const std::size_t sourceLine = hunk.newCount > 0 ? hunk.newStart : 0;
        excerpts.push_back(editor::multibuffer::ExcerptSource{
            root / hunk.filePath, sourceLine, sourceLine + (hunk.newCount > 0 ? hunk.newCount - 1 : 0),
            "▸ " + hunk.filePath + "  " + hunk.hunkHeader, // U+25B8 -- ProjectSidebar's own disclosure triangle
            std::move(formattedBody), std::move(tints)});
    }

    text::Buffer& results = editor::multibuffer::BuildMultibuffer(bufferList_, bufferName, excerpts);
    editor::SetLastResultsBuffer(bufferName);
    activeBuffer_.Set(results);
    statusMessage_ = excerpts.empty() ? emptyMessage : std::to_string(excerpts.size()) + " changed hunk" + (excerpts.size() == 1 ? "" : "s");

    // Auto-collapse-on-build follow-up: a binary file's own diff produces
    // zero hunks (see CountBinaryFileDiffs' own doc comment), so it's
    // already silently absent from excerpts above with no code needed for
    // that -- this just says so, rather than leaving the omission
    // unexplained when git's own file count doesn't match what's shown.
    if (const std::size_t binaryCount = editor::vcs::CountBinaryFileDiffs(rawDiff); binaryCount > 0) {
        statusMessage_ += " (" + std::to_string(binaryCount) + " binary file" + (binaryCount == 1 ? "" : "s") + " not shown)";
    }
}

void BufferView::RequestVcsFullDiffBuffer() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    const std::filesystem::path root = editor::ProjectRoot();
    vcsRunner_->RequestFullDiff(
        [this, root](std::string rawDiff) { BuildDiffHunksMultibuffer(rawDiff, root, "*vcs diff*", "Working tree clean."); },
        [this](std::string error) { statusMessage_ = "vcs full diff: " + error; });
}

void BufferView::RequestVcsCommitDiffBuffer(const std::string& commitHash) {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    const std::filesystem::path root = editor::ProjectRoot();
    vcsRunner_->RequestCommitDiff(
        commitHash,
        [this, root, commitHash](std::string rawDiff) {
            BuildDiffHunksMultibuffer(rawDiff, root, "*vcs commit " + commitHash + "*",
                                      "Commit " + commitHash + " touched no files.");
        },
        [this](std::string error) { statusMessage_ = "vcs commit diff: " + error; });
}

void BufferView::RequestVcsStatusBuffer() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    vcsRunner_->RequestStatus(
        [this](std::vector<editor::vcs::VcsStatusEntry> entries) { BuildVcsStatusBuffer(entries, /*announce=*/true); },
        [this](std::string error) { statusMessage_ = "vcs status: " + error; });
}

void BufferView::BuildVcsStatusBuffer(const std::vector<editor::vcs::VcsStatusEntry>& entries, bool announce) {
    const std::filesystem::path root = editor::ProjectRoot();

    std::string text;
    for (const editor::vcs::VcsStatusEntry& entry : entries) {
        text += (root / entry.path).string() + ":1: " + entry.state + " " + entry.path + "\n";
    }

    text::Buffer& status = RefillSingletonBuffer(bufferList_, kVcsStatusBufferName, text);
    if (announce) {
        editor::SetLastResultsBuffer(kVcsStatusBufferName);
        activeBuffer_.Set(status);
        statusMessage_ = entries.empty()
                             ? "Working tree clean."
                             : std::to_string(entries.size()) + " changed file" + (entries.size() == 1 ? "" : "s") +
                                   " -- C-c v a stages, C-c v u unstages, C-c v v visits";
    }
}

void BufferView::RefreshVcsStatusBuffer() {
    if (!vcsRunner_ || !bufferList_.Find(kVcsStatusBufferName)) {
        return;
    }
    vcsRunner_->RequestStatus(
        [this](std::vector<editor::vcs::VcsStatusEntry> entries) { BuildVcsStatusBuffer(entries, /*announce=*/false); });
}

void BufferView::BeginVcsCommitMessage() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    const std::filesystem::path path = editor::vcs::VcsCommitMessagePath();
    // FindByPath, not the OpenOrCreateFile call below, decides whether this
    // is a genuinely fresh commit (seed the template) or the user re-running
    // vcs-commit while one is already mid-composition (switch to it as-is,
    // preserving whatever they've already typed) -- OpenOrCreateFile itself
    // always returns *some* buffer either way.
    const bool    alreadyOpen  = bufferList_.FindByPath(path) != nullptr;
    text::Buffer& commitBuffer = bufferList_.OpenOrCreateFile(path);
    if (!alreadyOpen) {
        commitBuffer.InsertAtPoint(editor::vcs::kVcsCommitMessageTemplate);
        commitBuffer.SetPoint(0);
    }
    activeBuffer_.Set(commitBuffer);
}

void BufferView::FinishVcsCommitMessage() {
    text::Buffer&     commitBuffer = activeBuffer_.Get();
    const std::string message      = editor::vcs::ExtractCommitMessage(commitBuffer.Text());
    CloseVcsCommitMessageBuffer(commitBuffer);
    if (message.empty()) {
        statusMessage_ = "Empty commit message -- not committing.";
    }
    else if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
    }
    else {
        // Fire-and-forget, DapEvaluate's shape: the buffer's already closed
        // by the time this fires, the summary lands in statusMessage_ from
        // the callback.
        statusMessage_ = "Committing...";
        vcsRunner_->RequestCommit(
            message,
            [this](std::string summary) {
                statusMessage_ = summary.empty() ? "Committed." : summary;
                RefreshVcsStatusBuffer();
                // The comparison point (HEAD for git) just moved, so the
                // current buffer's markers are stale now.
                RequestDiffForCurrentBuffer();
            },
            [this](std::string error) { statusMessage_ = "vcs commit: " + error; });
    }
}

void BufferView::AbortVcsCommitMessage() {
    CloseVcsCommitMessageBuffer(activeBuffer_.Get());
    statusMessage_ = "Commit aborted.";
}

void BufferView::CloseVcsCommitMessageBuffer(text::Buffer& commitBuffer) {
    CloseBufferNow(commitBuffer);
    std::error_code ec;
    std::filesystem::remove(editor::vcs::VcsCommitMessagePath(), ec); // best-effort -- a leftover temp file is harmless
}

std::optional<std::filesystem::path> BufferView::ResolveVcsFileTarget() {
    text::Buffer& buffer = activeBuffer_.Get();
    if (buffer.Name() == kVcsStatusBufferName) {
        const text::ITextStorage& content   = buffer.Content();
        const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
        const std::size_t         lineStart = content.LineToByteOffset(line);
        const std::size_t         lineEnd =
            (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

        // BuildVcsStatusBuffer's own "<absolute path>:1: ..." shape -- the
        // same pattern VisitVcsResult parses, reused for the same reason.
        static const std::regex resultLinePattern(R"(^(.*):(\d+):)");
        std::smatch             match;
        if (std::regex_search(lineText, match, resultLinePattern)) {
            return std::filesystem::path(match[1].str());
        }
        return std::nullopt; // an empty/foreign line in the status buffer
    }
    if (buffer.Path()) {
        return *buffer.Path();
    }
    return std::nullopt;
}

void BufferView::StageOrUnstageHunkAtPoint(bool stage) {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.Path()) {
        statusMessage_ = "no file associated with this buffer";
        return;
    }
    if (buffer.Modified()) {
        // See this method's header doc comment -- unsaved edits make the
        // buffer's line numbers disagree with the on-disk diff's.
        statusMessage_ = "Buffer has unsaved changes -- save first, hunk staging works from the file on disk.";
        return;
    }

    const std::size_t targetLine = buffer.Content().ByteOffsetToLine(buffer.Point()) + 1; // 1-indexed, diff's own convention
    vcsRunner_->RequestHunkApply(
        buffer, targetLine, stage,
        [this, stage] {
            statusMessage_ = stage ? "Hunk staged." : "Hunk unstaged.";
            RefreshVcsStatusBuffer();
            RequestDiffForCurrentBuffer();
        },
        [this, stage](std::string error) {
            statusMessage_ = std::string("vcs ") + (stage ? "stage" : "unstage") + " hunk: " + error;
        });
}

void BufferView::RevertHunkAtPoint() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.Path()) {
        statusMessage_ = "no file associated with this buffer";
        return;
    }
    if (buffer.Modified()) {
        // See StageOrUnstageHunkAtPoint's own header doc comment -- unsaved
        // edits make the buffer's line numbers disagree with the on-disk
        // diff's.
        statusMessage_ = "Buffer has unsaved changes -- save first, hunk revert works from the file on disk.";
        return;
    }

    const std::size_t targetLine = buffer.Content().ByteOffsetToLine(buffer.Point()) + 1; // 1-indexed, diff's own convention
    vcsRunner_->RequestHunkRevert(
        buffer, targetLine,
        [this] {
            statusMessage_ = "Hunk reverted.";
            // AutoRevert/FileWatch picks up the now-changed-on-disk file on
            // its own next sweep (this buffer is guaranteed unmodified, the
            // gate just above) -- no explicit Buffer::Revert() call needed
            // here, same reasoning stage/unstage's own success handler
            // relies on for RefreshVcsStatusBuffer/RequestDiffForCurrentBuffer
            // below to see accurate state shortly after.
            RefreshVcsStatusBuffer();
            RequestDiffForCurrentBuffer();
        },
        [this](std::string error) {
            statusMessage_ = "vcs revert hunk: " + error;
        });
}

void BufferView::RevertHunkAtPointForTesting() {
    RevertHunkAtPoint();
}

void BufferView::StageHunkAtPointForTesting(bool stage) {
    StageOrUnstageHunkAtPoint(stage);
}

void BufferView::JumpToNextHunkForTesting() {
    JumpToNextHunk();
}

void BufferView::JumpToPreviousHunkForTesting() {
    JumpToPreviousHunk();
}

void BufferView::RequestPointerGraphAtPointForTesting() {
    RequestPointerGraphAtPoint();
}

void BufferView::BeginVcsCommitMessageForTesting() {
    BeginVcsCommitMessage();
}

void BufferView::FinishVcsCommitMessageForTesting() {
    FinishVcsCommitMessage();
}

void BufferView::AbortVcsCommitMessageForTesting() {
    AbortVcsCommitMessage();
}

void BufferView::RequestVcsBranchesBuffer() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    vcsRunner_->RequestBranchList(
        [this](std::vector<editor::vcs::VcsBranchEntry> entries) { BuildVcsBranchesBuffer(entries); },
        [this](std::string error) { statusMessage_ = "vcs branches: " + error; });
}

void BufferView::BuildVcsBranchesBuffer(const std::vector<editor::vcs::VcsBranchEntry>& entries) {
    std::string text;
    for (const editor::vcs::VcsBranchEntry& entry : entries) {
        text += (entry.current ? "* " : "  ") + entry.name + "\n";
    }

    text::Buffer& branches = RefillSingletonBuffer(bufferList_, kVcsBranchesBufferName, text);
    activeBuffer_.Set(branches);
    statusMessage_ = entries.empty() ? "No branches." : "M-x vcs-switch-branch switches; C-c v n creates.";
}

std::optional<std::filesystem::path> BufferView::ResolveVcsFileTargetForTesting() {
    return ResolveVcsFileTarget();
}

void BufferView::BeginVcsSwitchBranchPrompt() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    statusMessage_ = "Fetching branches...";
    vcsRunner_->RequestBranchList(
        [this](std::vector<editor::vcs::VcsBranchEntry> entries) {
            if (inputMode_ != InputMode::Normal) {
                // Another prompt began while the fetch was in flight --
                // don't hijack it (same in-progress guard RequestCloseBuffer
                // applies to its own confirmation).
                return;
            }
            std::vector<std::string> branchNames;
            for (const editor::vcs::VcsBranchEntry& entry : entries) {
                if (!entry.current) {
                    branchNames.push_back(entry.name);
                }
            }
            vcsBranchList_.Reset(std::move(branchNames));
            inputMode_ = InputMode::VcsSwitchBranch;
            prompt_.emplace("Switch to branch: ");
            vcsBranchList_.SelectTop();
            RefreshVcsSwitchBranchStatus();
        },
        [this](std::string error) { statusMessage_ = "vcs branch: " + error; });
}

// VCS side panel follow-up: pulled out of the VcsCreateBranch switch case
// verbatim so VcsPanel's own 'n' key (via RequestVcsAction) can start the
// exact same prompt InteractiveRequest::VcsCreateBranch already does.

void BufferView::BeginVcsCreateBranchPrompt() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    inputMode_ = InputMode::VcsCreateBranch;
    prompt_.emplace("New branch: ");
    statusMessage_ = prompt_->StatusText();
}

void BufferView::BuildDebugInfoLines(std::function<void(std::vector<std::string>)> onComplete) {
    dapManager_->RequestStackTrace([this, onComplete = std::move(onComplete)](std::vector<editor::dap::DapManager::StackFrame> frames) {
        if (frames.empty()) {
            onComplete({});
            return;
        }
        auto lines = std::make_shared<std::vector<std::string>>();
        lines->push_back("== Stack ==");
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const editor::dap::DapManager::StackFrame& frame = frames[i];
            // DAP round 4: "[frame:N]" is dap-restart-frame's own target
            // marker, RestartFrameAtPoint's counterpart to
            // FormatDebugVariableLine's "[ref:N]"/"[owner:M]".
            const std::string frameMarker = "  [frame:" + std::to_string(frame.id) + "]";
            if (frame.path) {
                // The established "path:line: text" results convention, so
                // C-c C-v (project-search-visit-result) jumps to a frame
                // with zero new navigation plumbing.
                lines->push_back(frame.path->string() + ":" + std::to_string(frame.line) + ": #" + std::to_string(i) +
                                 " " + frame.name + frameMarker);
            }
            else {
                lines->push_back("#" + std::to_string(i) + " " + frame.name + " (no source)" + frameMarker);
            }
        }
        dapManager_->RequestScopes(frames[0].id, [this, lines, onComplete](std::vector<editor::dap::DapManager::Scope> scopes) {
            const std::vector<std::string>& watches = dapManager_->Watches();
            if (scopes.empty() && watches.empty()) {
                onComplete(*lines);
                return;
            }
            // One variables request per scope plus one evaluate per watch,
            // all in flight at once -- chunks keep each section's own
            // output in a fixed slot regardless of response interleaving,
            // and every callback runs on the main thread (see DapClient.h),
            // so a plain shared counter covering both fan-outs is
            // race-free. Slot 0 is reserved for watches (built even when
            // empty -- skipped below), slots [1, 1+scopes.size()) for scopes.
            auto remaining = std::make_shared<std::size_t>(scopes.size() + watches.size());
            auto chunks    = std::make_shared<std::vector<std::vector<std::string>>>(1 + scopes.size());
            if (!watches.empty()) {
                std::vector<std::string>& watchChunk = (*chunks)[0];
                watchChunk.push_back("== Watches ==");
                watchChunk.resize(1 + watches.size()); // one line per watch, filled in place by index below
                for (std::size_t w = 0; w < watches.size(); ++w) {
                    dapManager_->Evaluate(
                        watches[w],
                        [this, lines, remaining, chunks, onComplete, w, expression = watches[w]](bool success, std::string text) {
                            (*chunks)[0][1 + w] = "  " + expression + " = " + (success ? text : ("<" + text + ">")) + "  [watch:" +
                                                  std::to_string(w) + "]";
                            if (--*remaining == 0) {
                                for (const std::vector<std::string>& finishedChunk : *chunks) {
                                    lines->insert(lines->end(), finishedChunk.begin(), finishedChunk.end());
                                }
                                onComplete(*lines);
                            }
                        },
                        "watch");
                }
            }
            // scopes.size() == 0 here just means the loop below never runs
            // and *remaining reaches 0 from the watch fan-out above alone.
            for (std::size_t s = 0; s < scopes.size(); ++s) {
                dapManager_->RequestVariables(
                    scopes[s].variablesReference,
                    [this, lines, remaining, chunks, onComplete, s, scopeVariablesReference = scopes[s].variablesReference,
                     scopeName = scopes[s].name](std::vector<editor::dap::DapManager::Variable> variables) {
                        std::vector<std::string>& chunk = (*chunks)[1 + s];
                        chunk.push_back("");
                        chunk.push_back("== " + scopeName + " ==");
                        for (const editor::dap::DapManager::Variable& variable : variables) {
                            chunk.push_back(FormatDebugVariableLine(variable, 2, scopeVariablesReference));
                        }
                        if (--*remaining == 0) {
                            for (const std::vector<std::string>& finishedChunk : *chunks) {
                                lines->insert(lines->end(), finishedChunk.begin(), finishedChunk.end());
                            }
                            onComplete(*lines);
                        }
                    });
            }
        });
    });
}

void BufferView::ShowDebugInfo() {
    statusMessage_ = "Fetching debug info...";
    BuildDebugInfoLines([this](std::vector<std::string> lines) {
        if (lines.empty()) {
            statusMessage_ = "No stack to show (is the session stopped?).";
            return;
        }
        BuildDebugBuffer(lines);
    });
}

void BufferView::SendDebugStateToAgent() {
    statusMessage_ = "Gathering debug state...";
    BuildDebugInfoLines([this](std::vector<std::string> lines) {
        if (lines.empty()) {
            statusMessage_ = "No stack to show (is the session stopped?).";
            return;
        }
        std::string prompt = "The debugger is currently stopped. Here is the current call stack and variable state:\n\n";
        for (const std::string& line : lines) {
            prompt += line + "\n";
        }
        prompt += "\nPlease help me understand what's happening at this point.";
        statusMessage_ = acpManager_->SendPrompt(prompt);
    });
}

void BufferView::BuildDebugBuffer(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) {
        text += line + "\n";
    }
    text::Buffer& debug = bufferList_.CreateBuffer("*debug*");
    debug.InsertAtPoint(text);
    debug.SetPoint(0);
    debug.SetReadOnly(true); // same tossable-read-only reasoning as BuildResultsBuffer
    activeBuffer_.Set(debug);
    statusMessage_ = "C-c C-v visits a frame; dap-expand-variable/dap-set-variable/dap-remove-watch act on point's own line.";
}

void BufferView::ExpandVariableAtPoint() {
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    const std::optional<ParsedDebugVariableLine> parsed    = ParseDebugVariableLine(lineText);
    const int                                    reference = parsed ? parsed->variablesReference : 0;
    if (reference <= 0) {
        statusMessage_ = "No expandable variable on this line.";
        return;
    }
    const std::size_t indent    = parsed->indent;
    const std::size_t markerPos = lineText.rfind("[ref:"); // splice target -- guaranteed present, reference > 0 above

    text::Buffer* const bufferPtr = &buffer;
    statusMessage_                = "Expanding...";
    dapManager_->RequestVariables(
        reference,
        [this, bufferPtr, line, lineText, markerPos, indent, reference](std::vector<editor::dap::DapManager::Variable> variables) {
            if (bufferPtr != &activeBuffer_.Get()) {
                return; // switched away while the request was in flight
            }
            text::Buffer&             target        = *bufferPtr;
            const text::ITextStorage& targetContent = target.Content();
            if (line >= targetContent.LineCount()) {
                return;
            }
            // Staleness guard, same spirit as RequestRenameAtPoint's own:
            // only splice into the exact line the request was made from.
            const std::size_t targetLineStart = targetContent.LineToByteOffset(line);
            const std::size_t targetLineEnd   = (line + 1 < targetContent.LineCount())
                                                    ? targetContent.LineToByteOffset(line + 1) - 1
                                                    : targetContent.ByteLength();
            if (targetContent.Substring(targetLineStart, targetLineEnd - targetLineStart) != lineText) {
                statusMessage_ = "Debug line changed -- not expanding.";
                return;
            }
            if (variables.empty()) {
                statusMessage_ = "No children (or the session already resumed).";
                return;
            }

            // Consume the "[ref:N]" marker (and its separating spaces) so a
            // second expand on the same line can't splice duplicates in --
            // but keep a trailing "[owner:M]" (round 2), if this line had
            // one, so the parent variable itself stays editable via
            // dap-set-variable even after being expanded.
            const std::size_t ownerMarkerPos = lineText.rfind("[owner:");
            const std::string ownerSuffix    = ownerMarkerPos != std::string::npos ? "  " + lineText.substr(ownerMarkerPos) : "";
            std::string       replacement    = lineText.substr(0, markerPos);
            while (!replacement.empty() && replacement.back() == ' ') {
                replacement.pop_back();
            }
            replacement += ownerSuffix;
            for (const editor::dap::DapManager::Variable& variable : variables) {
                replacement += "\n" + FormatDebugVariableLine(variable, indent + 2, reference);
            }

            // The *debug* buffer is read-only against user edits; this is a
            // programmatic splice, so the flag is lifted just around it --
            // same pragmatism as Buffer::AppendWhileReadOnly, which only
            // covers appends and can't do a mid-buffer splice.
            const bool wasReadOnly = target.ReadOnly();
            target.SetReadOnly(false);
            target.DeleteRange(targetLineStart, targetLineEnd - targetLineStart);
            target.InsertAt(targetLineStart, replacement);
            target.SetReadOnly(wasReadOnly);
            statusMessage_.clear();
        });
}

// Debugging wishlist follow-up (pointer/linked-list graph view). See
// PointerGraphSession's own doc comment in BufferView.h for the overall
// session shape -- this block mirrors RequestHierarchyAtPoint/
// ExpandHierarchyNode/PushHierarchyModel/EndHierarchySession/the five
// Hierarchy* routers exactly, over DAP variables instead of LSP hierarchy
// items, plus the cycle-detection ExpandPointerGraphNode adds. Placed here
// (not alongside the hierarchy block) so it can use ParseDebugVariableLine/
// FormatDebugVariableLine, both file-local to this translation unit.

void BufferView::RequestPointerGraphAtPoint() {
    if (!dapManager_ || dapManager_->State() != editor::dap::DapManager::SessionState::Stopped) {
        statusMessage_ = "Not stopped (nothing to inspect).";
        return;
    }
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    const std::optional<ParsedDebugVariableLine> parsed = ParseDebugVariableLine(lineText);
    if (!parsed || parsed->variablesReference <= 0) {
        statusMessage_ = "No expandable variable on this line.";
        return;
    }

    editor::PointerGraphNode root{.name               = parsed->name,
                                  .type               = parsed->type,
                                  .value              = parsed->value,
                                  .memoryReference    = parsed->memoryReference,
                                  .variablesReference = parsed->variablesReference};
    PointerGraphSession      session{.rootName = root.name};
    if (!root.memoryReference.empty()) {
        session.visitedMemoryRefs.insert(root.memoryReference);
    }
    session.tree.Reset({std::move(root)});
    pointerGraphSession_       = std::move(session);
    pointerGraphSelectedIndex_ = 0;
    ExpandPointerGraphNode(0); // auto-expand the root -- RequestHierarchyAtPoint's own precedent
}

void BufferView::ExpandPointerGraphNode(std::size_t index) {
    if (!pointerGraphSession_ || !dapManager_ || index >= pointerGraphSession_->tree.Size()) {
        return;
    }
    PointerGraphSession& session = *pointerGraphSession_;
    if (session.tree.IsLoading(index)) {
        return;
    }
    if (session.tree.ChildrenFetched(index)) {
        // Already explored -- just reveal it again, no request needed --
        // ExpandHierarchyNode's own reasoning.
        session.tree.SetExpanded(index, true);
        PushPointerGraphModel();
        return;
    }
    const editor::PointerGraphNode& node = session.tree.At(index).data;
    if (node.variablesReference <= 0) {
        return; // a cyclic node (or otherwise not expandable) -- nothing to fetch
    }

    session.tree.BeginLoading(index);
    PushPointerGraphModel(); // shows the loading glyph immediately

    const int         variablesReference = node.variablesReference;
    const std::size_t generation         = pointerGraphRequest_.Begin();
    dapManager_->RequestVariables(
        variablesReference,
        [this, index, generation](std::vector<editor::dap::DapManager::Variable> variables) {
            if (!pointerGraphSession_ || pointerGraphRequest_.IsStale(generation)) {
                return; // superseded by a newer request, or the session ended -- ExpandHierarchyNode's own guard
            }
            PointerGraphSession&                  session = *pointerGraphSession_;
            std::vector<editor::PointerGraphNode> children;
            children.reserve(variables.size());
            for (editor::dap::DapManager::Variable& variable : variables) {
                editor::PointerGraphNode child{.name               = std::move(variable.name),
                                               .type               = std::move(variable.type),
                                               .value              = std::move(variable.value),
                                               .memoryReference    = std::move(variable.memoryReference),
                                               .variablesReference = variable.variablesReference};
                // A real linked/circular list can point back into a node
                // already shown above it in this same session -- unlike an
                // LSP call/type hierarchy (acyclic by construction), so this
                // is the one thing ExpandHierarchyNode never had to guard
                // against. Forcing variablesReference to 0 here (rather than
                // just setting cyclic) is what actually stops the tree from
                // growing forever; cyclic only exists so the row label can
                // say why it stopped.
                if (!child.memoryReference.empty() && session.visitedMemoryRefs.contains(child.memoryReference)) {
                    child.cyclic             = true;
                    child.variablesReference = 0;
                }
                else if (!child.memoryReference.empty()) {
                    session.visitedMemoryRefs.insert(child.memoryReference);
                }
                children.push_back(std::move(child));
            }
            session.tree.Expand(index, std::move(children));
            PushPointerGraphModel();
        });
}

void BufferView::PushPointerGraphModel() {
    if (!pointerGraphSession_) {
        if (onPointerGraphChanged_) {
            onPointerGraphChanged_(std::nullopt);
        }
        return;
    }

    const PointerGraphSession& session = *pointerGraphSession_;
    ui::TreeViewModel          model;
    model.title = "Pointer graph: " + session.rootName;

    const std::vector<editor::ExpandableTree<editor::PointerGraphNode>::VisibleRow> rows = session.tree.FlattenVisible();
    model.rows.reserve(rows.size());
    for (const auto& row : rows) {
        const auto& node = session.tree.At(row.index);
        model.rows.push_back(ui::TreeRow{
            .label = editor::FormatPointerGraphLabel(node.data),
            .depth = row.depth,
            // A cyclic node's variablesReference is forced to 0 the moment
            // it's detected (see ExpandPointerGraphNode), but a freshly
            // appended ExpandableTree child always starts childrenFetched
            // == false regardless -- without this explicit check a cyclic
            // leaf would still show a (dead) expand affordance.
            .hasChildren = !node.data.cyclic && (!node.childrenFetched || !node.children.empty()),
            .expanded    = node.expanded,
            .loading     = node.loading,
        });
    }
    if (!model.rows.empty()) {
        model.selectedIndex = std::min(pointerGraphSelectedIndex_, model.rows.size() - 1);
    }

    if (onPointerGraphChanged_) {
        onPointerGraphChanged_(std::move(model));
    }
}

void BufferView::EndPointerGraphSession() {
    pointerGraphSession_.reset();
    pointerGraphSelectedIndex_ = 0;
    if (onPointerGraphChanged_) {
        onPointerGraphChanged_(std::nullopt);
    }
    TakeFocus(); // reclaim keyboard focus from the TreeView overlay
}

void BufferView::SetOnPointerGraphChanged(std::function<void(std::optional<ui::TreeViewModel>)> handler) {
    onPointerGraphChanged_ = std::move(handler);
}

void BufferView::PointerGraphActivate(std::size_t index) {
    // v1 scope cut (see this method's own doc comment in BufferView.h): no
    // natural "jump to source" target for a plain runtime variable, so
    // Activate just toggles expand/collapse like ToggleExpand does.
    PointerGraphToggleExpand(index);
}

void BufferView::PointerGraphToggleExpand(std::size_t index) {
    pointerGraphSelectedIndex_ = index;
    ExpandPointerGraphNode(index);
}

void BufferView::PointerGraphCollapse(std::size_t index) {
    if (!pointerGraphSession_ || index >= pointerGraphSession_->tree.Size()) {
        return;
    }
    pointerGraphSelectedIndex_ = index;
    pointerGraphSession_->tree.SetExpanded(index, false);
    PushPointerGraphModel();
}

void BufferView::PointerGraphCancel() {
    EndPointerGraphSession();
}

void BufferView::PointerGraphSelectionChanged(std::size_t index) {
    pointerGraphSelectedIndex_ = index;
}

void BufferView::SetOnMemoryImageChanged(std::function<void(std::optional<ui::MemoryImageModel>)> handler) {
    onMemoryImageChanged_ = std::move(handler);
}

void BufferView::MemoryImageCancel() {
    if (onMemoryImageChanged_) {
        onMemoryImageChanged_(std::nullopt);
    }
    TakeFocus(); // reclaim keyboard focus from the MemoryImageView overlay
}

void BufferView::RestartFrameAtPoint() {
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    const std::size_t markerPos = lineText.rfind("[frame:");
    int               frameId   = 0;
    if (markerPos != std::string::npos) {
        try {
            frameId = std::stoi(lineText.substr(markerPos + 7)); // stoi stops at the closing ']'
        }
        catch (const std::exception&) {
            frameId = 0;
        }
    }
    if (markerPos == std::string::npos) {
        statusMessage_ = "Not a stack frame line.";
        return;
    }
    statusMessage_ = dapManager_->RestartFrame(frameId);
}

void BufferView::ShowDisassemblyAtPoint() {
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    // "[frame:N]" is optional here (unlike RestartFrameAtPoint, which
    // refuses without one) -- 0 means "no preference," falling back to the
    // top stopped frame below.
    const std::size_t markerPos        = lineText.rfind("[frame:");
    int               requestedFrameId = 0;
    if (markerPos != std::string::npos) {
        try {
            requestedFrameId = std::stoi(lineText.substr(markerPos + 7)); // stoi stops at the closing ']'
        }
        catch (const std::exception&) {
            requestedFrameId = 0;
        }
    }

    statusMessage_ = "Fetching instructions...";
    dapManager_->RequestStackTrace([this, requestedFrameId](std::vector<editor::dap::DapManager::StackFrame> frames) {
        if (frames.empty()) {
            statusMessage_ = "No stack to disassemble (is the session stopped?).";
            return;
        }
        const editor::dap::DapManager::StackFrame* target = &frames[0];
        if (requestedFrameId != 0) {
            for (const editor::dap::DapManager::StackFrame& frame : frames) {
                if (frame.id == requestedFrameId) {
                    target = &frame;
                    break;
                }
            }
        }
        if (target->instructionPointerReference.empty()) {
            statusMessage_ = "No instruction pointer for this frame (adapter didn't report one).";
            return;
        }
        const std::string pcAddress = target->instructionPointerReference;
        // A fixed, generous window centered on the PC -- ShowDebugInfo's own
        // "one shot, re-invoke to refresh" model; no incremental paging.
        dapManager_->RequestDisassembly(
            pcAddress, -32, 64,
            [this, pcAddress](std::vector<editor::dap::DapManager::DisassembledInstruction> instructions) {
                if (instructions.empty()) {
                    statusMessage_ = "No instructions returned (adapter may not support disassembly).";
                    return;
                }
                BuildDisassemblyBuffer(instructions, pcAddress);
            });
    });
}

void BufferView::BuildDisassemblyBuffer(const std::vector<editor::dap::DapManager::DisassembledInstruction>& instructions,
                                        const std::string&                                                   pcAddress) {
    std::string text;
    for (const editor::dap::DapManager::DisassembledInstruction& instruction : instructions) {
        std::string line;
        if (instruction.path) {
            // The established "path:line: text" results convention, so
            // C-c C-v (project-search-visit-result) jumps to a located
            // instruction's source line with zero new navigation plumbing.
            line += instruction.path->string() + ":" + std::to_string(instruction.line) + ": ";
        }
        line += (instruction.address == pcAddress) ? "-> " : "   ";
        line += instruction.address;
        if (!instruction.instructionBytes.empty()) {
            line += "  " + instruction.instructionBytes;
        }
        line += "  " + instruction.instruction;
        text += line + "\n";
    }
    text::Buffer& disassembly = bufferList_.CreateBuffer("*disassembly*");
    disassembly.InsertAtPoint(text);
    disassembly.SetPoint(0);
    disassembly.SetReadOnly(true); // same tossable-read-only reasoning as BuildDebugBuffer
    activeBuffer_.Set(disassembly);
    statusMessage_ = "C-c C-v visits a located instruction's source line.";
}

void BufferView::RemoveWatchAtPoint() {
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    const std::size_t markerPos = lineText.rfind("[watch:");
    if (markerPos == std::string::npos) {
        statusMessage_ = "No watch on this line.";
        return;
    }
    std::size_t index = 0;
    try {
        index = static_cast<std::size_t>(std::stoul(lineText.substr(markerPos + 7))); // stoul stops at the closing ']'
    }
    catch (const std::exception&) {
        statusMessage_ = "No watch on this line.";
        return;
    }
    dapManager_->RemoveWatchAt(index);
    ShowDebugInfo();
}

void BufferView::SetVariableAtPoint() {
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    const std::optional<ParsedDebugVariableLine> parsed = ParseDebugVariableLine(lineText);
    if (!parsed || parsed->ownerRef <= 0) {
        statusMessage_ = "Not an editable variable line.";
        return;
    }
    const int         ownerRef = parsed->ownerRef;
    const std::string name     = parsed->name;

    pendingDapSetVariable_ = PendingDapSetVariable{
        .buffer = &buffer, .line = line, .lineText = lineText, .ownerRef = ownerRef, .name = name};
    inputMode_ = InputMode::DapSetVariableValue;
    prompt_.emplace("New value for " + name + ": ");
    statusMessage_ = prompt_->StatusText();
}

void BufferView::ToggleHexFormatAtPoint() {
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    // A trailing "[hex]" marker (this method's own addition, never present
    // on a freshly-built ShowDebugInfo line) is both the display hint the
    // line was last fetched with and this toggle's only state -- present
    // means flip back to decimal, absent means switch to hex.
    const bool wantHex = lineText.find("  [hex]") == std::string::npos;

    text::Buffer* const bufferPtr = &buffer;
    // Shared staleness guard both branches below use before splicing --
    // ExpandVariableAtPoint/SetVariableAtPoint's own "did the line change
    // while the request was in flight" check.
    auto spliceIfUnchanged = [this, bufferPtr, line, lineText](const std::string& replacement) {
        if (bufferPtr != &activeBuffer_.Get()) {
            return; // switched away while the request was in flight
        }
        const text::ITextStorage& targetContent = bufferPtr->Content();
        if (line >= targetContent.LineCount()) {
            return;
        }
        const std::size_t targetLineStart = targetContent.LineToByteOffset(line);
        const std::size_t targetLineEnd =
            (line + 1 < targetContent.LineCount()) ? targetContent.LineToByteOffset(line + 1) - 1 : targetContent.ByteLength();
        if (targetContent.Substring(targetLineStart, targetLineEnd - targetLineStart) != lineText) {
            statusMessage_ = "Debug line changed -- not reformatting.";
            return;
        }
        const bool wasReadOnly = bufferPtr->ReadOnly();
        bufferPtr->SetReadOnly(false);
        bufferPtr->DeleteRange(targetLineStart, targetLineEnd - targetLineStart);
        bufferPtr->InsertAt(targetLineStart, replacement);
        bufferPtr->SetReadOnly(wasReadOnly);
        statusMessage_.clear();
    };

    const std::size_t watchMarkerPos = lineText.rfind("[watch:");
    if (watchMarkerPos != std::string::npos) {
        std::size_t watchIndex = 0;
        try {
            watchIndex = static_cast<std::size_t>(std::stoul(lineText.substr(watchMarkerPos + 7))); // stoul stops at ']'
        }
        catch (const std::exception&) {
            statusMessage_ = "No watch on this line.";
            return;
        }
        const std::vector<std::string>& watches = dapManager_->Watches();
        if (watchIndex >= watches.size()) {
            statusMessage_ = "Watch no longer exists.";
            return;
        }
        const std::string expression = watches[watchIndex];
        statusMessage_               = "Formatting...";
        dapManager_->Evaluate(
            expression,
            [spliceIfUnchanged, watchIndex, expression, wantHex](bool success, std::string text) {
                std::string replacement = "  " + expression + " = " + (success ? text : ("<" + text + ">")) + "  [watch:" +
                                          std::to_string(watchIndex) + "]";
                if (wantHex) {
                    replacement += "  [hex]";
                }
                spliceIfUnchanged(replacement);
            },
            "watch", wantHex);
        return;
    }

    const std::optional<ParsedDebugVariableLine> parsed = ParseDebugVariableLine(lineText);
    if (!parsed || parsed->ownerRef <= 0) {
        statusMessage_ = "No formattable value on this line.";
        return;
    }
    const int         ownerRef = parsed->ownerRef;
    const std::size_t indent   = parsed->indent;
    const std::string name     = parsed->name;

    statusMessage_ = "Formatting...";
    dapManager_->RequestVariables(
        ownerRef,
        [this, bufferPtr, spliceIfUnchanged, name, ownerRef, indent, wantHex](std::vector<editor::dap::DapManager::Variable> variables) {
            if (bufferPtr != &activeBuffer_.Get()) {
                return; // switched away while the request was in flight
            }
            const auto it = std::find_if(variables.begin(), variables.end(),
                                         [&name](const editor::dap::DapManager::Variable& v) { return v.name == name; });
            if (it == variables.end()) {
                statusMessage_ = "Variable no longer available (or the session already resumed).";
                return;
            }
            spliceIfUnchanged(FormatDebugVariableLine(*it, indent, ownerRef, wantHex));
        },
        wantHex);
}

void BufferView::ToggleWatchGraphAtPoint() {
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    const std::size_t watchMarkerPos = lineText.rfind("[watch:");
    if (watchMarkerPos == std::string::npos) {
        statusMessage_ = "Not a watch line -- graphing only applies to watch expressions.";
        return;
    }
    std::size_t watchIndex = 0;
    try {
        watchIndex = static_cast<std::size_t>(std::stoul(lineText.substr(watchMarkerPos + 7))); // stoul stops at ']'
    }
    catch (const std::exception&) {
        statusMessage_ = "No watch on this line.";
        return;
    }
    const std::vector<std::string>& watches = dapManager_->Watches();
    if (watchIndex >= watches.size()) {
        statusMessage_ = "Watch no longer exists.";
        return;
    }
    const std::string expression = watches[watchIndex];

    text::Buffer* const bufferPtr = &buffer;
    // ExpandVariableAtPoint/ToggleHexFormatAtPoint's own staleness-guarded
    // splice shape -- reject a splice if the line changed underneath an
    // in-flight request.
    auto spliceIfUnchanged = [this, bufferPtr, line, lineText](const std::string& replacement) {
        if (bufferPtr != &activeBuffer_.Get()) {
            return; // switched away while the request was in flight
        }
        const text::ITextStorage& targetContent = bufferPtr->Content();
        if (line >= targetContent.LineCount()) {
            return;
        }
        const std::size_t targetLineStart = targetContent.LineToByteOffset(line);
        const std::size_t targetLineEnd =
            (line + 1 < targetContent.LineCount()) ? targetContent.LineToByteOffset(line + 1) - 1 : targetContent.ByteLength();
        if (targetContent.Substring(targetLineStart, targetLineEnd - targetLineStart) != lineText) {
            statusMessage_ = "Debug line changed -- not graphing.";
            return;
        }
        const bool wasReadOnly = bufferPtr->ReadOnly();
        bufferPtr->SetReadOnly(false);
        bufferPtr->DeleteRange(targetLineStart, targetLineEnd - targetLineStart);
        bufferPtr->InsertAt(targetLineStart, replacement);
        bufferPtr->SetReadOnly(wasReadOnly);
        statusMessage_.clear();
    };

    // A trailing "[graph]" marker is this toggle's only state -- present
    // means strip it back to the plain line, absent means append a graph.
    if (lineText.find("  [graph]") != std::string::npos) {
        spliceIfUnchanged(lineText.substr(0, watchMarkerPos) + "[watch:" + std::to_string(watchIndex) + "]");
        return;
    }

    const std::vector<double>& history = dapManager_->WatchHistoryAt(watchIndex);
    if (history.size() >= 2) {
        spliceIfUnchanged(lineText + "  " + editor::BuildBlockSparkline(history) + "  [graph]");
        return;
    }

    // No scalar history yet -- try a one-shot numeric-array snapshot of the
    // watch's current value instead (an expandable value, every child
    // numeric).
    statusMessage_ = "Graphing...";
    dapManager_->EvaluateWithReference(
        expression,
        [this, bufferPtr, spliceIfUnchanged, lineText](editor::dap::DapManager::EvaluateResult result) {
            if (bufferPtr != &activeBuffer_.Get()) {
                return; // switched away while the request was in flight
            }
            if (!result.success || result.variablesReference <= 0) {
                statusMessage_ = "Not enough history yet, and not an expandable/numeric-array value.";
                return;
            }
            dapManager_->RequestVariables(
                result.variablesReference,
                [this, spliceIfUnchanged, lineText](std::vector<editor::dap::DapManager::Variable> variables) {
                    if (variables.empty()) {
                        statusMessage_ = "No elements to graph.";
                        return;
                    }
                    std::vector<double> values;
                    values.reserve(variables.size());
                    for (const auto& v : variables) {
                        double parsed = 0.0;
                        if (!editor::TryParseNumeric(v.value, parsed)) {
                            statusMessage_ = "Not every element is numeric -- can't graph.";
                            return;
                        }
                        values.push_back(parsed);
                    }
                    spliceIfUnchanged(lineText + "  " + editor::BuildBlockSparkline(values) + "  [graph]");
                });
        },
        "watch");
}

void BufferView::LineInspectAtPoint() {
    if (dapManager_->State() != editor::dap::DapManager::SessionState::Stopped) {
        statusMessage_ = "Not stopped (nothing to inspect).";
        return;
    }
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();

    if (!mode_.lineInspect) {
        statusMessage_ = "No expression extraction available for this mode.";
        return;
    }
    const std::vector<std::pair<std::size_t, std::size_t>> candidates = mode_.lineInspect(buffer.Text(), lineStart, lineEnd);
    if (candidates.empty()) {
        statusMessage_ = "No expressions found on this line.";
        return;
    }

    text::Buffer* const bufferPtr = &buffer;
    lineInspect_                  = LineInspectState{
        .buffer = bufferPtr, .contentGeneration = buffer.ContentGeneration(), .line = line, .ranges = candidates};

    statusMessage_ = "Inspecting...";
    // Same shared-counter fan-out-then-assemble shape ShowDebugInfo's own
    // watch/scope evaluation uses -- every callback runs on the main thread
    // (DapClient's own threading contract), so a plain shared counter
    // covering every candidate is race-free; results land in a fixed slot
    // per candidate so the final message reads left-to-right regardless of
    // response interleaving. A failed evaluation is wrapped in "<...>",
    // ShowDebugInfo's watch-fan-out convention.
    auto       texts     = std::make_shared<std::vector<std::string>>(candidates.size());
    auto       remaining = std::make_shared<std::size_t>(candidates.size());
    const bool capped    = candidates.size() >= editor::kMaxLineInspectExpressions;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto [start, end]      = candidates[i];
        const std::string expression = std::string(content.Substring(start, end - start));
        dapManager_->Evaluate(expression, [this, bufferPtr, texts, remaining, i, expression, capped](bool success, std::string text) {
            (*texts)[i] = expression + " = " + (success ? text : ("<" + text + ">"));
            if (--*remaining != 0) {
                return;
            }
            if (bufferPtr != &activeBuffer_.Get()) {
                return; // switched away while the requests were in flight
            }
            std::string message;
            for (std::size_t j = 0; j < texts->size(); ++j) {
                if (j > 0) {
                    message += "  ";
                }
                message += (*texts)[j];
            }
            if (capped) {
                message += "  (showing first " + std::to_string(editor::kMaxLineInspectExpressions) + ")";
            }
            statusMessage_ = std::move(message);
        });
    }
}

void BufferView::ShowMemoryAtPoint() {
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    const std::size_t markerPos = lineText.rfind("[mem:");
    const std::size_t closePos  = (markerPos == std::string::npos) ? std::string::npos : lineText.find(']', markerPos + 5);
    if (markerPos == std::string::npos || closePos == std::string::npos) {
        statusMessage_ = "No memory reference on this line.";
        return;
    }

    pendingDapMemoryReference_ = lineText.substr(markerPos + 5, closePos - (markerPos + 5));
    pendingDapMemoryAsImage_   = false;
    inputMode_                 = InputMode::DapMemoryByteCount;
    prompt_.emplace("Byte count (default 128): ");
    statusMessage_ = prompt_->StatusText();
}

void BufferView::ShowMemoryImageAtPoint() {
    // ShowMemoryAtPoint's own "[mem:<ref>]"-parse body, duplicated rather
    // than shared (TreeView.cpp's own PaintRowText precedent -- small
    // enough that a new shared helper isn't worth it), differing only in
    // which flag it sets before entering the shared DapMemoryByteCount
    // prompt.
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    const std::size_t markerPos = lineText.rfind("[mem:");
    const std::size_t closePos  = (markerPos == std::string::npos) ? std::string::npos : lineText.find(']', markerPos + 5);
    if (markerPos == std::string::npos || closePos == std::string::npos) {
        statusMessage_ = "No memory reference on this line.";
        return;
    }

    pendingDapMemoryReference_ = lineText.substr(markerPos + 5, closePos - (markerPos + 5));
    pendingDapMemoryAsImage_   = true;
    inputMode_                 = InputMode::DapMemoryByteCount;
    prompt_.emplace("Byte count (default 128): ");
    statusMessage_ = prompt_->StatusText();
}

void BufferView::BuildMemoryBuffer(const std::string& memoryReference, const editor::dap::DapManager::MemoryBlock& block) {
    constexpr std::size_t kBytesPerRow = 16;

    std::string text = "Memory at " + memoryReference;
    if (!block.address.empty() && block.address != memoryReference) {
        text += " (" + block.address + ")";
    }
    text += "\n";
    for (std::size_t offset = 0; offset < block.data.size(); offset += kBytesPerRow) {
        const std::size_t  rowLen = std::min(kBytesPerRow, block.data.size() - offset);
        std::ostringstream row;
        row << std::hex << std::setfill('0') << std::setw(8) << offset << "  ";
        std::string ascii;
        for (std::size_t i = 0; i < kBytesPerRow; ++i) {
            if (i < rowLen) {
                const auto byte = block.data[offset + i];
                row << std::setw(2) << static_cast<unsigned>(byte) << ' ';
                ascii += (byte >= 0x20 && byte < 0x7f) ? static_cast<char>(byte) : '.';
            }
            else {
                row << "   ";
            }
        }
        text += row.str() + " |" + ascii + "|\n";
    }
    if (block.unreadableBytes > 0) {
        text += std::to_string(block.unreadableBytes) + " byte(s) unreadable.\n";
    }

    text::Buffer& memory = bufferList_.CreateBuffer("*memory*");
    memory.InsertAtPoint(text);
    memory.SetPoint(0);
    memory.SetReadOnly(true); // same tossable-read-only reasoning as BuildDebugBuffer
    activeBuffer_.Set(memory);
    statusMessage_.clear();
}

void BufferView::PushMemoryImageModel(const std::string& memoryReference, const editor::dap::DapManager::MemoryBlock& block) {
    ui::MemoryImageModel model;
    model.title = "Memory image: " + memoryReference + " (" + std::to_string(block.data.size()) + " bytes)";
    if (!block.address.empty() && block.address != memoryReference) {
        model.title += " [" + block.address + "]";
    }
    model.bytes = block.data;

    if (onMemoryImageChanged_) {
        onMemoryImageChanged_(model);
    }
    statusMessage_.clear();
}

void BufferView::BeginDapThreadSelect() {
    if (dapManager_->State() != editor::dap::DapManager::SessionState::Stopped) {
        statusMessage_ = "Not stopped (nothing to pick a thread in).";
        return;
    }
    statusMessage_ = "Fetching threads...";
    dapManager_->RequestThreads([this](std::vector<editor::dap::DapManager::Thread> threads) {
        if (threads.empty()) {
            statusMessage_ = "No threads reported (or the session already resumed).";
            return;
        }
        pendingDapThreads_  = std::move(threads);
        dapThreadSelection_ = 0;
        inputMode_          = InputMode::DapThreadSelect;
        RefreshDapThreadSelectStatus();
    });
}

void BufferView::RefreshDapThreadSelectStatus() {
    std::string status = "Select thread: ";
    for (std::size_t i = 0; i < pendingDapThreads_.size(); ++i) {
        if (i > 0) {
            status += "  ";
        }
        const bool selected = (i == dapThreadSelection_);
        status += (selected ? "[" : "") + std::to_string(i + 1) + ") " + pendingDapThreads_[i].name + (selected ? "]" : "");
    }
    statusMessage_ = status;
}

void BufferView::HandleDapThreadSelectKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_ = "Thread selection cancelled.";
        EndInteractiveSession();
        return;
    }
    if (chord.Special == editor::SpecialKey::Down) {
        dapThreadSelection_ = (dapThreadSelection_ + 1) % pendingDapThreads_.size();
        RefreshDapThreadSelectStatus();
        return;
    }
    if (chord.Special == editor::SpecialKey::Up) {
        dapThreadSelection_ = (dapThreadSelection_ + pendingDapThreads_.size() - 1) % pendingDapThreads_.size();
        RefreshDapThreadSelectStatus();
        return;
    }
    std::size_t chosen = dapThreadSelection_;
    if (IsPlainCharacter(chord) && chord.Codepoint >= U'1' && chord.Codepoint <= U'9') {
        const std::size_t index = static_cast<std::size_t>(chord.Codepoint - U'1');
        if (index >= pendingDapThreads_.size()) {
            return; // out of range -- stay in the selection list
        }
        chosen = index;
    }
    else if (chord.Special != editor::SpecialKey::Enter) {
        return; // anything else is ignored -- stay in the selection list
    }

    const editor::dap::DapManager::Thread thread = pendingDapThreads_[chosen];
    dapManager_->SelectThread(thread.id, [this, name = thread.name](bool success) {
        statusMessage_ = success ? ("Selected thread: " + name) : "Failed to select thread.";
    });
    EndInteractiveSession();
}

// DAP round 3: BeginDapThreadSelect's own shape, but a live/local toggle set
// (pendingDapEnabledExceptionFilters_) rather than a single pick -- nothing
// reaches the adapter until Enter commits it via
// DapManager::SetExceptionBreakpointFilters.

void BufferView::BeginDapExceptionFilterSelect() {
    const std::vector<editor::dap::DapManager::ExceptionFilter>& filters = dapManager_->AvailableExceptionFilters();
    if (filters.empty()) {
        statusMessage_ = "No exception breakpoint filters available (no session, or the adapter doesn't advertise any).";
        return;
    }
    pendingDapExceptionFilters_        = filters;
    pendingDapEnabledExceptionFilters_ = dapManager_->EnabledExceptionFilters();
    dapExceptionFilterSelection_       = 0;
    inputMode_                         = InputMode::DapExceptionFilterSelect;
    RefreshDapExceptionFilterStatus();
}

void BufferView::RefreshDapExceptionFilterStatus() {
    std::string status = "Exception breakpoints (space/digit toggle, enter apply): ";
    for (std::size_t i = 0; i < pendingDapExceptionFilters_.size(); ++i) {
        if (i > 0) {
            status += "  ";
        }
        const editor::dap::DapManager::ExceptionFilter& filter   = pendingDapExceptionFilters_[i];
        const bool                                      checked  = pendingDapEnabledExceptionFilters_.contains(filter.id);
        const bool                                      selected = (i == dapExceptionFilterSelection_);
        status += (selected ? "[" : "") + std::string(checked ? "✓" : " ") + std::to_string(i + 1) + ") " + filter.label +
                  (selected ? "]" : "");
    }
    statusMessage_ = status;
}

void BufferView::HandleDapExceptionFilterSelectKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_ = "Exception breakpoint selection cancelled.";
        EndInteractiveSession();
        return;
    }
    if (chord.Special == editor::SpecialKey::Down) {
        dapExceptionFilterSelection_ = (dapExceptionFilterSelection_ + 1) % pendingDapExceptionFilters_.size();
        RefreshDapExceptionFilterStatus();
        return;
    }
    if (chord.Special == editor::SpecialKey::Up) {
        dapExceptionFilterSelection_ =
            (dapExceptionFilterSelection_ + pendingDapExceptionFilters_.size() - 1) % pendingDapExceptionFilters_.size();
        RefreshDapExceptionFilterStatus();
        return;
    }
    if (chord.Special == editor::SpecialKey::Enter) {
        dapManager_->SetExceptionBreakpointFilters(pendingDapEnabledExceptionFilters_);
        statusMessage_ =
            "Exception breakpoints: " + std::to_string(pendingDapEnabledExceptionFilters_.size()) + " enabled.";
        EndInteractiveSession();
        return;
    }
    std::optional<std::size_t> toggled;
    if (IsPlainCharacter(chord) && chord.Codepoint == U' ') {
        toggled = dapExceptionFilterSelection_;
    }
    else if (IsPlainCharacter(chord) && chord.Codepoint >= U'1' && chord.Codepoint <= U'9') {
        const std::size_t index = static_cast<std::size_t>(chord.Codepoint - U'1');
        if (index >= pendingDapExceptionFilters_.size()) {
            return; // out of range -- stay in the selection list
        }
        dapExceptionFilterSelection_ = index;
        toggled                      = index;
    }
    else {
        return; // anything else is ignored -- stay in the selection list
    }
    const std::string& id = pendingDapExceptionFilters_[*toggled].id;
    if (!pendingDapEnabledExceptionFilters_.insert(id).second) {
        pendingDapEnabledExceptionFilters_.erase(id); // was already enabled -- insert reported no-op, so toggle off
    }
    RefreshDapExceptionFilterStatus();
}

bufferview::ConfirmPrompt BufferView::ConfirmRevertHunkPrompt() {
    return {.cancelMessage = "Revert cancelled.", .onConfirm = [this] { RevertHunkAtPoint(); }};
}

void BufferView::HandleConfirmRevertHunkKey(const editor::KeyChord& chord) {
    HandleConfirmPromptKey(ConfirmRevertHunkPrompt(), chord);
}

bufferview::FuzzyPrompt BufferView::VcsSwitchBranchPrompt() {
    return {.list          = &vcsBranchList_,
            .historyKey    = "vcs-switch-branch",
            .cancelMessage = "Switch branch cancelled.",
            .emptyMessage  = [](const std::string& query) { return "No branch matching \"" + query + "\""; },
            .commit        = [this](const std::string& selected) {
                if (!vcsRunner_) {
                    statusMessage_ = "no vcs runner configured";
                    return;
                }
                // A branch switch rewrites the working tree underneath any open
                // buffer. Unmodified buffers catch up on the next auto-revert
                // tick; a *modified* buffer is left alone, its save hitting the
                // supersession y/n rather than a confusing stale-content
                // overwrite.
                statusMessage_ = "Switching to " + selected + "...";
                vcsRunner_->RequestBranchSwitch(
                    selected,
                    [this, selected] {
                        statusMessage_ = "Switched to " + selected + " (modified buffers not reloaded)";
                        RefreshVcsStatusBuffer();
                        RequestDiffForCurrentBuffer();
                    },
                    [this](std::string error) { statusMessage_ = "vcs branch: " + error; }); }};
}

void BufferView::RefreshVcsSwitchBranchStatus() {
    RefreshFuzzyPrompt(VcsSwitchBranchPrompt());
}

void BufferView::HandleVcsSwitchBranchKey(const editor::KeyChord& chord) {
    HandleFuzzyPromptKey(VcsSwitchBranchPrompt(), chord);
}

// dropdown-path-completion follow-up: RefreshSwitchToBufferStatus's own
// shape, over editor::acp::AcpAgentNames() (a static configured list --
// same "no create-new case" reasoning as switch-to-buffer above).

void BufferView::SetVcsPanel(VcsPanel* panel) {
    vcsPanel_ = panel;
}

void BufferView::RequestVcsAction(VcsPanelAction action) {
    switch (action) {
        case VcsPanelAction::Commit:
            BeginVcsCommitMessage();
            return;
        case VcsPanelAction::SwitchBranch:
            BeginVcsSwitchBranchPrompt();
            return;
        case VcsPanelAction::CreateBranch:
            BeginVcsCreateBranchPrompt();
            return;
    }
}

void BufferView::SetDapManager(editor::dap::DapManager* dapManager) {
    dapManager_ = dapManager;
}

} // namespace ned::ui
