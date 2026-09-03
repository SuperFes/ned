#include "IncrementalSearch.h"

#include <algorithm>

#include "Text/Utf8.h"

namespace ned::editor {

namespace {

    // ASCII alphanumeric + underscore -- deliberately not Unicode-aware,
    // matching Buffer::MoveForwardWord's own word-char definition (see its
    // doc comment in Buffer.h) since content_ has no codepoint-decoding
    // available to it, only raw UTF-8 bytes.
    bool IsWordByte(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

    char AsciiLower(char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    std::string ToAsciiLower(std::string_view text) {
        std::string result(text);
        for (char& c : result) {
            c = AsciiLower(c);
        }
        return result;
    }

    bool HasAsciiUppercase(std::string_view text) {
        for (const char c : text) {
            if (c >= 'A' && c <= 'Z') {
                return true;
            }
        }
        return false;
    }

} // namespace

IncrementalSearch::IncrementalSearch(text::Buffer& buffer, Direction direction)
    : buffer_(buffer), direction_(direction), huge_(buffer.Content().IsHuge()),
      content_(huge_ ? std::string() : buffer.Text()), contentLower_(huge_ ? std::string() : ToAsciiLower(content_)),
      originalPoint_(buffer.Point()) {}

void IncrementalSearch::AppendChar(char32_t codepoint) {
    query_ += text::EncodeCodepointUtf8(codepoint);
    Search(originalPoint_);
}

void IncrementalSearch::DeleteChar() {
    if (query_.empty()) {
        return;
    }
    text::RemoveLastCodepoint(query_);
    Search(originalPoint_);
}

void IncrementalSearch::AppendText(std::string_view text) {
    if (text.empty()) {
        return;
    }
    query_ += text;
    Search(originalPoint_);
}

void IncrementalSearch::AppendWordAtPoint() {
    if (huge_) {
        // Bounded lookahead rather than content_ (empty for a huge buffer)
        // -- a word run longer than this is never real source text, so
        // this is the same "few-MiB-of-real-content" assumption the rest
        // of huge-file handling already makes.
        constexpr std::size_t kLookahead = 4096;
        const std::size_t     point      = buffer_.Point();
        const std::size_t     total      = buffer_.Content().ByteLength();
        if (point >= total) {
            return;
        }
        const std::string window = buffer_.Content().Substring(point, std::min(kLookahead, total - point));

        std::size_t offset = 0;
        while (offset < window.size() && !IsWordByte(window[offset])) {
            ++offset;
        }
        const std::size_t start = offset;
        while (offset < window.size() && IsWordByte(window[offset])) {
            ++offset;
        }
        if (offset == start) {
            return;
        }
        AppendText(std::string_view(window).substr(start, offset - start));
        return;
    }

    std::size_t       offset = buffer_.Point();
    const std::size_t total  = content_.size();

    while (offset < total && !IsWordByte(content_[offset])) {
        ++offset;
    }
    const std::size_t start = offset;
    while (offset < total && IsWordByte(content_[offset])) {
        ++offset;
    }
    if (offset == start) {
        return; // no word-shaped text left in this direction
    }
    AppendText(std::string_view(content_).substr(start, offset - start));
}

void IncrementalSearch::RepeatSearch() {
    if (query_.empty()) {
        return;
    }
    Search(buffer_.Point());
}

void IncrementalSearch::ReverseDirection() {
    direction_ = (direction_ == Direction::Forward) ? Direction::Backward : Direction::Forward;
    if (query_.empty()) {
        return;
    }
    Search(buffer_.Point());
}

void IncrementalSearch::Accept() {
    // Nothing to do here -- point is already at the current match (or
    // unchanged if the query never matched). The session simply ends when
    // the caller discards this object.
}

void IncrementalSearch::Cancel() {
    buffer_.SetPoint(originalPoint_);
}

const std::string& IncrementalSearch::Query() const {
    return query_;
}

bool IncrementalSearch::Found() const {
    return found_;
}

std::string IncrementalSearch::StatusLabel() const {
    std::string label;
    if (!found_) {
        label += "Failing ";
    }
    if (direction_ == Direction::Backward) {
        label += "Backward ";
    }
    label += "I-search: ";
    return label;
}

std::string IncrementalSearch::StatusText() const {
    return StatusLabel() + query_;
}

std::size_t IncrementalSearch::MatchedPrefixLength() const {
    return matchedPrefixLength_;
}

std::size_t IncrementalSearch::ComputeMatchedPrefixLength() const {
    const bool          caseSensitive = HasAsciiUppercase(query_);
    const std::string&  haystack      = caseSensitive ? content_ : contentLower_;
    const std::string   needleOwner   = caseSensitive ? query_ : ToAsciiLower(query_);

    std::size_t len = text::PreviousCodepointBoundary(query_, query_.size());
    while (len > 0) {
        if (haystack.find(needleOwner.substr(0, len)) != std::string::npos) {
            return len;
        }
        len = text::PreviousCodepointBoundary(query_, len);
    }
    return 0;
}

void IncrementalSearch::Search(std::size_t from) {
    if (query_.empty()) {
        found_               = true;
        matchedPrefixLength_ = 0;
        buffer_.SetPoint(originalPoint_);
        return;
    }

    if (huge_) {
        SearchHuge(from);
        return;
    }

    // Smart case, Emacs-style: case-insensitive unless the query itself
    // contains an uppercase letter (see this file's header comment).
    const bool          caseSensitive = HasAsciiUppercase(query_);
    const std::string&  haystack      = caseSensitive ? content_ : contentLower_;
    const std::string   needleOwner   = caseSensitive ? std::string() : ToAsciiLower(query_);
    const std::string&  needle        = caseSensitive ? query_ : needleOwner;

    std::size_t pos;
    if (direction_ == Direction::Forward) {
        pos = haystack.find(needle, from);
        if (pos == std::string::npos) {
            pos = haystack.find(needle); // wrap around to the top of the document
        }
    } else {
        pos = (from == 0) ? std::string::npos : haystack.rfind(needle, from - 1);
        if (pos == std::string::npos) {
            pos = haystack.rfind(needle); // wrap around to the bottom of the document
        }
    }

    if (pos == std::string::npos) {
        found_               = false;
        matchedPrefixLength_ = ComputeMatchedPrefixLength();
        return; // point stays wherever the last successful match left it
    }

    found_               = true;
    matchedPrefixLength_ = query_.size();
    buffer_.SetPoint(direction_ == Direction::Forward ? pos + query_.size() : pos);
}

// huge-file-search-and-save follow-up: same smart-case/wrap-around
// semantics as the in-memory Search() above, but reading the haystack in
// bounded windows via Content().Substring instead of content_/
// contentLower_ (both empty for a huge buffer). Each window overlaps its
// neighbor by needle.size() - 1 bytes so a match straddling a window
// boundary is never missed. This still costs an O(document size) scan in
// the worst case (query not found anywhere, or found only after wrapping)
// -- unavoidable for a genuine whole-document search -- but never holds
// more than one window's worth of the buffer in memory at a time.
void IncrementalSearch::SearchHuge(std::size_t from) {
    constexpr std::size_t kWindow = 4 * 1024 * 1024;

    const std::size_t total         = buffer_.Content().ByteLength();
    const bool         caseSensitive = HasAsciiUppercase(query_);
    const std::string  needle        = caseSensitive ? query_ : ToAsciiLower(query_);
    const std::size_t  needleLen     = needle.size();

    auto readLower = [&](std::size_t start, std::size_t length) {
        std::string window = buffer_.Content().Substring(start, length);
        if (!caseSensitive) {
            for (char& c : window) {
                c = AsciiLower(c);
            }
        }
        return window;
    };

    // Scans [start, total) left to right, kWindow bytes at a time.
    auto scanForwardFrom = [&](std::size_t start) -> std::size_t {
        std::size_t offset = start;
        while (offset < total) {
            const std::size_t windowLen = std::min(kWindow + needleLen - 1, total - offset);
            const std::string window    = readLower(offset, windowLen);
            const std::size_t  pos      = window.find(needle);
            if (pos != std::string::npos) {
                return offset + pos;
            }
            offset += kWindow;
        }
        return std::string::npos;
    };

    // Scans [0, beforeExclusive) right to left, kWindow bytes at a time.
    auto scanBackwardFrom = [&](std::size_t beforeExclusive) -> std::size_t {
        std::size_t end = beforeExclusive;
        while (end > 0) {
            const std::size_t windowLen = std::min(kWindow + needleLen - 1, end);
            const std::size_t start     = end - windowLen;
            const std::string window    = readLower(start, windowLen);
            const std::size_t  pos      = window.rfind(needle);
            if (pos != std::string::npos) {
                return start + pos;
            }
            if (start == 0) {
                break;
            }
            end = start + needleLen - 1; // re-approach, keeping the same overlap
        }
        return std::string::npos;
    };

    std::size_t pos;
    if (direction_ == Direction::Forward) {
        pos = scanForwardFrom(from);
        if (pos == std::string::npos) {
            pos = scanForwardFrom(0); // wrap around to the top of the document
        }
    } else {
        pos = scanBackwardFrom(from);
        if (pos == std::string::npos) {
            pos = scanBackwardFrom(total); // wrap around to the bottom of the document
        }
    }

    if (pos == std::string::npos) {
        found_ = false;
        // partial-match-highlighting follow-up: no partial-prefix info for a
        // huge buffer (see MatchedPrefixLength()'s own doc comment) --
        // query_.size() itself signals "not available" to a caller.
        matchedPrefixLength_ = query_.size();
        return; // point stays wherever the last successful match left it
    }

    found_               = true;
    matchedPrefixLength_ = query_.size();
    buffer_.SetPoint(direction_ == Direction::Forward ? pos + query_.size() : pos);
}

} // namespace ned::editor
