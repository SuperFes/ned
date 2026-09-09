//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// Builders for the read-only derived buffers (search results, agenda, clock report,
// diagnostics, messages) and link resolution/activation.
//

#include "UI/BufferView/Internal.h"

namespace ned::ui {

// The file-local helpers these definitions call live in BufferView/Internal.h
// now that several parts share them -- see that header. This using-directive is
// what let the split leave every call site untouched.
using namespace detail;

namespace {

    std::size_t CountUniqueMatchFiles(const std::vector<editor::SearchMatch>& matches) {
        std::vector<std::filesystem::path> seen;
        for (const editor::SearchMatch& match : matches) {
            if (std::find(seen.begin(), seen.end(), match.file) == seen.end()) {
                seen.push_back(match.file);
            }
        }
        return seen.size();
    }

} // namespace

void BufferView::VisitSearchResult() {
    VisitResultUnderPoint();
}

std::optional<std::filesystem::path> BufferView::ResolveResultPath(const std::filesystem::path& path) const {
    if (path.empty()) {
        return std::nullopt;
    }
    std::error_code ec;
    if (bufferList_.FindByPath(path) != nullptr || std::filesystem::exists(path, ec)) {
        return path;
    }
    if (path.is_relative()) {
        const std::filesystem::path rooted = editor::ProjectRoot() / path;
        if (bufferList_.FindByPath(rooted) != nullptr || std::filesystem::exists(rooted, ec)) {
            return rooted;
        }
    }
    return std::nullopt;
}

void BufferView::JumpToPathLine(const std::filesystem::path& path, std::size_t line) {
    // Multibuffer-gaps follow-up: a path that resolves to nothing is
    // reported, not opened. OpenOrCreateFile creates on miss by design (what
    // find-file on a new path needs), which used to make a stale/unresolvable
    // results line silently produce an empty scratch buffer named after it --
    // indistinguishable, at a glance, from a real file that happens to be
    // empty.
    const std::optional<std::filesystem::path> resolved = ResolveResultPath(path);
    if (!resolved) {
        statusMessage_ = "No such file: " + path.string();
        return;
    }
    try {
        text::Buffer& opened = bufferList_.OpenOrCreateFile(*resolved);
        activeBuffer_.Set(opened);
        opened.SetPoint(opened.ByteOffsetForLineAndColumn(line - 1, 0)); // 1-indexed -> 0-indexed
        statusMessage_.clear();
        viewport_.ScrollToShowPoint();
    }
    catch (const std::exception& e) {
        ReportError(e.what());
    }
}

void BufferView::JumpToPathByte(const std::filesystem::path& path, std::size_t byteOffset) {
    const std::optional<std::filesystem::path> resolved = ResolveResultPath(path);
    if (!resolved) {
        statusMessage_ = "No such file: " + path.string();
        return;
    }
    try {
        text::Buffer& opened = bufferList_.OpenOrCreateFile(*resolved);
        activeBuffer_.Set(opened);
        const std::size_t clamped = std::min(byteOffset, opened.Content().ByteLength());
        opened.SetPoint(text::SnapToGraphemeBoundary(opened.Content(), clamped));
        statusMessage_.clear();
        viewport_.ScrollToShowPoint();
    }
    catch (const std::exception& e) {
        ReportError(e.what());
    }
}

void BufferView::VisitResultUnderPoint() {
    const text::Buffer& buffer = activeBuffer_.Get();

    // Multibuffers follow-up: a *vcs diff*/*diagnostics*/*references* buffer
    // carries a MultibufferIndex mapping the whole composite byte space back
    // to (source path, source line) -- this works from any line inside an
    // excerpt's body, not just a single "path:line:" index line the
    // regex-based fallback below requires, so it takes over entirely once a
    // buffer is one of these (never falls through to the regex path for the
    // same buffer).
    if (editor::multibuffer::MultibufferIndex* index = editor::multibuffer::MultibufferIndexFor(buffer)) {
        if (const editor::multibuffer::ExcerptSpan* span = index->SpanAtOffset(buffer.Point());
            span && span->sourceStartLine > 0) {
            JumpToExcerptSource(*span);
        }
        return;
    }

    // Full commit diff view follow-up: a *vcs log <name>* buffer has no
    // per-line source location (BuildVcsLogBuffer's own doc comment already
    // says as much) -- but its line does carry the one thing a commit diff
    // needs, the commit's own hash as the line's leading whitespace-
    // delimited token ("<hash> <date> <author>: <summary>"). A silent no-op
    // on a blank/malformed line, same posture as the regex fallback below.
    if (buffer.Name().starts_with("*vcs log ")) {
        const text::ITextStorage& logContent   = buffer.Content();
        const std::size_t         logLine      = logContent.ByteOffsetToLine(buffer.Point());
        const std::size_t         logLineStart = logContent.LineToByteOffset(logLine);
        const std::size_t         logLineEnd =
            (logLine + 1 < logContent.LineCount()) ? logContent.LineToByteOffset(logLine + 1) - 1 : logContent.ByteLength();
        const std::string logLineText = logContent.Substring(logLineStart, logLineEnd - logLineStart);
        const std::size_t hashEnd     = logLineText.find(' ');
        if (hashEnd != std::string::npos && hashEnd > 0) {
            RequestVcsCommitDiffBuffer(logLineText.substr(0, hashEnd));
        }
        return;
    }

    if (const std::optional<ResultLineLocation> loc = ResultLineAtPoint()) {
        JumpToPathLine(loc->path, loc->lineNumber);
    }
    // else: not a results-shaped line -- silent no-op, see this method's own header comment
}

namespace {

    // A composite excerpt body always ends in a newline (BuildMultibuffer
    // appends one if the caller's bodyText lacked it); the source slice it
    // was resolved against ends in one too -- except when the excerpt covers
    // the source's own final line and that file has no trailing newline.
    // Comparing with one trailing newline dropped from each side is what
    // keeps that single case on the byte-exact path instead of silently
    // demoting it to a line jump.
    std::string_view WithoutTrailingNewline(std::string_view text) {
        return (!text.empty() && text.back() == '\n') ? text.substr(0, text.size() - 1) : text;
    }

} // namespace

void BufferView::JumpToExcerptSource(const editor::multibuffer::ExcerptSpan& span) {
    const text::Buffer& composite = activeBuffer_.Get();
    const std::size_t   point     = composite.Point();

    // Only an editable range's body is verbatim source bytes -- see this
    // method's own doc comment in BufferView.h for what the other excerpt
    // kinds look like and why they can't be mapped this way.
    const text::Buffer::ExcerptRange* covering = nullptr;
    for (const text::Buffer::ExcerptRange& range : composite.ExcerptRanges()) {
        if (range.editable && range.sourcePath == span.sourcePath && point >= range.start && point < range.end) {
            covering = &range;
            break;
        }
    }

    if (covering != nullptr && covering->sourceEndByte >= covering->sourceStartByte) {
        const std::string bodyText = composite.Content().Substring(covering->start, covering->end - covering->start);
        // An uncommitted wgrep-style edit invalidates the offset arithmetic
        // outright (every byte typed shifts everything after it in the
        // composite but nothing in the source) -- the CommitExcerptChanges
        // "has this excerpt changed" test, reused verbatim.
        if (bodyText == covering->originalText) {
            const std::optional<std::filesystem::path> resolved = ResolveResultPath(span.sourcePath);
            const text::Buffer* const                  source   = resolved ? bufferList_.FindByPath(*resolved) : nullptr;
            // Validated against the *live* source only when it's already
            // open. An unopened source is trusted: it was read at build time
            // and reading it again here just to check would cost the whole
            // file on every jump, for a mismatch that JumpToPathByte's own
            // clamp already keeps harmless.
            bool mapped = true;
            if (source != nullptr) {
                const std::size_t sourceLength = source->Content().ByteLength();
                const std::size_t sourceEnd    = std::min(covering->sourceEndByte, sourceLength);
                mapped                         = covering->sourceStartByte <= sourceEnd &&
                                                 WithoutTrailingNewline(source->Content().Substring(covering->sourceStartByte,
                                                                                                    sourceEnd - covering->sourceStartByte)) ==
                                                     WithoutTrailingNewline(bodyText);
            }
            if (mapped) {
                JumpToPathByte(span.sourcePath,
                               std::min(covering->sourceStartByte + (point - covering->start), covering->sourceEndByte));
                return;
            }
        }
    }

    JumpToPathLine(span.sourcePath, span.sourceStartLine);
}

std::optional<BufferView::ResultLineLocation> BufferView::ResultLineAtPoint() const {
    const text::Buffer&       buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         point     = buffer.Point();
    const std::size_t         line      = content.ByteOffsetToLine(point);
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    // Matches every flat "path:line:" results-buffer format written here --
    // project-search/project-replace/agenda's HandlePromptKey/BuildResultsBuffer
    // path and BuildVcsBlameBuffer's own format both write this shape, plus
    // DiagnosticsLog::RebuildMessagesBuffer/TestResultsBuffer's own
    // "path:line: message" lines. Greedy .* correctly handles the rare case
    // of a ':' inside the path itself, by backing off to find the *last*
    // plausible ":<digits>:" split.
    static const std::regex resultLinePattern(R"(^(.*):(\d+):)");

    std::smatch match;
    if (!std::regex_search(lineText, match, resultLinePattern)) {
        return std::nullopt;
    }

    return ResultLineLocation{
        .path         = match[1].str(),
        .lineNumber   = std::stoul(match[2].str()),
        .fullLineText = lineText,
    };
}

void BufferView::BuildProjectReplaceReview(const std::vector<editor::SearchMatch>& matches, const std::string& pattern,
                                           const std::string& replacement) {
    // in-file-regex follow-up's own note applies here: the *search* ran on
    // RE2, the rewrite runs on PCRE2, which accepts essentially everything
    // RE2 does but can still throw at match time.
    const editor::RegexPattern regex(pattern);

    const std::filesystem::path root = editor::ProjectRoot();
    // Same cap, and the same honest note about what it dropped, as every
    // other multibuffer consumer -- see Editor/MultibufferLimits.h.
    const std::size_t maxExcerpts = editor::MultibufferMaxExcerpts();
    const std::size_t keptCount   = (maxExcerpts == 0) ? matches.size() : std::min(matches.size(), maxExcerpts);

    std::vector<editor::multibuffer::ExcerptSource> excerpts;
    excerpts.reserve(keptCount);
    for (std::size_t i = 0; i < keptCount; ++i) {
        const editor::SearchMatch&  match = matches[i];
        std::error_code             ec;
        const std::filesystem::path relative    = std::filesystem::relative(match.file, root, ec);
        const std::string           displayPath = (!ec && !relative.empty()) ? relative.string() : match.file.string();
        const std::string           hugeMarker  = LooksHugeSource(bufferList_, match.file) ? "  [huge]" : "";
        excerpts.push_back(editor::multibuffer::ExcerptSource{
            match.file, match.lineNumber, match.lineNumber, "▸ " + displayPath + ":" + std::to_string(match.lineNumber) + hugeMarker, match.lineText, {},
            /*editable=*/true});
    }

    // The flat preview built at ConfirmPattern time (so the match list is
    // visible while the replacement is still being typed) already carries
    // this name, as would a review left over from an earlier replace -- so
    // the new one lands beside it as "*project replace*<2>" and takes the
    // real name once the old one is gone. Built and made active *before* the
    // close so activeBuffer_ never points at a buffer being erased, and
    // closed through CloseBufferNow so any other pane showing it is
    // retargeted too.
    text::Buffer* const stale = bufferList_.Find("*project replace*");

    text::Buffer& review = editor::multibuffer::BuildMultibuffer(bufferList_, "*project replace*", excerpts, matches.size());

    // The rewrite itself, applied into the review buffer rather than into any
    // file -- which is what makes every excerpt read as "changed" against the
    // originalText snapshot BuildMultibuffer just captured, and therefore what
    // multibuffer-commit-changes will later write back. Reverse order so an
    // earlier excerpt's own offsets can't be disturbed by a later one's
    // rewrite; the range fields are copied out first because editing
    // relocates the vector they live in.
    std::size_t replacementCount = 0;
    std::size_t excerptsChanged  = 0;
    review.BeginUndoGroup();
    for (std::size_t i = review.ExcerptRanges().size(); i-- > 0;) {
        const std::size_t start = review.ExcerptRanges()[i].start;
        const std::size_t end   = review.ExcerptRanges()[i].end;
        const std::string body  = review.Content().Substring(start, end - start);

        const editor::RegexPattern::ReplaceAllResult rewritten = regex.ReplaceAll(body, replacement);
        if (rewritten.count == 0 || rewritten.text == body) {
            continue; // RE2 matched this line, PCRE2 doesn't -- left exactly as it was
        }
        review.DeleteRange(start, end - start);
        review.InsertAt(start, rewritten.text);
        replacementCount += rewritten.count;
        ++excerptsChanged;
    }
    review.EndUndoGroup();
    review.SetPoint(0);

    activeBuffer_.Set(review);
    if (stale != nullptr) {
        // Registry entry cleared explicitly: an earlier review *is* a
        // multibuffer, and a stale index keyed by a freed Buffer* is
        // inherited by whatever lands at that address next (see
        // Multibuffer.h's ClearRegistryForTesting note). Harmless for the
        // flat preview, which never had one.
        editor::multibuffer::ClearMultibufferIndexFor(*stale);
        CloseBufferNow(*stale);
        review.Rename("*project replace*");
    }
    editor::SetLastResultsBuffer("*project replace*");

    const std::size_t fileCount = CountUniqueMatchFiles(matches);
    statusMessage_              = std::to_string(replacementCount) + " replacement" + (replacementCount == 1 ? "" : "s") +
                                  " in " + std::to_string(excerptsChanged) + " line" + (excerptsChanged == 1 ? "" : "s") + " across " +
                                  std::to_string(fileCount) + " file" + (fileCount == 1 ? "" : "s") +
                                  " -- C-c C-c apply, M-c this file, M-r/M-R revert, M-n/M-p move";
}

void BufferView::ShowMessagesBuffer() {
    editor::RebuildMessagesBuffer(bufferList_);
    text::Buffer* messages = bufferList_.Find(std::string(editor::MessagesBufferName()));
    if (!messages) {
        return; // unreachable -- RebuildMessagesBuffer always finds-or-creates it
    }
    activeBuffer_.Set(*messages);
    statusMessage_.clear();
}

void BufferView::BuildAgendaMultibuffer() {
    const std::vector<editor::AgendaItem> items = editor::CollectAgendaItems(editor::ProjectRoot());

    auto sectionLabel = [](editor::AgendaSection section) -> const char* {
        switch (section) {
            case editor::AgendaSection::Overdue:
                return "Overdue";
            case editor::AgendaSection::Today:
                return "Today";
            case editor::AgendaSection::Upcoming:
                return "Upcoming";
            case editor::AgendaSection::Undated:
                return "Undated";
        }
        return "";
    };

    // One excerpt per agenda item, its own single (synthesized) body line --
    // same "header names the file/line, body is the content" shape
    // RequestDiagnosticsBuffer's own excerpts use just above. The section
    // label lives in the header text itself (items already arrive grouped
    // by section, CollectAgendaItems' own sort order) rather than a
    // separate divider excerpt -- BuildMultibuffer's own rule line between
    // excerpts already gives every entry a visible top edge.
    std::vector<editor::multibuffer::ExcerptSource> excerpts;
    excerpts.reserve(items.size());
    for (const editor::AgendaItem& item : items) {
        const std::size_t line = item.headline.lineNumber + 1; // 1-indexed, matching every other multibuffer consumer
        std::string       header =
            "▸ [" + std::string(sectionLabel(item.section)) + "] " + item.file.string() + ":" + std::to_string(line);
        std::string body = editor::FormatAgendaItemSummary(item);
        excerpts.push_back(editor::multibuffer::ExcerptSource{item.file, line, line, std::move(header), std::move(body), {}});
    }

    text::Buffer& results = editor::multibuffer::BuildMultibuffer(bufferList_, "*agenda*", excerpts);
    editor::SetLastResultsBuffer("*agenda*");
    activeBuffer_.Set(results);
    statusMessage_ = excerpts.empty() ? "No active TODOs." : std::to_string(excerpts.size()) + " agenda item" + (excerpts.size() == 1 ? "" : "s");
}

void BufferView::BuildClockReportMultibuffer() {
    text::Buffer&                                buffer     = activeBuffer_.Get();
    const std::string                            bufferText = buffer.Text();
    const std::vector<editor::org::Headline>     headlines  = editor::org::ParseOutline(bufferText, editor::org::TodoKeywords());
    const std::vector<editor::org::HeadlineNode> tree       = editor::org::BuildHeadlineTree(headlines);
    // sourcePath is empty for a not-yet-saved buffer -- jump-to-source is
    // then a silent no-op on any excerpt, the same "no source, no jump"
    // posture every other multibuffer consumer here already has.
    const std::filesystem::path sourcePath = buffer.Path().value_or(std::filesystem::path{});

    std::vector<editor::multibuffer::ExcerptSource> excerpts;
    for (const editor::org::HeadlineNode& root : tree) {
        CollectClockedHeadlines(bufferText, root, sourcePath, excerpts);
    }

    text::Buffer& results = editor::multibuffer::BuildMultibuffer(bufferList_, "*clock report*", excerpts);
    editor::SetLastResultsBuffer("*clock report*");
    activeBuffer_.Set(results);
    statusMessage_ = excerpts.empty() ? "No clocked time in this buffer."
                                      : std::to_string(excerpts.size()) + " clocked headline" + (excerpts.size() == 1 ? "" : "s");
}

void BufferView::OpenLinkAtPoint() {
    // documentLink follow-up: LSP-first, falling back to the pre-existing
    // chain -- see this method's own doc comment in BufferView.h. No manager
    // wired up at all short-circuits synchronously (SwitchHeaderSource's own
    // precedent), which is what keeps every no-LSP caller, including every
    // BufferView test, on exactly the old path.
    if (!lspManager_) {
        OpenLinkAtPointWithoutLsp();
        return;
    }

    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = documentLinkRequest_.Begin();
    // embedded-language-documents follow-up: an #include inside an embedded
    // region belongs to that region's own server, same routing every other
    // point-scoped LSP request here uses.
    const std::string serverKey = ResolvedLspServerKey(point);

    lspManager_->RequestDocumentLinks(
        buffer,
        [this, bufferPtr, point, generation, serverKey](std::vector<editor::lsp::Manager::ResolvedDocumentLink> links) {
            if (documentLinkRequest_.IsStale(generation)) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent -- RequestDefinitionAtPoint's own guard
            }
            const auto covering = std::find_if(links.begin(), links.end(),
                                               [point](const editor::lsp::Manager::ResolvedDocumentLink& link) {
                                                   return link.startByte <= point && point <= link.endByte;
                                               });
            if (covering == links.end()) {
                OpenLinkAtPointWithoutLsp(); // the server has nothing here (a URL in a comment, a bare path, an Org link)
                return;
            }
            if (!covering->needsResolve) {
                OpenResolvedDocumentLink(*covering);
                return;
            }
            // A link the server deliberately sent target-less, expecting a
            // second round trip before it can be followed.
            lspManager_->ResolveDocumentLink(
                activeBuffer_.Get(), *covering,
                [this, bufferPtr, point, generation](std::optional<editor::lsp::Manager::ResolvedDocumentLink> resolved) {
                    if (documentLinkRequest_.IsStale(generation)) {
                        return;
                    }
                    if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                        return;
                    }
                    if (!resolved || resolved->needsResolve) {
                        OpenLinkAtPointWithoutLsp(); // resolve failed or still named no target
                        return;
                    }
                    OpenResolvedDocumentLink(*resolved);
                },
                serverKey);
        },
        serverKey);
}

void BufferView::OpenResolvedDocumentLink(const editor::lsp::Manager::ResolvedDocumentLink& link) {
    if (!link.path.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(link.path, ec)) {
            OpenLinkAtPointWithoutLsp(); // see this method's own doc comment
            return;
        }
        try {
            // Deliberately no PushJumpMark here: OpenDetectedLink's own
            // file tier doesn't push one either, and which tier answered a
            // given open-link-at-point shouldn't be observable.
            text::Buffer& opened = bufferList_.OpenOrCreateFile(link.path);
            activeBuffer_.Set(opened);
            statusMessage_.clear();
        }
        catch (const std::exception& e) {
            ReportError(e.what());
        }
        return;
    }
    if (!link.url.empty()) {
        // Reuses the generic tail so a server-reported URL opens exactly the
        // way a detected one does, including its "no URL-open command
        // configured" reporting.
        OpenDetectedLink(editor::link::DetectedLink{
            .kind      = editor::link::LinkKind::Url,
            .target    = link.url,
            .startByte = link.startByte,
            .endByte   = link.endByte,
        });
        return;
    }
    OpenLinkAtPointWithoutLsp(); // a target this editor can't act on at all
}

void BufferView::OpenDetectedLink(const editor::link::DetectedLink& detected) {
    if (detected.kind == editor::link::LinkKind::Url) {
        if (editor::link::OpenUrl(detected.target)) {
            statusMessage_ = "Opening " + detected.target;
        }
        else {
            statusMessage_ = "No URL-open command configured (ned/set-url-open-command).";
        }
        return;
    }

    text::Buffer&         buffer = activeBuffer_.Get();
    std::filesystem::path baseDirectory =
        buffer.Path() ? buffer.Path()->parent_path() : editor::ProjectRoot();
    // resolver-gaps follow-up: Python's own leading-dot relative-import
    // level -- 1 dot means "this file's own directory" (baseDirectory
    // already computed above, no ascension), each additional dot ascends
    // one more parent directory (Mode.h's ImportTarget::relativeLevel doc
    // comment has the full semantics). Stops early if parent_path() stops
    // making progress (the filesystem root), the same guard
    // NodeModules.cpp's own upward walk uses.
    for (int level = 1; level < detected.relativeLevel; ++level) {
        const std::filesystem::path parent = baseDirectory.parent_path();
        if (parent == baseDirectory) {
            break;
        }
        baseDirectory = parent;
    }
    // resolver-gaps follow-up: Rust's own bodyless "mod foo;" declaration
    // (Mode.h's ImportTarget::isModDeclaration doc comment) -- a submodule
    // of any file other than a crate root/mod.rs lives one directory level
    // below the importing file, under a subdirectory named after that
    // file's own stem (e.g. "src/foo.rs"'s own "mod bar;" resolves against
    // "src/foo/bar.rs", not "src/bar.rs"). "main"/"lib"/"mod" are Rust's own
    // three file-name conventions where the importing file already sits at
    // the level its submodules resolve from, so no adjustment applies.
    if (detected.isModDeclaration && buffer.Path()) {
        const std::string stem = buffer.Path()->stem().string();
        if (stem != "main" && stem != "lib" && stem != "mod") {
            baseDirectory /= stem;
        }
    }
    const editor::ProjectSettings projectSettings = editor::LoadProjectSettings(editor::ProjectRoot());

    // toolchain-include-paths follow-up: project-configured includePaths
    // always come first (a user override outranks a guessed default), with
    // the real compiler's own system search paths appended as a last-resort
    // fallback for an angle-form/system include ProjectSettings never
    // mentioned at all.
    const std::string                        languageKey    = editor::LanguageKeyForMode(mode_);
    std::vector<std::filesystem::path>       includePaths   = editor::IncludePathsForMode(projectSettings, mode_.name);
    const std::vector<std::filesystem::path> toolchainPaths = editor::ToolchainIncludePathsForLanguage(languageKey);
    includePaths.insert(includePaths.end(), toolchainPaths.begin(), toolchainPaths.end());

    // import-target-tree-sitter follow-up: per-language extension/index-file/
    // package-dir parameters (Editor/ImportResolutionConfig.h) widen what
    // ResolveFileLink can find beyond an exact on-disk match -- a relative
    // JS/TS import written without its real extension, a Python package's
    // __init__.py, a bare "import x from 'lodash'" package specifier.
    const editor::ImportResolutionConfig importConfig =
        editor::ResolveImportResolutionConfig(projectSettings, languageKey);
    if (importConfig.searchPackageDirs) {
        const std::vector<std::filesystem::path> packageDirs =
            editor::NodeModulesSearchPaths(baseDirectory, editor::ProjectRoot());
        includePaths.insert(includePaths.end(), packageDirs.begin(), packageDirs.end());
    }

    const auto resolved = editor::link::ResolveFileLink(detected.target, baseDirectory, includePaths,
                                                        importConfig.extensions, importConfig.indexBasenames);
    if (!resolved) {
        statusMessage_ = "No such file: " + detected.target;
        return;
    }

    try {
        text::Buffer& opened = bufferList_.OpenOrCreateFile(*resolved);
        activeBuffer_.Set(opened);
        statusMessage_.clear();
    }
    catch (const std::exception& e) {
        ReportError(e.what());
    }
}

void BufferView::BuildResultsBuffer(const std::vector<editor::SearchMatch>& matches, const std::string& name) {
    std::string resultsText;
    for (const editor::SearchMatch& match : matches) {
        resultsText += match.file.string() + ":" + std::to_string(match.lineNumber) + ": " + match.lineText + "\n";
    }

    text::Buffer& results = bufferList_.CreateBuffer(name);
    results.InsertAtPoint(resultsText);
    results.SetPoint(0);
    // read-only-buffers follow-up: a synthesized, no-file-to-save-to
    // buffer -- read-only both to prevent editing it (nothing meaningful
    // would happen to the edit anyway) and, doubling as "tossable," so its
    // Modified() state (unavoidable -- InsertAtPoint above already set it)
    // never triggers the close/quit unsaved-changes prompt. See
    // RequestCloseBuffer/StartInteractiveSession's ConfirmQuit case.
    results.SetReadOnly(true);
    editor::SetLastResultsBuffer(name);
    activeBuffer_.Set(results);
}

} // namespace ned::ui
