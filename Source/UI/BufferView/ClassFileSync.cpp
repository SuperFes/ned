//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// class-file-sync: keeping a file's name and the single type declared inside
// it in agreement, in both directions. The rules -- what counts as the file's
// one top-level type, how a filename splits so a compound suffix survives a
// stem swap, and when an offer is certain enough to make unprompted -- are all
// pure and live in Editor/ClassFileSync.h. Everything here is the I/O half:
// running the mode's tags query over the buffer, opening the y/n, and handing
// the accepted rename to the machinery that already exists for it.
//
// Nothing here performs a rename itself. Direction A routes through
// PerformProjectRename, so the file rename gets workspace/willRenameFiles,
// the import fixup and the open-buffer follow exactly as a hand-typed
// rename-file does; direction B routes through the ordinary rename flow, so
// the type rename gets the server tier and the review buffer. That is the
// whole point of the feature being this small: both halves of it already
// existed, unconnected.
//
// WHERE THE AUTOMATIC OFFER CAN AND CANNOT FIRE, since it is not obvious:
// renaming a top-level type is always a cross-file edit, so rename-symbol's
// scope-aware tier declines it outright (LocalBinding::scopeIsFile) and it
// reaches the server tier or nothing. The offer therefore rides on the
// server's own rename landing, or on a *rename* review being committed --
// never on the no-server in-buffer rewrite, which by construction only ever
// renames a binding that is NOT the file's type. With no language server
// configured for a language, the explicit commands are the whole feature,
// and they need no server at all: that asymmetry is deliberate, not an
// oversight.
//

#include "UI/BufferView/Internal.h"

#include "Editor/ClassFileSync.h"
#include "Editor/ClassFileSyncSettings.h"

namespace ned::ui {

using namespace detail;

namespace {

    // What to say when there is no type to act on. Separated per reason
    // rather than collapsed into one message: "this mode has no tags query"
    // and "this file declares nothing" are different problems, and the user
    // asked a direct question in both cases.
    std::string DeclineMessage(const std::optional<editor::classfile::Resolution>& resolution) {
        if (!resolution) {
            return "No type information for this file -- its language has no tags query, or the buffer is too "
                   "large to scan whole.";
        }
        switch (resolution->decline) {
            case editor::classfile::Decline::NoTypes:
                return "No class, interface, enum, struct or record declared at the top level of this file.";
            case editor::classfile::Decline::SeveralTypes:
                return "This file declares " + std::to_string(resolution->candidateCount) +
                       " top-level types -- not renaming on a guess.";
            case editor::classfile::Decline::None:
                break;
        }
        return std::string();
    }

} // namespace

std::optional<editor::classfile::Resolution> BufferView::ResolveTopLevelTypeIn(
    const std::filesystem::path& path, editor::classfile::Strictness strictness) {
    // A directory reaches here for real: renaming one is an ordinary
    // rename-file, and the offer that follows it asks this about whatever
    // moved. Reading a directory as a stream THROWS rather than failing
    // quietly ("basic_filebuf::underflow error reading the file: Is a
    // directory"), and that exception surfaced as the rename's own status
    // message -- so the check is here, covering every caller, rather than at
    // the one call site that happened to find it.
    std::error_code regularEc;
    if (!std::filesystem::is_regular_file(path, regularEc)) {
        return std::nullopt;
    }
    // Live buffer content first, the file only if nothing has it open --
    // ReviewSourceTextForRename already encodes that rule (and the huge-file
    // refusal below it), so the answer here cannot disagree with what a
    // rename review would have shown for the same file.
    const std::optional<std::string> text = ReviewSourceTextForRename(path);
    if (!text) {
        return std::nullopt;
    }
    const editor::Mode mode = editor::ModeForPath(path);
    if (!mode.symbolKind) {
        return std::nullopt;
    }
    try {
        return editor::classfile::SelectType(mode.symbolKind(*text), strictness);
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<editor::classfile::Resolution> BufferView::ResolveTopLevelTypeInBuffer(
    editor::classfile::Strictness strictness) {
    if (!mode_.symbolKind) {
        return std::nullopt; // no tags query for this language
    }
    text::Buffer& buffer = activeBuffer_.Get();
    // Deliberately not windowed, unlike the fold/symbol/test gutters. Those
    // degrade to "what is visible", which is a partial display; this would
    // degrade to "the one type in the part I looked at", which is a wrong
    // rename. Same reasoning ResolveLocalBindingAtPoint records for the
    // scope-aware rename tier.
    if (buffer.Content().IsHuge()) {
        return std::nullopt;
    }
    const std::string text = buffer.Content().Substring(0, buffer.Content().ByteLength());
    try {
        return editor::classfile::SelectType(mode_.symbolKind(text), strictness);
    }
    catch (const std::exception&) {
        return std::nullopt; // a query that failed to run is "no type information", not an error to report
    }
}

void BufferView::RequestRenameFileToMatchType() {
    text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.Path().has_value()) {
        statusMessage_ = "This buffer has no file to rename.";
        return;
    }

    const std::optional<editor::classfile::Resolution> resolution =
        ResolveTopLevelTypeInBuffer(editor::classfile::Strictness::BestEffort);
    if (!resolution || !resolution->type) {
        statusMessage_ = DeclineMessage(resolution);
        return;
    }

    const std::filesystem::path source   = *buffer.Path();
    const std::string           current  = source.filename().string();
    const std::string           proposed = editor::classfile::FileNameForType(current, resolution->type->name);
    if (proposed.empty()) {
        statusMessage_ = "\"" + resolution->type->name + "\" cannot be used as a file name.";
        return;
    }
    if (proposed == current) {
        statusMessage_ = current + " is already named after " + resolution->type->name + ".";
        return;
    }

    const std::filesystem::path destination = source.parent_path() / proposed;
    std::error_code             ec;
    if (std::filesystem::exists(destination, ec)) {
        statusMessage_ = proposed + " already exists -- not overwriting it.";
        return;
    }

    classFileRenameSource_      = source;
    classFileRenameDestination_ = destination;
    inputMode_                  = InputMode::ConfirmRenameFileToMatchType;
    // The count is named whenever it is more than one: the command took the
    // outermost type on purpose, and the user is entitled to see which name
    // it settled on and that there were others.
    const std::string note = resolution->candidateCount > 1
                                 ? " (outermost of " + std::to_string(resolution->candidateCount) + " types here)"
                                 : std::string();
    statusMessage_         = "Rename " + current + " to " + proposed + ", after " + resolution->type->name + note + "? (y/n)";
}

bufferview::ConfirmPrompt BufferView::ConfirmRenameFileToMatchTypePrompt() {
    // Captured now: PerformProjectRename starts a session of its own, and
    // EndInteractiveSession has already cleared these by the time onConfirm
    // runs -- the ordering rule ConfirmPrompt.h states.
    return {.cancelMessage = "Rename cancelled.",
            .onConfirm     = [this, source = classFileRenameSource_, destination = classFileRenameDestination_] {
                PerformProjectRename(source, destination);
            }};
}

void BufferView::HandleConfirmRenameFileToMatchTypeKey(const editor::KeyChord& chord) {
    HandleConfirmPromptKey(ConfirmRenameFileToMatchTypePrompt(), chord);
}

void BufferView::ArmClassFileRenameOffer(std::vector<std::filesystem::path> touched, std::string oldName,
                                         std::string newName) {
    pendingClassFileOffer_.reset();
    if (!editor::ClassFileSyncEnabled() || touched.empty() || oldName.empty() || newName.empty() ||
        oldName == newName) {
        return;
    }

    // The "before" half of the evidence, and the reason arming happens
    // BEFORE the edits land rather than after: a file is only a candidate if
    // it was demonstrably named after its own single type a moment ago. That
    // is what separates a real type rename from a rename of something else
    // that merely happens to end up spelled like the file's type -- after
    // the fact the two are indistinguishable, since both leave a file named
    // oldName holding a type named newName.
    for (const std::filesystem::path& path : touched) {
        if (editor::classfile::SplitFileName(path.filename().string()).stem != oldName) {
            continue;
        }
        const std::optional<editor::classfile::Resolution> before =
            ResolveTopLevelTypeIn(path, editor::classfile::Strictness::Strict);
        if (!before || !before->type || before->type->name != oldName) {
            continue;
        }
        pendingClassFileOffer_ =
            PendingClassFileOffer{.file = path, .oldName = std::move(oldName), .newName = std::move(newName)};
        return; // only one file can be named after the symbol being renamed
    }
}

bool BufferView::MaybeOfferClassFileRename() {
    const std::optional<PendingClassFileOffer> offer = pendingClassFileOffer_;
    pendingClassFileOffer_.reset();
    if (!offer || inputMode_ != InputMode::Normal) {
        return false; // never interrupt a session that is already up
    }

    // The "after" half: the edits have landed, so the file's one type should
    // now carry the new name. Re-read rather than assumed -- an excerpt the
    // user excluded in the review is exactly the case where the rename was
    // proposed and then did not happen.
    const std::optional<editor::classfile::Resolution> after =
        ResolveTopLevelTypeIn(offer->file, editor::classfile::Strictness::Strict);
    const std::string filename = offer->file.filename().string();
    if (!after ||
        !editor::classfile::FileRenameFollowsSymbolRename(*after, filename, offer->oldName, offer->newName)) {
        return false;
    }

    const std::string proposed = editor::classfile::FileNameForType(filename, offer->newName);
    if (proposed.empty()) {
        return false;
    }
    const std::filesystem::path destination = offer->file.parent_path() / proposed;
    std::error_code             ec;
    if (std::filesystem::exists(destination, ec)) {
        return false; // silently: this is an offer, not a request, so there is nothing to report
    }

    classFileRenameSource_      = offer->file;
    classFileRenameDestination_ = destination;
    inputMode_                  = InputMode::ConfirmRenameFileToMatchType;
    statusMessage_              = "Renamed " + offer->oldName + " to " + offer->newName + ". Rename " + filename + " to " +
                                  proposed + " as well? (y/n)";
    return true;
}

bool BufferView::OfferTypeRenameAfterFileMove(const std::filesystem::path& previousPath,
                                              const std::filesystem::path& newPath) {
    if (!editor::ClassFileSyncEnabled() || inputMode_ != InputMode::Normal) {
        return false;
    }
    // The buffer has already followed the file by the time this runs, so the
    // resolution is read at the NEW path -- but the question is asked of the
    // OLD name, which is what the type should still be called if the file
    // was named after it.
    const std::optional<editor::classfile::Resolution> resolution =
        ResolveTopLevelTypeIn(newPath, editor::classfile::Strictness::Strict);
    if (!resolution || !editor::classfile::TypeRenameFollowsFileMove(*resolution, previousPath.filename().string(),
                                                                     newPath.filename().string())) {
        return false;
    }

    const std::string proposed = editor::classfile::SplitFileName(newPath.filename().string()).stem;
    classTypeRenameOldName_    = resolution->type->name;
    classTypeRenameNewName_    = proposed;
    classTypeRenameOffset_     = resolution->type->nameStartByte;
    inputMode_                 = InputMode::ConfirmRenameTypeToMatchFile;
    statusMessage_             = "Renamed " + previousPath.filename().string() + " to " + newPath.filename().string() +
                                 ". Rename " + resolution->type->name + " to " + proposed + " as well? (y/n)";
    return true;
}

void BufferView::RequestRenameTypeToMatchFile() {
    text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.Path().has_value()) {
        statusMessage_ = "This buffer has no file to take a name from.";
        return;
    }

    const std::optional<editor::classfile::Resolution> resolution =
        ResolveTopLevelTypeIn(*buffer.Path(), editor::classfile::Strictness::BestEffort);
    if (!resolution || !resolution->type) {
        statusMessage_ = DeclineMessage(resolution);
        return;
    }

    const std::string filename = buffer.Path()->filename().string();
    const std::string proposed = editor::classfile::SplitFileName(filename).stem;
    if (!editor::classfile::LooksLikeTypeName(proposed)) {
        // "helpers-v2.php", "02_migration.php": ordinary filenames that are
        // simply not type names. Nothing to guess at.
        statusMessage_ = "\"" + proposed + "\" would not be a valid type name.";
        return;
    }
    if (proposed == resolution->type->name) {
        statusMessage_ = resolution->type->name + " is already named after " + filename + ".";
        return;
    }

    classTypeRenameOldName_ = resolution->type->name;
    classTypeRenameNewName_ = proposed;
    classTypeRenameOffset_  = resolution->type->nameStartByte;
    inputMode_              = InputMode::ConfirmRenameTypeToMatchFile;
    const std::string note  = resolution->candidateCount > 1
                                  ? " (outermost of " + std::to_string(resolution->candidateCount) + " types here)"
                                  : std::string();
    statusMessage_          = "Rename " + resolution->type->name + " to " + proposed + ", after " + filename + note + "? (y/n)";
}

bufferview::ConfirmPrompt BufferView::ConfirmRenameTypeToMatchFilePrompt() {
    return {.cancelMessage = "Rename cancelled.",
            .onConfirm     = [this, oldName = classTypeRenameOldName_, newName = classTypeRenameNewName_,
                              offset = classTypeRenameOffset_] { ApplyTypeRenameToMatchFile(oldName, newName, offset); }};
}

void BufferView::HandleConfirmRenameTypeToMatchFileKey(const editor::KeyChord& chord) {
    HandleConfirmPromptKey(ConfirmRenameTypeToMatchFilePrompt(), chord);
}

void BufferView::ApplyTypeRenameToMatchFile(const std::string& oldName, const std::string& newName,
                                            std::size_t nameOffset) {
    text::Buffer& buffer = activeBuffer_.Get();
    if (buffer.ReadOnly()) {
        statusMessage_ = "Buffer is read-only.";
        return;
    }
    // Re-resolved rather than trusted: the offsets were taken when the
    // prompt opened, and being wrong about them rewrites the wrong text --
    // the same guard ApplyLocalRename states for the same reason.
    const std::optional<editor::classfile::Resolution> fresh =
        buffer.Path().has_value()
            ? ResolveTopLevelTypeIn(*buffer.Path(), editor::classfile::Strictness::BestEffort)
            : std::nullopt;
    if (!fresh || !fresh->type || fresh->type->name != oldName || fresh->type->nameStartByte != nameOffset) {
        statusMessage_ = "Buffer changed since the rename started -- nothing renamed.";
        return;
    }

    // Tier 1: a server is actually running for this buffer. Renaming a type
    // is a cross-file edit and a server is the only thing that knows the
    // whole dependency tree, so it wins outright whenever it is there. Point
    // goes onto the identifier first -- RequestRenameAtPoint reads the word
    // at point to learn what is being renamed.
    buffer.SetPoint(nameOffset);
    viewport_.ScrollToShowPoint();
    if (lspManager_ != nullptr) {
        const std::string              serverKey = ResolvedLspServerKey(nameOffset);
        const std::vector<std::string> active    = lspManager_->ActiveServerKeysForBuffer(buffer);
        if (std::find(active.begin(), active.end(), serverKey) != active.end()) {
            RequestRenameAtPoint(newName);
            return;
        }
    }

    // Tier 2: no server. Rename the declaration and every whole-word
    // occurrence IN THIS FILE, and say so -- this is the "top-scoped name
    // pair" guess, which is reliable precisely because the pairing is local:
    // this file's one top-level type is named after this file. What it
    // cannot do is follow the dependency tree, so references in other files
    // are left alone rather than guessed at, and the status line names that
    // limit rather than leaving it to be discovered later.
    const std::string                  text = buffer.Content().Substring(0, buffer.Content().ByteLength());
    std::vector<editor::HighlightSpan> spans;
    if (mode_.highlight) {
        try {
            spans = mode_.highlight(text, editor::HighlightWindow{});
        }
        catch (const std::exception&) {
            spans.clear(); // no highlighter output just means every hit reads as code
        }
    }

    editor::rename::FileRenameHits file;
    file.file = *buffer.Path();
    file.hits = editor::rename::FindWholeWordOccurrences(text, spans, oldName);
    if (file.hits.empty()) {
        statusMessage_ = "No occurrence of " + oldName + " to rename.";
        return;
    }

    const std::string limitNote =
        "Other files are not updated -- no language server is running for this buffer.";

    // The review path first, for the same reason a symbol rename prefers it:
    // a rename nobody can see before it lands is the thing the review exists
    // to stop. BuildRenameReview classifies each hit itself, so comment and
    // string occurrences arrive excluded rather than applied.
    if (editor::RenameThroughReview() && BuildRenameReview({file}, oldName, newName)) {
        // BuildRenameReview's own line ends on its key hints with no full
        // stop, so the note needs one in front of it or the two run together.
        statusMessage_ += ". " + limitNote;
        return;
    }

    const std::size_t replaced = file.hits.size();
    buffer.BeginUndoGroup();
    for (auto hit = file.hits.rbegin(); hit != file.hits.rend(); ++hit) {
        if (hit->kind != editor::rename::HitKind::Reference) {
            continue; // a comment or string occurrence is never rewritten unasked
        }
        buffer.DeleteRange(hit->startByte, hit->endByte - hit->startByte);
        buffer.SetPoint(hit->startByte);
        buffer.InsertAtPoint(newName);
    }
    buffer.EndUndoGroup();
    statusMessage_ = "Renamed " + oldName + " to " + newName + " (" + std::to_string(replaced) + " occurrence" +
                     (replaced == 1 ? "" : "s") + " in this file). " + limitNote;
}

} // namespace ned::ui
