#include "CompletionSession.h"

#include <algorithm>

#include "FuzzyMatch.h"
#include "Lsp/Position.h"
#include "Text/ITextStorage.h"

namespace ned::editor {

namespace {

    // A candidate's replace region starts wherever the server's own textEdit
    // said, falling back to the caller's word-boundary rule when it sent
    // none. Clamped to point: a server reporting a range that starts *after*
    // the cursor (malformed, or a pure-suffix edit this editor has no way to
    // express) degrades to a plain insert at point rather than a reversed
    // range that would delete backwards.
    std::size_t ResolveReplaceStart(const lsp::CompletionItem& item, const text::ITextStorage& content, std::size_t point,
                                    std::size_t fallbackPrefixStart) {
        if (!item.textEdit) {
            return std::min(fallbackPrefixStart, point);
        }
        return std::min(lsp::PositionToByte(content, item.textEdit->start), point);
    }

} // namespace

CompletionSession::CompletionSession(std::vector<lsp::CompletionItem> items, bool isIncomplete, const text::ITextStorage& content,
                                     std::size_t point, std::size_t fallbackPrefixStart) : isIncomplete_(isIncomplete), prefixStart_(std::min(fallbackPrefixStart, point)) {
    allCandidates_.reserve(items.size());
    for (lsp::CompletionItem& item : items) {
        const std::size_t replaceStart = ResolveReplaceStart(item, content, point, prefixStart_);
        allCandidates_.push_back(
            CompletionCandidate{.item = std::move(item), .replaceStart = replaceStart, .sourceIndex = allCandidates_.size()});
    }
    Rank(content.Substring(prefixStart_, point - prefixStart_));
}

void CompletionSession::Rank(std::string_view prefix) {
    // Scores are computed once per candidate here rather than inside the
    // comparator: a comparator that scores its operands runs FuzzyScore
    // O(n log n) times instead of O(n), and a real server's list can be
    // thousands of items re-ranked on every keystroke.
    std::vector<std::pair<int, const CompletionCandidate*>> scored;
    scored.reserve(allCandidates_.size());
    for (const CompletionCandidate& candidate : allCandidates_) {
        // An empty prefix keeps everything: there's nothing to match
        // against, and FuzzyScore would score every candidate 0 anyway.
        if (prefix.empty()) {
            scored.emplace_back(0, &candidate);
            continue;
        }
        if (const std::optional<int> score = FuzzyScore(candidate.item.filterText, prefix)) {
            scored.emplace_back(*score, &candidate);
        }
    }

    // stable_sort so candidates the comparator considers fully equal stay in
    // the server's own order -- the last tiebreak available, and free here.
    // Note the sort runs for an empty prefix too, so even an unnarrowed list
    // comes out in the server's own sortText order rather than arrival
    // order: the ordering the LSP spec actually asks a client for, which
    // this codebase previously ignored outright.
    std::stable_sort(scored.begin(), scored.end(),
                     [](const std::pair<int, const CompletionCandidate*>& lhs,
                        const std::pair<int, const CompletionCandidate*>& rhs) {
                         if (lhs.first != rhs.first) {
                             return lhs.first > rhs.first;
                         }
                         // sortText is the server's own intended ordering and
                         // beats the label for equally good matches -- it's
                         // how a server surfaces "this overload first" or
                         // "deprecated last". Content already defaulted it
                         // to the label, so this never compares empties.
                         if (lhs.second->item.sortText != rhs.second->item.sortText) {
                             return lhs.second->item.sortText < rhs.second->item.sortText;
                         }
                         return lhs.second->item.label < rhs.second->item.label;
                     });

    candidates_.clear();
    candidates_.reserve(scored.size());
    for (const auto& [score, candidate] : scored) {
        candidates_.push_back(*candidate);
    }
    selectedIndex_ = 0;
    // completion-trigger-characters follow-up: preselect only applies while
    // nothing has been typed -- see this method's own doc comment in the
    // header. The spec says at most one item per list carries it; the first
    // one wins if a server sends several anyway.
    if (prefix.empty()) {
        for (std::size_t i = 0; i < candidates_.size(); ++i) {
            if (candidates_[i].item.preselect) {
                selectedIndex_ = i;
                break;
            }
        }
    }
}

void CompletionSession::ApplyResolution(std::size_t index, const lsp::CompletionItem& resolved) {
    if (index >= candidates_.size()) {
        return;
    }
    const std::size_t sourceIndex = candidates_[index].sourceIndex;

    // One merge rule, applied to the ranked copy and its master-list twin
    // both -- see this method's own doc comment in the header for why only
    // these three fields, and why the master matters (Refilter rebuilds
    // candidates_ from it on the very next keystroke).
    const auto merge = [&resolved](CompletionCandidate& candidate) {
        if (!resolved.documentation.empty()) {
            candidate.item.documentation = resolved.documentation;
        }
        if (!resolved.detail.empty()) {
            candidate.item.detail = resolved.detail;
        }
        if (!resolved.additionalTextEdits.empty()) {
            candidate.item.additionalTextEdits = resolved.additionalTextEdits;
        }
        candidate.resolved = true;
    };
    merge(candidates_[index]);
    if (sourceIndex < allCandidates_.size()) {
        merge(allCandidates_[sourceIndex]);
    }
}

bool CompletionSession::IsCommitCharacter(std::string_view ch) const {
    if (selectedIndex_ >= candidates_.size() || ch.empty()) {
        return false;
    }
    const std::vector<std::string>& commitCharacters = candidates_[selectedIndex_].item.commitCharacters;
    return std::find(commitCharacters.begin(), commitCharacters.end(), ch) != commitCharacters.end();
}

void CompletionSession::Select(std::size_t index) {
    if (index >= candidates_.size()) {
        return;
    }
    selectedIndex_ = index;
}

void CompletionSession::Cycle(int direction) {
    if (candidates_.empty()) {
        return;
    }
    const std::size_t count = candidates_.size();
    selectedIndex_          = (direction > 0) ? (selectedIndex_ + 1) % count : (selectedIndex_ + count - 1) % count;
}

CompletionSession::Outcome CompletionSession::Refilter(const text::ITextStorage& content, std::size_t point,
                                                       std::size_t currentPrefixStart) {
    // Point left the word this session was requested for: a non-word
    // character was typed, the word's own start was deleted through, or
    // point moved elsewhere entirely. Nothing here can be salvaged -- and in
    // the trigger-character case ("." , "::", "->") the caller's own
    // auto-trigger gate is what issues the genuinely new request, so this
    // session doesn't need to know that set at all.
    if (currentPrefixStart != prefixStart_ || point < prefixStart_) {
        return Outcome::Dismiss;
    }

    Rank(content.Substring(prefixStart_, point - prefixStart_));

    if (candidates_.empty()) {
        // A complete list that no longer matches is genuinely exhausted --
        // the server already told us these were all of them. An incomplete
        // one is the opposite: the matches may well exist and simply weren't
        // sent, so ask rather than give up.
        return isIncomplete_ ? Outcome::Rerequest : Outcome::Dismiss;
    }
    return isIncomplete_ ? Outcome::Rerequest : Outcome::Keep;
}

std::optional<CompletionSession::AcceptPlan> CompletionSession::PlanAccept(std::size_t point) const {
    if (selectedIndex_ >= candidates_.size()) {
        return std::nullopt;
    }
    const CompletionCandidate& candidate = candidates_[selectedIndex_];
    return AcceptPlan{
        .replaceStart    = std::min(candidate.replaceStart, point),
        .replaceEnd      = point,
        .newText         = candidate.item.insertText,
        .isSnippet       = candidate.item.isSnippet,
        .additionalEdits = candidate.item.additionalTextEdits,
    };
}

} // namespace ned::editor
