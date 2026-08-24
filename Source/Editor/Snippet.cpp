#include "Editor/Snippet.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <map>
#include <optional>

namespace ned::editor {

namespace {

    // One tokenized tabstop marker, before substitution.
    struct RawStop {
        int         index;
        std::string placeholder; // nested markers already stripped
        bool        hasPlaceholder;
    };

    std::optional<RawStop> ParseTabstopAt(std::string_view body, std::size_t& pos);

    // Reads placeholder content from just past the ':' up to the matching
    // unescaped '}', consuming it. A nested tabstop marker contributes its
    // own placeholder text only (the inner stop is dropped -- see the
    // header's cuts). nullopt when the closing '}' is missing.
    std::optional<std::string> ParsePlaceholderContent(std::string_view body, std::size_t& pos) {
        std::string content;
        while (pos < body.size()) {
            const char c = body[pos];
            if (c == '\\' && pos + 1 < body.size()) {
                const char next = body[pos + 1];
                if (next == '}' || next == '$' || next == '\\') {
                    content.push_back(next);
                    pos += 2;
                    continue;
                }
                content.push_back(c);
                ++pos;
                continue;
            }
            if (c == '}') {
                ++pos;
                return content;
            }
            if (c == '$') {
                std::size_t probe = pos;
                if (const auto inner = ParseTabstopAt(body, probe)) {
                    content += inner->placeholder;
                    pos = probe;
                    continue;
                }
            }
            content.push_back(c);
            ++pos;
        }
        return std::nullopt;
    }

    // Bounded so a pathological "$9999999999" can't overflow; anything past
    // the cap is treated as ill-formed and falls through as literal text.
    std::optional<int> ParseIndexDigits(std::string_view body, std::size_t& pos) {
        constexpr std::size_t kMaxIndexDigits = 6;
        const std::size_t     start           = pos;
        while (pos < body.size() && std::isdigit(static_cast<unsigned char>(body[pos]))) {
            ++pos;
        }
        if (pos == start || pos - start > kMaxIndexDigits) {
            pos = start;
            return std::nullopt;
        }
        int value = 0;
        for (std::size_t i = start; i < pos; ++i) {
            value = value * 10 + (body[i] - '0');
        }
        return value;
    }

    // Attempts to parse a tabstop marker at body[pos] (which is '$'). On
    // success advances pos past the marker; on failure leaves pos untouched
    // so the caller emits the '$' literally.
    std::optional<RawStop> ParseTabstopAt(std::string_view body, std::size_t& pos) {
        std::size_t p = pos + 1;
        if (p >= body.size()) {
            return std::nullopt;
        }
        if (std::isdigit(static_cast<unsigned char>(body[p]))) {
            const auto index = ParseIndexDigits(body, p);
            if (!index) {
                return std::nullopt;
            }
            pos = p;
            return RawStop{*index, "", false};
        }
        if (body[p] != '{') {
            return std::nullopt;
        }
        ++p;
        const auto index = ParseIndexDigits(body, p);
        if (!index || p >= body.size()) {
            return std::nullopt;
        }
        if (body[p] == '}') {
            pos = p + 1;
            return RawStop{*index, "", false};
        }
        if (body[p] != ':') {
            return std::nullopt; // ${1|...|}/${1/.../} choice/transform forms: ill-formed here
        }
        ++p;
        auto content = ParsePlaceholderContent(body, p);
        if (!content) {
            return std::nullopt;
        }
        pos = p;
        return RawStop{*index, std::move(*content), true};
    }

} // namespace

ParsedSnippet ParseSnippet(std::string_view body) {
    // Pass 1: tokenize into literal runs and tabstop markers.
    struct Piece {
        bool        isStop = false;
        std::string literal;
        RawStop     stop{};
    };
    std::vector<Piece> pieces;
    std::string        literal;
    const auto         flushLiteral = [&] {
        if (!literal.empty()) {
            pieces.push_back(Piece{false, std::move(literal), {}});
            literal.clear();
        }
    };
    std::size_t pos = 0;
    while (pos < body.size()) {
        const char c = body[pos];
        if (c == '\\' && pos + 1 < body.size() && (body[pos + 1] == '$' || body[pos + 1] == '\\')) {
            literal.push_back(body[pos + 1]);
            pos += 2;
            continue;
        }
        if (c == '$') {
            std::size_t probe = pos;
            if (auto stop = ParseTabstopAt(body, probe)) {
                flushLiteral();
                pieces.push_back(Piece{true, "", std::move(*stop)});
                pos = probe;
                continue;
            }
        }
        literal.push_back(c);
        ++pos;
    }
    flushLiteral();

    // Each index's substitution text: its first placeholder-carrying
    // occurrence wins; an index with no placeholder anywhere substitutes "".
    std::map<int, std::string> primaryText;
    std::map<int, std::size_t> primaryPiece; // piece position of the winning occurrence
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const Piece& piece = pieces[i];
        if (!piece.isStop) {
            continue;
        }
        if (!primaryPiece.contains(piece.stop.index) || (piece.stop.hasPlaceholder && !pieces[primaryPiece[piece.stop.index]].stop.hasPlaceholder)) {
            primaryPiece[piece.stop.index] = i;
            primaryText[piece.stop.index]  = piece.stop.placeholder;
        }
    }

    // Pass 2: emit stripped text, recording every occurrence as a field.
    ParsedSnippet result;
    struct Emitted {
        SnippetField field;
        bool         primary;
        std::size_t  piecePos;
    };
    std::vector<Emitted> emitted;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const Piece& piece = pieces[i];
        if (!piece.isStop) {
            result.text += piece.literal;
            continue;
        }
        const std::string& substitution = primaryText[piece.stop.index];
        const std::size_t  start        = result.text.size();
        result.text += substitution;
        emitted.push_back(Emitted{SnippetField{piece.stop.index, start, result.text.size()},
                                  primaryPiece[piece.stop.index] == i, i});
    }

    // Visit order: ascending index with 0 last; within an index the primary
    // occurrence first, then mirrors in document order (stable sort keeps
    // piece order for equal keys).
    std::stable_sort(emitted.begin(), emitted.end(), [](const Emitted& a, const Emitted& b) {
        const int keyA = a.field.index == 0 ? INT_MAX : a.field.index;
        const int keyB = b.field.index == 0 ? INT_MAX : b.field.index;
        if (keyA != keyB) {
            return keyA < keyB;
        }
        return a.primary && !b.primary;
    });
    result.fields.reserve(emitted.size() + 1);
    for (const Emitted& e : emitted) {
        result.fields.push_back(e.field);
    }
    if (std::none_of(result.fields.begin(), result.fields.end(),
                     [](const SnippetField& f) { return f.index == 0; })) {
        result.fields.push_back(SnippetField{0, result.text.size(), result.text.size()});
    }
    return result;
}

std::optional<SnippetSession> SnippetSession::Start(text::Buffer& buffer, std::string bufferName,
                                                    std::size_t replaceStart, std::size_t replaceEnd,
                                                    const ParsedSnippet& parsed) {
    buffer.BeginUndoGroup();
    if (replaceEnd > replaceStart) {
        buffer.DeleteRange(replaceStart, replaceEnd - replaceStart);
    }
    buffer.InsertAt(replaceStart, parsed.text);

    const bool onlyFinalStop =
        std::all_of(parsed.fields.begin(), parsed.fields.end(), [](const SnippetField& f) { return f.index == 0; });
    if (onlyFinalStop) {
        buffer.EndUndoGroup();
        buffer.SetPoint(replaceStart + parsed.fields.front().start);
        return std::nullopt;
    }

    std::vector<text::Buffer::SnippetRange> ranges;
    ranges.reserve(parsed.fields.size());
    std::size_t nextId = 1;
    for (const SnippetField& field : parsed.fields) {
        ranges.push_back(text::Buffer::SnippetRange{nextId++, field.index, replaceStart + field.start,
                                                    replaceStart + field.end, false});
    }
    buffer.SetSnippetRanges(std::move(ranges));

    SnippetSession session;
    session.bufferName_ = std::move(bufferName);
    for (const SnippetField& field : parsed.fields) {
        // parsed.fields is already in visit order (ascending index, 0 last)
        if (session.visitOrder_.empty() || session.visitOrder_.back() != field.index) {
            session.visitOrder_.push_back(field.index);
        }
    }
    session.activePos_ = 0;
    session.EnterActiveField(buffer);
    buffer.EndUndoGroup();
    return session;
}

void SnippetSession::EnterActiveField(text::Buffer& buffer) {
    const int index = visitOrder_[activePos_];
    // The index's primary range is the first one carrying it --
    // SetSnippetRanges preserved ParsedSnippet::fields' primary-first
    // order, and relocation never reorders the vector.
    for (const text::Buffer::SnippetRange& range : buffer.SnippetRanges()) {
        if (range.tabstopIndex == index) {
            activeRangeId_ = range.id;
            buffer.SetActiveSnippetRange(range.id);
            buffer.SetPoint(range.end);
            pristine_       = range.end > range.start;
            lastSyncedText_ = buffer.Content().Substring(range.start, range.end - range.start);
            return;
        }
    }
}

SnippetSession::NavResult SnippetSession::NextField(text::Buffer& buffer) {
    if (activePos_ + 1 >= visitOrder_.size()) {
        return NavResult::Finished;
    }
    ++activePos_;
    if (visitOrder_[activePos_] == 0) {
        for (const text::Buffer::SnippetRange& range : buffer.SnippetRanges()) {
            if (range.tabstopIndex == 0) {
                buffer.SetPoint(range.start);
                break;
            }
        }
        return NavResult::Finished;
    }
    EnterActiveField(buffer);
    return NavResult::Moved;
}

SnippetSession::NavResult SnippetSession::PreviousField(text::Buffer& buffer) {
    if (activePos_ == 0) {
        return NavResult::Moved; // already at the first field -- stay
    }
    --activePos_;
    EnterActiveField(buffer);
    return NavResult::Moved;
}

void SnippetSession::Finish(text::Buffer& buffer) {
    buffer.ClearSnippetRanges();
}

bool SnippetSession::Pristine() const {
    return pristine_;
}

void SnippetSession::ClearPristine() {
    pristine_ = false;
}

void SnippetSession::DeleteActiveFieldContent(text::Buffer& buffer) {
    const text::Buffer::SnippetRange* range = FindRange(buffer, activeRangeId_);
    if (range != nullptr && range->end > range->start) {
        buffer.DeleteRange(range->start, range->end - range->start);
    }
}

void SnippetSession::SyncMirrors(text::Buffer& buffer) {
    const text::Buffer::SnippetRange* active = FindRange(buffer, activeRangeId_);
    if (active == nullptr) {
        return;
    }
    const std::string current = buffer.Content().Substring(active->start, active->end - active->start);
    if (current == lastSyncedText_) {
        return;
    }
    const int                index = active->tabstopIndex;
    std::vector<std::size_t> mirrorIds;
    for (const text::Buffer::SnippetRange& range : buffer.SnippetRanges()) {
        if (range.tabstopIndex == index && range.id != activeRangeId_) {
            mirrorIds.push_back(range.id);
        }
    }
    // A rewrite of a mirror directly adjacent to the active field inserts
    // exactly at point (the field's own edge), and Point_'s right-gravity
    // relocation would drag point to the end of the mirror's fresh text --
    // remember where point sits relative to the active field and restore
    // it after the rewrites. Point outside the field (the user moved away
    // mid-session) just rides ordinary relocation instead.
    const std::size_t pointBefore      = buffer.Point();
    const bool        pointInActive    = pointBefore >= active->start && pointBefore <= active->end;
    const std::size_t pointFieldOffset = pointInActive ? pointBefore - active->start : 0;
    // Deactivate for the rewrites: the active range's grow-at-boundary
    // gravity would absorb a rewrite of a directly adjacent mirror (the
    // insert lands exactly at the active field's own edge); with every
    // range inactive, boundary inserts stay excluded everywhere and each
    // rewritten mirror is repaired explicitly below.
    buffer.SetActiveSnippetRange(0); // 0 is never an assigned id -- clears every flag
    for (const std::size_t id : mirrorIds) {
        // Re-resolve per iteration: each rewrite relocates every other range.
        const text::Buffer::SnippetRange* mirror = FindRange(buffer, id);
        if (mirror == nullptr) {
            continue;
        }
        const std::size_t start = mirror->start;
        if (buffer.Content().Substring(start, mirror->end - start) == current) {
            continue;
        }
        if (mirror->end > start) {
            buffer.DeleteRange(start, mirror->end - start);
        }
        if (!current.empty()) {
            buffer.InsertAt(start, current);
        }
        buffer.UpdateSnippetRange(id, start, start + current.size());
    }
    buffer.SetActiveSnippetRange(activeRangeId_);
    if (pointInActive) {
        if (const text::Buffer::SnippetRange* after = FindRange(buffer, activeRangeId_)) {
            buffer.SetPoint(after->start + pointFieldOffset);
        }
    }
    lastSyncedText_ = current;
}

bool SnippetSession::RangesValid(const text::Buffer& buffer) const {
    return !buffer.SnippetRanges().empty();
}

std::optional<std::pair<std::size_t, std::size_t>> SnippetSession::ActiveFieldRange(const text::Buffer& buffer) const {
    const text::Buffer::SnippetRange* range = FindRange(buffer, activeRangeId_);
    if (range == nullptr) {
        return std::nullopt;
    }
    return std::make_pair(range->start, range->end);
}

const std::string& SnippetSession::BufferName() const {
    return bufferName_;
}

std::string SnippetSession::StatusText() const {
    const std::size_t total = visitOrder_.empty() ? 0 : visitOrder_.size() - 1; // the final stop isn't a field
    return "Snippet field " + std::to_string(activePos_ + 1) + "/" + std::to_string(total) + " (TAB next, S-TAB previous, ESC done)";
}

const text::Buffer::SnippetRange* SnippetSession::FindRange(const text::Buffer& buffer, std::size_t id) const {
    for (const text::Buffer::SnippetRange& range : buffer.SnippetRanges()) {
        if (range.id == id) {
            return &range;
        }
    }
    return nullptr;
}

} // namespace ned::editor
