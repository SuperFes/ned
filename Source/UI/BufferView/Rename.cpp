//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// rename-symbol's scope-aware tier: resolving the name at point against the
// mode's own locals.scm (Editor/LocalScopes.h) and rewriting every
// occurrence of that one binding in this buffer, with no language server
// involved. The other tier -- textDocument/prepareRename plus
// textDocument/rename, which this falls through to -- lives in Lsp.cpp
// beside the rest of the LSP broker.
//
// rename-review follow-up: both tiers hand their edits to BuildRenameReview
// below when RenameThroughReview() is on, which is what makes a rename
// reviewable instead of blind. The classification and layout are pure and
// live in Editor/RenameReview.h; everything here is the I/O half -- reading
// each touched file's live text, running its own mode's highlighter over it,
// stitching the multibuffer and applying the proposed bodies into it.
//

#include "UI/BufferView/Internal.h"

#include "Editor/ImportFixup.h"
#include "Editor/ImportFixupSettings.h"
#include "Editor/LocalScopes.h"
#include "Editor/ModeOverrides.h"
#include "Editor/NextError.h"
#include "Editor/Project/Root.h"
#include "Editor/Project/Search.h"
#include "Editor/RenameReview.h"
#include "Editor/RenameReviewSettings.h"

namespace ned::ui {

using namespace detail;

void BufferView::RequestRenameSymbolAtPoint() {
    pendingLocalRename_.reset();

    if (const std::optional<editor::locals::LocalBinding> binding = ResolveLocalBindingAtPoint()) {
        // A file-level binding can be referenced from other files, so
        // rewriting it here alone would be a silent partial rename -- that
        // is a language server's question, not this buffer's. Same for a
        // binding whose own scope contains a use the resolver could not
        // attribute (see LocalBinding::usedBeforeDefinition): renaming would
        // leave that use behind.
        if (!binding->scopeIsFile && !binding->usedBeforeDefinition) {
            pendingLocalRename_ = binding;
            inputMode_          = InputMode::RenameLocalNewName;
            prompt_.emplace("New name: ");
            prompt_->SetText(binding->name);
            statusMessage_ = prompt_->StatusText();
            return;
        }
    }
    RequestPrepareRenameAtPoint();
}

std::optional<editor::locals::LocalBinding> BufferView::ResolveLocalBindingAtPoint() {
    if (!mode_.localScopes) {
        return std::nullopt; // no locals query for this mode
    }
    text::Buffer& buffer = activeBuffer_.Get();
    // A huge buffer is never handed to a whole-document tree-sitter query --
    // the same rule the fold/symbol/test gutters follow, except that those
    // can window and this cannot: a binding's occurrences are only complete
    // if the whole file was seen, so a windowed answer would be a partial
    // rename rather than a partial display.
    if (buffer.Content().IsHuge()) {
        return std::nullopt;
    }
    const std::string text = buffer.Content().Substring(0, buffer.Content().ByteLength());
    return editor::locals::ResolveBindingAt(mode_.localScopes(text), text, buffer.Point());
}

namespace {

    constexpr std::string_view kRenameReviewBufferName      = "*rename*";
    constexpr std::string_view kImportFixupReviewBufferName = "*imports*";

    // A composite excerpt's body range covers the newline BuildMultibuffer
    // appended after its last line; RenameReview's proposals are line text
    // without one. Both directions go through these two so a rewrite never
    // silently welds an excerpt onto the line after it.
    std::string_view WithoutTrailingNewline(std::string_view text) {
        return (!text.empty() && text.back() == '\n') ? text.substr(0, text.size() - 1) : text;
    }

    void ReplaceExcerptBody(text::Buffer& composite, std::size_t start, std::size_t end, std::string_view proposed) {
        const std::string current = composite.Content().Substring(start, end - start);
        std::string       text(proposed);
        if (!current.empty() && current.back() == '\n') {
            text += '\n';
        }
        composite.DeleteRange(start, end - start);
        composite.InsertAt(start, text);
    }

    std::string ProjectRelativeDisplayPath(const std::filesystem::path& path) {
        std::error_code             ec;
        const std::filesystem::path relative = std::filesystem::relative(path, editor::ProjectRoot(), ec);
        return (!ec && !relative.empty()) ? relative.string() : path.string();
    }

} // namespace

std::optional<std::string> BufferView::ReviewSourceTextForRename(const std::filesystem::path& path) const {
    if (const text::Buffer* open = bufferList_.FindByPath(path)) {
        if (open->Content().IsHuge()) {
            return std::nullopt;
        }
        return open->Content().Substring(0, open->Content().ByteLength());
    }
    if (LooksHugeSource(bufferList_, path)) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool BufferView::BuildRenameReview(std::vector<editor::rename::FileRenameHits> files, const std::string& oldName,
                                   const std::string& newName) {
    if (files.empty()) {
        return false;
    }

    // Each file's own mode decides what is a comment and what is a string --
    // a rename can cross language boundaries (a C++ symbol named in a .janet
    // plugin's string), so the highlighter is resolved per path rather than
    // taken from the pane's current mode.
    for (editor::rename::FileRenameHits& file : files) {
        const std::optional<std::string> text = ReviewSourceTextForRename(file.file);
        if (!text) {
            return false; // a huge or unreadable source: apply directly, don't half-review
        }
        file.text        = *text;
        file.displayPath = ProjectRelativeDisplayPath(file.file);

        const editor::Mode                 mode = editor::ModeForPath(file.file);
        std::vector<editor::HighlightSpan> spans;
        if (mode.highlight) {
            try {
                spans = mode.highlight(file.text, editor::HighlightWindow{});
            }
            catch (const std::exception&) {
                spans.clear(); // no highlighter output just means every hit reads as code
            }
        }

        // A server's own edits are classified too, not assumed to be
        // references: a server that does rename inside comments would
        // otherwise have that half applied silently, which is the exact
        // thing this review exists to make visible.
        std::sort(file.hits.begin(), file.hits.end(),
                  [](const editor::rename::RenameHit& a, const editor::rename::RenameHit& b) {
                      return a.startByte < b.startByte;
                  });
        for (editor::rename::RenameHit& hit : file.hits) {
            hit.kind = editor::rename::ClassifyHit(spans, hit.startByte);
        }

        std::vector<editor::rename::RenameHit> extra =
            editor::rename::FindExtraCandidates(file.text, spans, oldName, file.hits);
        if (!extra.empty()) {
            file.hits.insert(file.hits.end(), extra.begin(), extra.end());
            std::sort(file.hits.begin(), file.hits.end(),
                      [](const editor::rename::RenameHit& a, const editor::rename::RenameHit& b) {
                          return a.startByte < b.startByte;
                      });
        }
    }

    std::vector<editor::rename::ReviewExcerpt> rows = editor::rename::BuildReviewExcerpts(files, newName);
    const std::optional<BufferView::ReviewCounts> counts =
        PresentReviewExcerpts(std::move(rows), std::string(kRenameReviewBufferName));
    if (!counts) {
        return false;
    }

    // class-file-sync follow-up: the names this review is about, recorded
    // for its commit rather than read back out of renameOldName_/
    // renameNewName_ -- those belong to the SERVER tier's own request and
    // are never set when the scope-aware tier builds a review. Cleared by
    // PresentReviewExcerpts, so an *imports* review (which shares that
    // function and therefore renameProposalOwner_) can never be mistaken for
    // a rename that named something.
    renameReviewOldName_ = oldName;
    renameReviewNewName_ = newName;

    std::string message = "Rename \"" + oldName + "\" to \"" + newName + "\": " + std::to_string(counts->applied) +
                          " reference" + (counts->applied == 1 ? "" : "s");
    if (counts->risky != 0) {
        message += ", " + std::to_string(counts->risky) + " comment/string occurrence" +
                   (counts->risky == 1 ? "" : "s") + " excluded (M-a includes one)";
    }
    message += " -- C-c C-c apply, M-a/M-r include/exclude, M-n/M-p move";
    statusMessage_ = message;
    return true;
}

// The half every review shares: cap, stitch, write the proposals in, make
// the buffer active. Split out of BuildRenameReview by the
// file-rename-propagation follow-up, whose import fixups are the second
// caller -- same rows, same commit path, a different sentence about them.
// The walk plus the plan, in one place because both callers -- an in-editor
// rename and an externally detected move -- want exactly the same thing.
// Reads live buffer content in place of any file that is open, the rule
// every project-wide operation here follows.
editor::importfix::FixupPlan BufferView::PlanImportFixups(const std::vector<editor::importfix::MovedFile>& rawMoves) {
    if (rawMoves.empty() || !editor::ImportFixupEnabled()) {
        return {};
    }
    // Absolute first, always: a rename's paths come from whatever the user
    // typed at the prompt, which is routinely relative to the working
    // directory, and every comparison below -- against the project root,
    // against a resolved import -- is meaningless against one of those.
    std::vector<editor::importfix::MovedFile> moves;
    moves.reserve(rawMoves.size());
    for (const editor::importfix::MovedFile& move : rawMoves) {
        std::error_code             ec;
        const std::filesystem::path from = std::filesystem::weakly_canonical(move.from, ec);
        std::error_code             toEc;
        const std::filesystem::path to = std::filesystem::weakly_canonical(move.to, toEc);
        moves.push_back({ec ? move.from : from, toEc ? move.to : to});
    }

    // A move with no end inside the project is not this project's business:
    // nothing here can be importing it, and scanning to prove that is the
    // one cost worth refusing to pay. (The moved file's own imports are
    // skipped along with it -- a file outside the root was never part of
    // what this feature covers.)
    const std::filesystem::path root      = editor::ProjectRoot();
    const auto                  underRoot = [&root](const std::filesystem::path& path) {
        const std::filesystem::path relative = path.lexically_normal().lexically_relative(root.lexically_normal());
        return !relative.empty() && *relative.begin() != "..";
    };
    if (std::none_of(moves.begin(), moves.end(), [&](const editor::importfix::MovedFile& move) {
            return underRoot(move.from) || underRoot(move.to);
        })) {
        return {};
    }

    // The "which files might import this?" question, asked of the machinery
    // built for exactly that: threaded, .gitignore-aware, and reading an
    // open buffer's live content in place of its file.
    const std::string pattern = editor::importfix::CandidatePattern(moves);
    if (pattern.empty()) {
        return {};
    }
    std::vector<std::filesystem::path> candidates;
    try {
        std::filesystem::path previous;
        const std::size_t     limit = editor::ImportFixupMaxFiles();
        for (const editor::SearchMatch& match : editor::SearchDirectory(root, pattern, bufferList_)) {
            if (match.file == previous) {
                continue; // matches arrive grouped by file, several lines per file
            }
            previous = match.file;
            candidates.push_back(match.file);
            if (limit != 0 && candidates.size() >= limit) {
                break;
            }
        }
    }
    catch (const editor::SearchPatternError&) {
        return {}; // a filename this pattern can't express: decline, don't guess
    }

    return editor::importfix::PlanImportFixups(moves, candidates, [this](const std::filesystem::path& path) {
        return ReviewSourceTextForRename(path);
    });
}

// file-watcher follow-up to the above: a move ned did not make (a git mv, a
// file manager, a build script). The planner needs no special mode for it --
// resolution simply fails for every import that named the file, and its
// inverse question answers instead (Editor/ImportFixup.h's MatchMovedTarget)
// -- so this is the same plan-and-review, with a different sentence.
void BufferView::ReviewExternalMoves(const std::vector<editor::importfix::MovedFile>& moves) {
    const editor::importfix::FixupPlan plan = PlanImportFixups(moves);
    if (plan.files.empty()) {
        return; // nothing imported it: the move needs no comment
    }
    const std::string what = moves.size() == 1
                                 ? "Detected move of " + moves.front().from.filename().string()
                                 : "Detected " + std::to_string(moves.size()) + " moved files";
    BuildImportFixupReview(plan, what);
}
// file-rename-propagation follow-up: the same review, for edits that are not
// all the same text. Every specifier rewrite is a Reference hit carrying its
// own replacement (Editor/RenameReview.h's RenameHit::replacement), so the
// classification pass a symbol rename runs -- which would read an import
// path as a string and exclude it -- is deliberately skipped: these hits are
// not candidates to weigh up, they are the edit.
bool BufferView::BuildImportFixupReview(const editor::importfix::FixupPlan& plan, const std::string& what) {
    if (plan.files.empty()) {
        return false;
    }

    std::vector<editor::rename::FileRenameHits> files;
    std::size_t                                 edits = 0;
    for (const editor::importfix::FileFixup& fixup : plan.files) {
        editor::rename::FileRenameHits file;
        file.file        = fixup.file;
        file.displayPath = ProjectRelativeDisplayPath(fixup.file);
        file.text        = fixup.text;
        for (const editor::importfix::SpecEdit& edit : fixup.edits) {
            file.hits.push_back(editor::rename::RenameHit{.startByte   = edit.startByte,
                                                          .endByte     = edit.endByte,
                                                          .kind        = editor::rename::HitKind::Reference,
                                                          .replacement = edit.newText});
            ++edits;
        }
        files.push_back(std::move(file));
    }

    const std::optional<BufferView::ReviewCounts> counts = PresentReviewExcerpts(
        editor::rename::BuildReviewExcerpts(files, std::string()), std::string(kImportFixupReviewBufferName));
    if (!counts) {
        return false;
    }

    std::string message = what + ": " + std::to_string(edits) + " import" + (edits == 1 ? "" : "s") + " in " +
                          std::to_string(plan.files.size()) + " file" + (plan.files.size() == 1 ? "" : "s");
    if (plan.declined != 0) {
        message += ", " + std::to_string(plan.declined) + " left alone (no way to write the new location in its own style)";
    }
    message += " -- C-c C-c apply, M-r exclude, M-n/M-p move";
    statusMessage_ = message;
    return true;
}

std::optional<BufferView::ReviewCounts> BufferView::PresentReviewExcerpts(
    std::vector<editor::rename::ReviewExcerpt> rows, const std::string& bufferName) {
    if (rows.empty()) {
        return std::nullopt;
    }

    // Same cap, and the same honest note about what it dropped, as every
    // other multibuffer consumer -- see Editor/MultibufferLimits.h. Capped
    // here rather than left to BuildMultibuffer so renameProposals_ below
    // stays index-aligned with the ranges that actually got built.
    const std::size_t total       = rows.size();
    const std::size_t maxExcerpts = editor::MultibufferMaxExcerpts();
    if (maxExcerpts != 0 && rows.size() > maxExcerpts) {
        rows.resize(maxExcerpts);
    }

    std::vector<editor::multibuffer::ExcerptSource> excerpts;
    excerpts.reserve(rows.size());
    for (const editor::rename::ReviewExcerpt& row : rows) {
        excerpts.push_back(row.source);
    }

    // Same stale-singleton dance BuildProjectReplaceReview documents: the
    // new buffer is built and made active before the old one is closed, so
    // activeBuffer_ never points at a buffer being erased.
    text::Buffer* const stale = bufferList_.Find(bufferName);

    text::Buffer& review = editor::multibuffer::BuildMultibuffer(bufferList_, bufferName, excerpts, total);

    // The proposals applied into the review buffer rather than into any
    // file, which is what makes a row read as "changed" against the
    // originalText snapshot BuildMultibuffer just captured -- and therefore
    // what a commit writes back. A risky-only row is deliberately left at
    // its original text: unchanged excerpts commit nothing, so "excluded by
    // default" needs no separate mechanism. Reverse order so an earlier
    // row's offsets can't be disturbed by a later row's rewrite.
    std::size_t applied = 0;
    std::size_t risky   = 0;
    review.BeginUndoGroup();
    for (std::size_t i = review.ExcerptRanges().size(); i-- > 0;) {
        if (i >= rows.size()) {
            continue;
        }
        risky += rows[i].hasRisky ? 1 : 0;
        if (!rows[i].hasReference) {
            continue;
        }
        const std::size_t start = review.ExcerptRanges()[i].start;
        const std::size_t end   = review.ExcerptRanges()[i].end;
        if (rows[i].referencesOnlyBody == WithoutTrailingNewline(review.Content().Substring(start, end - start))) {
            continue;
        }
        ReplaceExcerptBody(review, start, end, rows[i].referencesOnlyBody);
        ++applied;
    }
    review.EndUndoGroup();
    review.SetPoint(0);

    renameProposals_     = std::move(rows);
    renameProposalOwner_ = &review;
    renameReviewOldName_.clear();
    renameReviewNewName_.clear();

    activeBuffer_.Set(review);
    if (stale != nullptr) {
        editor::multibuffer::ClearMultibufferIndexFor(*stale);
        CloseBufferNow(*stale);
        review.Rename(bufferName);
    }
    editor::SetLastResultsBuffer(bufferName);

    return ReviewCounts{.applied = applied, .risky = risky};
}

bool BufferView::HandleRenameReviewIncludeKey() {
    text::Buffer& review = activeBuffer_.Get();
    if (renameProposalOwner_ != &review || renameProposals_.empty()) {
        return false;
    }
    const std::vector<text::Buffer::ExcerptRange>& ranges = review.ExcerptRanges();
    const std::size_t                              point  = review.Point();
    for (std::size_t i = 0; i < ranges.size() && i < renameProposals_.size(); ++i) {
        if (point < ranges[i].start || point > ranges[i].end) {
            continue;
        }
        const editor::rename::ReviewExcerpt& row   = renameProposals_[i];
        const std::size_t                    start = ranges[i].start;
        const std::size_t                    end   = ranges[i].end;
        const std::string                    current(WithoutTrailingNewline(review.Content().Substring(start, end - start)));

        // Derived from the row's current text rather than from stored state,
        // so this stays right across an undo, an M-r revert, and a hand
        // edit -- none of which this command hears about.
        const std::string* next = nullptr;
        if (current == row.allHitsBody) {
            statusMessage_ = "Every occurrence in this excerpt is already included.";
            return true;
        }
        if (current == row.referencesOnlyBody) {
            next = &row.allHitsBody;
        }
        else if (current == row.source.bodyText) {
            next = (row.referencesOnlyBody != row.source.bodyText) ? &row.referencesOnlyBody : &row.allHitsBody;
        }
        else {
            statusMessage_ = "Excerpt was edited by hand -- M-r restores it first.";
            return true;
        }

        // start/end are copied out first: editing relocates the very vector
        // ranges refers into.
        review.BeginUndoGroup();
        ReplaceExcerptBody(review, start, end, *next);
        review.EndUndoGroup();
        statusMessage_ = (next == &row.allHitsBody && row.hasRisky)
                             ? "Included this excerpt's comment/string occurrences."
                             : "Included this excerpt.";
        return true;
    }
    return false;
}

void BufferView::ClearRenameProposals(const text::Buffer& buffer) {
    if (renameProposalOwner_ == &buffer) {
        renameProposals_.clear();
        renameProposalOwner_ = nullptr;
        renameReviewOldName_.clear();
        renameReviewNewName_.clear();
    }
}

void BufferView::ApplyLocalRename(const std::string& newName) {
    const std::optional<editor::locals::LocalBinding> binding = std::move(pendingLocalRename_);
    pendingLocalRename_.reset();
    if (!binding) {
        statusMessage_ = "No local binding to rename.";
        return;
    }
    if (newName.empty() || newName == binding->name) {
        statusMessage_.clear();
        return;
    }

    text::Buffer& buffer = activeBuffer_.Get();
    if (buffer.ReadOnly()) {
        statusMessage_ = "Buffer is read-only.";
        return;
    }
    // The binding was resolved against the buffer as it stood when the
    // prompt opened. Nothing should have edited it since -- the prompt owns
    // every keystroke while it is up -- but a re-resolve is cheap next to
    // rewriting ranges that have moved, and this is the one place where
    // being wrong corrupts the user's file rather than showing them
    // something stale.
    const std::optional<editor::locals::LocalBinding> fresh = ResolveLocalBindingAtPoint();
    if (!fresh || fresh->name != binding->name || fresh->definition != binding->definition ||
        fresh->occurrences != binding->occurrences) {
        statusMessage_ = "Buffer changed since the rename started -- nothing renamed.";
        return;
    }

    // rename-review follow-up: the review needs a real path to commit back
    // into, so a buffer that has never been saved keeps the direct rewrite
    // below -- as does a source BuildRenameReview declines (a huge file).
    if (editor::RenameThroughReview() && buffer.Path().has_value()) {
        std::vector<editor::rename::RenameHit> hits;
        hits.reserve(binding->occurrences.size());
        for (const std::pair<std::size_t, std::size_t>& occurrence : binding->occurrences) {
            hits.push_back(editor::rename::RenameHit{occurrence.first, occurrence.second,
                                                     editor::rename::HitKind::Reference});
        }
        editor::rename::FileRenameHits file;
        file.file = *buffer.Path();
        file.hits = std::move(hits);
        if (BuildRenameReview({std::move(file)}, binding->name, newName)) {
            return;
        }
    }

    const std::size_t point     = buffer.Point();
    std::size_t       newPoint  = point;
    const std::size_t oldLength = binding->name.size();
    const std::size_t replaced  = binding->occurrences.size();

    // Back to front, so an earlier range's offsets are still valid when it
    // is reached; point is adjusted per range for the same reason.
    buffer.BeginUndoGroup();
    for (auto it = binding->occurrences.rbegin(); it != binding->occurrences.rend(); ++it) {
        buffer.DeleteRange(it->first, it->second - it->first); // (offset, LENGTH)
        buffer.SetPoint(it->first);
        buffer.InsertAtPoint(newName);
        if (point >= it->second) {
            newPoint = newPoint - oldLength + newName.size();
        }
        else if (point > it->first) {
            newPoint = it->first + newName.size(); // point was inside the old name
        }
    }
    buffer.SetPoint(std::min(newPoint, buffer.Content().ByteLength()));
    buffer.EndUndoGroup();

    const std::string what = binding->qualifier.empty() ? std::string("binding") : binding->qualifier;
    statusMessage_         = "Renamed local " + what + " \"" + binding->name + "\" to \"" + newName + "\" (" +
                             std::to_string(replaced) + " occurrence" + (replaced == 1 ? "" : "s") + ").";
    viewport_.ScrollToShowPoint();
}

} // namespace ned::ui
