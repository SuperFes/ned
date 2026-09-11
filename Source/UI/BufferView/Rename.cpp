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

#include "Editor/LocalScopes.h"
#include "Editor/ModeOverrides.h"
#include "Editor/NextError.h"
#include "Editor/Project/Root.h"
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

    constexpr std::string_view kRenameReviewBufferName = "*rename*";

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
    if (rows.empty()) {
        return false;
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
    text::Buffer* const stale = bufferList_.Find(std::string(kRenameReviewBufferName));

    text::Buffer& review = editor::multibuffer::BuildMultibuffer(bufferList_, std::string(kRenameReviewBufferName),
                                                                 excerpts, total);

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

    activeBuffer_.Set(review);
    if (stale != nullptr) {
        editor::multibuffer::ClearMultibufferIndexFor(*stale);
        CloseBufferNow(*stale);
        review.Rename(std::string(kRenameReviewBufferName));
    }
    editor::SetLastResultsBuffer(std::string(kRenameReviewBufferName));

    std::string message = "Rename \"" + oldName + "\" to \"" + newName + "\": " + std::to_string(applied) +
                          " reference" + (applied == 1 ? "" : "s");
    if (risky != 0) {
        message += ", " + std::to_string(risky) + " comment/string occurrence" + (risky == 1 ? "" : "s") +
                   " excluded (M-a includes one)";
    }
    message += " -- C-c C-c apply, M-a/M-r include/exclude, M-n/M-p move";
    statusMessage_ = message;
    return true;
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
