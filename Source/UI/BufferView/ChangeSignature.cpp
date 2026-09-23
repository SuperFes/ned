//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// change-signature's project-wide half: given a function's old signature
// (resolved elsewhere -- point resolution and the retype prompt are a
// follow-up to this one) and the new signature text the user retyped, runs
// the discovery pass (Editor/ChangeSignature.h::DiscoverSignatureAndCallSites),
// builds the position mapping and every call-site/signature-site rewrite,
// and opens the review through the same machinery a rename or an import
// fixup already uses -- BuildReviewExcerpts -> PresentReviewExcerpts, one
// RenameHit per edit range carrying its own replacement text, no comment/
// string classification pass (these hits are the edit, not a candidate to
// weigh, the same reason BuildImportFixupReview skips it).
//

#include "UI/BufferView/Internal.h"

#include "Editor/ChangeSignature.h"
#include "Editor/ChangeSignatureSettings.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Project/Root.h"
#include "Editor/Project/Search.h"
#include "Editor/RenameReview.h"

#include <algorithm>
#include <unordered_map>

namespace ned::ui {

using namespace detail;

namespace {

    constexpr std::string_view kChangeSignatureReviewBufferName = "*signature*";

    // Same helper Rename.cpp keeps privately for its own review buffers --
    // colocated rather than shared, the same "three similar lines" call
    // this codebase already makes for a helper this small elsewhere.
    std::string ProjectRelativeDisplayPath(const std::filesystem::path& path) {
        std::error_code             ec;
        const std::filesystem::path relative = std::filesystem::relative(path, editor::ProjectRoot(), ec);
        return (!ec && !relative.empty()) ? relative.string() : path.string();
    }

    editor::changesig::FileScanResult ScanFileForChangeSignature(const std::filesystem::path& path, std::string_view text) {
        const editor::Mode                mode = editor::ModeForPath(path);
        editor::changesig::FileScanResult result;
        if (mode.signatures) {
            result.signatures = mode.signatures(text);
        }
        if (mode.calls) {
            result.calls = mode.calls(text);
        }
        return result;
    }

} // namespace

bool BufferView::BuildChangeSignatureReview(const std::filesystem::path& targetFile, const std::string& targetText,
                                            const editor::SignatureMarker& targetSignature,
                                            const std::string&             newSignatureText) {
    const editor::Mode targetMode = editor::ModeForPath(targetFile);
    if (!targetMode.signatures) {
        statusMessage_ = "This file's language has no signature query.";
        return false;
    }

    // The synthetic-wrapper trick Editor/ChangeSignature.h's own header
    // comment describes: feeding the retyped text back through the real
    // grammar-based parameter parser rather than hand-rolling a second
    // nested-bracket-aware splitter. A real BODY (not a bare ";") is
    // required -- cpp/signatures.janet's own header comment explains why a
    // bodyless free-function prototype is deliberately never captured
    // (C++'s "most vexing parse"), which a semicolon-terminated wrapper
    // would be.
    const std::string syntheticText    = "void __ned_sig(" + newSignatureText + ") {}";
    const auto         syntheticMarkers = targetMode.signatures(syntheticText);
    if (syntheticMarkers.empty()) {
        statusMessage_ = "Change signature: couldn't parse the new signature.";
        return false;
    }
    const std::vector<editor::SignatureParameter>& newParams = syntheticMarkers.front().parameters;

    const editor::changesig::MappingResult mapping = editor::changesig::BuildPositionMapping(
        targetText, targetSignature.parameters, syntheticText, newParams);
    if (mapping.declined) {
        statusMessage_ = "Change signature: " + mapping.declineReason + ".";
        return false;
    }

    const std::string_view name = std::string_view(targetText).substr(
        targetSignature.nameStartByte, targetSignature.nameEndByte - targetSignature.nameStartByte);
    const std::string pattern = editor::changesig::CandidatePattern(name);
    if (pattern.empty()) {
        statusMessage_ = "Change signature: nothing to search for.";
        return false;
    }

    std::vector<std::filesystem::path> candidates;
    try {
        std::filesystem::path previous;
        const std::size_t     limit = editor::ChangeSignatureMaxFiles();
        for (const editor::SearchMatch& match : editor::SearchDirectory(editor::ProjectRoot(), pattern, bufferList_)) {
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
        statusMessage_ = "Change signature: couldn't search the project for \"" + std::string(name) + "\".";
        return false;
    }

    const editor::changesig::DiscoveryResult discovery = editor::changesig::DiscoverSignatureAndCallSites(
        name, targetSignature.parameters.size(), candidates,
        [this](const std::filesystem::path& path) { return ReviewSourceTextForRename(path); },
        ScanFileForChangeSignature);

    if (discovery.arityMismatches != 0) {
        statusMessage_ = "Change signature: " + std::to_string(discovery.arityMismatches) + " other definition" +
                         (discovery.arityMismatches == 1 ? "" : "s") + " named \"" + std::string(name) +
                         "\" with a different parameter count found -- can't tell which call sites are which.";
        return false;
    }

    // The target's own signature is added explicitly, from the caller's own
    // already-resolved targetSignature, rather than trusted to come back out
    // of `discovery` -- the candidate search normally re-finds it too (the
    // target's own file always mentions its own function's name), but
    // "normally" is not a guarantee the one edit the user actually asked for
    // should ever rest on. The loop below skips re-adding it if discovery
    // found the identical range, which is the common case.
    std::unordered_map<std::filesystem::path, editor::rename::FileRenameHits> byFile;
    auto addSignatureHit = [&byFile](const std::filesystem::path& file, const std::string& text,
                                     const editor::SignatureMarker& signature, const std::string& replacement) {
        auto [it, inserted] = byFile.try_emplace(file);
        if (inserted) {
            it->second.file = file;
            it->second.text = text;
        }
        it->second.hits.push_back(editor::rename::RenameHit{.startByte   = signature.parametersStartByte + 1,
                                                             .endByte     = signature.parametersEndByte - 1,
                                                             .kind        = editor::rename::HitKind::Reference,
                                                             .replacement = replacement});
    };
    addSignatureHit(targetFile, targetText, targetSignature, newSignatureText);
    for (const editor::changesig::SignatureSite& site : discovery.signatureSites) {
        if (site.file == targetFile && site.signature.parametersStartByte == targetSignature.parametersStartByte &&
            site.signature.parametersEndByte == targetSignature.parametersEndByte) {
            continue; // already added explicitly above
        }
        addSignatureHit(site.file, site.text, site.signature, newSignatureText);
    }

    std::string declineReason;
    std::size_t declinedCallSites = 0;
    for (const editor::changesig::CallSite& site : discovery.callSites) {
        const editor::changesig::ArgumentRewrite rewrite =
            editor::changesig::RewriteArgumentList(site.text, site.call.arguments, syntheticText, mapping.origins);
        if (rewrite.declined) {
            ++declinedCallSites;
            declineReason = rewrite.declineReason;
            continue;
        }
        auto [it, inserted] = byFile.try_emplace(site.file);
        if (inserted) {
            it->second.file = site.file;
            it->second.text = site.text;
        }
        it->second.hits.push_back(editor::rename::RenameHit{.startByte   = site.call.argumentsStartByte + 1,
                                                             .endByte     = site.call.argumentsEndByte - 1,
                                                             .kind        = editor::rename::HitKind::Reference,
                                                             .replacement = rewrite.argumentListText});
    }

    std::vector<editor::rename::FileRenameHits> files;
    files.reserve(byFile.size());
    std::size_t edits = 0;
    for (auto& [path, file] : byFile) {
        std::sort(file.hits.begin(), file.hits.end(),
                  [](const editor::rename::RenameHit& a, const editor::rename::RenameHit& b) {
                      return a.startByte < b.startByte;
                  });
        file.displayPath = ProjectRelativeDisplayPath(file.file);
        edits += file.hits.size();
        files.push_back(std::move(file));
    }

    const std::optional<BufferView::ReviewCounts> counts = PresentReviewExcerpts(
        editor::rename::BuildReviewExcerpts(files, std::string()), std::string(kChangeSignatureReviewBufferName));
    if (!counts) {
        statusMessage_ = "Change signature: nothing found to rewrite.";
        return false;
    }

    std::string message = "Change signature of \"" + std::string(name) + "\": " + std::to_string(edits) + " edit" +
                          (edits == 1 ? "" : "s") + " in " + std::to_string(files.size()) + " file" +
                          (files.size() == 1 ? "" : "s");
    if (declinedCallSites != 0) {
        message += ", " + std::to_string(declinedCallSites) + " call site(s) left alone (" + declineReason + ")";
    }
    message += " -- C-c C-c apply, M-r exclude, M-n/M-p move";
    statusMessage_ = message;
    return true;
}

namespace {

    // The innermost Mode::signatures marker containing `point` -- smallest
    // range wins on overlap, the same "innermost" rule ResolveLocalBindingAt
    // applies, defensive against a future grammar that nests function-like
    // definitions (a lambda) even though cpp/signatures.janet doesn't today.
    const editor::SignatureMarker* InnermostSignatureAt(const std::vector<editor::SignatureMarker>& markers,
                                                         std::size_t                                 point) {
        const editor::SignatureMarker* best = nullptr;
        for (const editor::SignatureMarker& marker : markers) {
            if (marker.startByte <= point && point <= marker.endByte &&
                (best == nullptr || (marker.endByte - marker.startByte) < (best->endByte - best->startByte))) {
                best = &marker;
            }
        }
        return best;
    }

} // namespace

void BufferView::RequestChangeSignatureAtPoint() {
    pendingChangeSignature_.reset();
    text::Buffer& buffer = activeBuffer_.Get();

    if (!buffer.Path().has_value()) {
        statusMessage_ = "Buffer must be saved before changing its signature.";
        return;
    }
    // Same huge-buffer exclusion ResolveLocalBindingAtPoint applies, for the
    // same reason: a windowed answer here would silently miss a call site or
    // another signature outside the window rather than showing a partial
    // display.
    if (buffer.Content().IsHuge()) {
        statusMessage_ = "Buffer too large for change-signature.";
        return;
    }
    if (!mode_.signatures) {
        statusMessage_ = "No signature query configured for this mode.";
        return;
    }

    const std::string text = buffer.Content().Substring(0, buffer.Content().ByteLength());
    const auto        markers = mode_.signatures(text);
    const editor::SignatureMarker* marker = InnermostSignatureAt(markers, buffer.Point());
    if (marker == nullptr) {
        statusMessage_ = "No function signature at point.";
        return;
    }
    for (const editor::SignatureParameter& parameter : marker->parameters) {
        if (parameter.isVariadic) {
            statusMessage_ = "A variadic parameter can't be reordered, dropped or defaulted.";
            return;
        }
    }

    const std::string prefill(
        std::string_view(text).substr(marker->parametersStartByte + 1, marker->parametersEndByte - marker->parametersStartByte - 2));

    pendingChangeSignature_ = PendingChangeSignature{.file = *buffer.Path(), .text = text, .signature = *marker};
    inputMode_              = InputMode::ChangeSignatureNewSignature;
    prompt_.emplace("New signature: ");
    prompt_->SetText(prefill);
    statusMessage_ = prompt_->StatusText();
}

void BufferView::ApplyChangeSignature(const std::string& newSignatureText) {
    const std::optional<PendingChangeSignature> pending = std::move(pendingChangeSignature_);
    pendingChangeSignature_.reset();
    if (!pending) {
        statusMessage_ = "No pending signature change.";
        return;
    }

    text::Buffer& buffer = activeBuffer_.Get();
    if (buffer.Path() != pending->file) {
        statusMessage_ = "Buffer changed since the signature change started -- nothing changed.";
        return;
    }
    const std::string currentText = buffer.Content().Substring(0, buffer.Content().ByteLength());
    if (currentText != pending->text) {
        // The prompt owns every keystroke while it is up, same as
        // ApplyLocalRename's own guard -- this is a re-verify, not the
        // expected path, cheap next to the cost of building a review
        // against ranges that have silently moved.
        const auto                     markers = mode_.signatures ? mode_.signatures(currentText) : std::vector<editor::SignatureMarker>{};
        const editor::SignatureMarker* fresh   = nullptr;
        for (const editor::SignatureMarker& marker : markers) {
            if (marker.parametersStartByte == pending->signature.parametersStartByte &&
                marker.parametersEndByte == pending->signature.parametersEndByte &&
                marker.nameStartByte == pending->signature.nameStartByte) {
                fresh = &marker;
                break;
            }
        }
        if (fresh == nullptr) {
            statusMessage_ = "Buffer changed since the signature change started -- nothing changed.";
            return;
        }
        BuildChangeSignatureReview(pending->file, currentText, *fresh, newSignatureText);
        return;
    }

    BuildChangeSignatureReview(pending->file, pending->text, pending->signature, newSignatureText);
}

} // namespace ned::ui
