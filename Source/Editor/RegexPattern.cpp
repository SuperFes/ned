#include "RegexPattern.h"

#include <cstdint>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "Text/Utf8.h"

namespace ned::editor {

namespace {

    // Backtracking safety net: PCRE2's own default is 10 million internal
    // match steps; this is deliberately tighter. A legitimate editor-scale
    // match (one line, or one file's worth of subject) finishes in far fewer
    // steps -- hitting this means catastrophic backtracking, and the right
    // outcome is a visible error, not seconds of frozen UI before the
    // default limit trips.
    constexpr std::uint32_t kMatchLimit = 1'000'000;

    std::string Pcre2ErrorMessage(int errorCode) {
        PCRE2_UCHAR message[256];
        if (pcre2_get_error_message(errorCode, message, sizeof(message)) < 0) {
            return "unknown PCRE2 error " + std::to_string(errorCode);
        }
        return std::string(reinterpret_cast<const char*>(message));
    }

} // namespace

struct RegexPattern::Impl {
    pcre2_code*          code         = nullptr;
    pcre2_match_context* matchContext = nullptr;
    std::uint32_t        captureCount = 0;

    ~Impl() {
        if (matchContext != nullptr) {
            pcre2_match_context_free(matchContext);
        }
        if (code != nullptr) {
            pcre2_code_free(code);
        }
    }
};

RegexPattern::RegexPattern(const std::string& pattern) : impl_(std::make_unique<Impl>()) {
    pcre2_compile_context* compileContext = pcre2_compile_context_create(nullptr);
    if (compileContext != nullptr) {
        // Deterministic ^/$ behavior regardless of how PCRE2 was configured
        // at build time -- buffers/files in this codebase are LF-newline
        // byte streams (a CR before the LF is just a byte on the line).
        pcre2_set_newline(compileContext, PCRE2_NEWLINE_LF);
    }

    int        errorCode   = 0;
    PCRE2_SIZE errorOffset = 0;
    impl_->code            = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(),
                                           PCRE2_UTF | PCRE2_UCP | PCRE2_MULTILINE | PCRE2_MATCH_INVALID_UTF,
                                           &errorCode, &errorOffset, compileContext);
    pcre2_compile_context_free(compileContext);

    if (impl_->code == nullptr) {
        throw RegexPatternError(Pcre2ErrorMessage(errorCode) + " (at pattern offset " + std::to_string(errorOffset) +
                                ")");
    }

    // Best effort -- on failure pcre2_match transparently falls back to the
    // interpreter, so the result is identical, just slower.
    pcre2_jit_compile(impl_->code, PCRE2_JIT_COMPLETE);

    pcre2_pattern_info(impl_->code, PCRE2_INFO_CAPTURECOUNT, &impl_->captureCount);

    impl_->matchContext = pcre2_match_context_create(nullptr);
    if (impl_->matchContext != nullptr) {
        pcre2_set_match_limit(impl_->matchContext, kMatchLimit);
    }
}

RegexPattern::~RegexPattern()                                  = default;
RegexPattern::RegexPattern(RegexPattern&&) noexcept            = default;
RegexPattern& RegexPattern::operator=(RegexPattern&&) noexcept = default;

std::optional<RegexMatch> RegexPattern::Search(std::string_view subject, std::size_t startOffset) const {
    if (startOffset > subject.size()) {
        return std::nullopt;
    }

    const std::unique_ptr<pcre2_match_data, decltype(&pcre2_match_data_free)> matchData(
        pcre2_match_data_create_from_pattern(impl_->code, nullptr), &pcre2_match_data_free);
    if (!matchData) {
        throw RegexPatternError("failed to allocate PCRE2 match data");
    }

    const int rc = pcre2_match(impl_->code, reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.size(), startOffset,
                               0, matchData.get(), impl_->matchContext);
    if (rc == PCRE2_ERROR_NOMATCH) {
        return std::nullopt;
    }
    if (rc == PCRE2_ERROR_MATCHLIMIT || rc == PCRE2_ERROR_DEPTHLIMIT) {
        throw RegexPatternError("match limit exceeded -- this pattern backtracks catastrophically on this text");
    }
    if (rc < 0) {
        throw RegexPatternError("regex match failed: " + Pcre2ErrorMessage(rc));
    }

    const PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(matchData.get());

    RegexMatch match;
    match.start = ovector[0];
    match.end   = ovector[1];
    match.groups.resize(impl_->captureCount + 1);
    // rc is one more than the highest-numbered pair that was set; pairs at
    // or above it were never written, so only read below it.
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(rc) && i <= impl_->captureCount; ++i) {
        if (ovector[2 * i] == PCRE2_UNSET) {
            continue;
        }
        match.groups[i] = RegexGroupSpan{true, ovector[2 * i], ovector[2 * i + 1]};
    }
    return match;
}

namespace {

    std::string_view GroupText(std::string_view subject, const RegexMatch& match, std::size_t groupNumber) {
        if (groupNumber >= match.groups.size() || !match.groups[groupNumber].matched) {
            return {}; // a valid group that didn't participate expands to nothing
        }
        const RegexGroupSpan& span = match.groups[groupNumber];
        return subject.substr(span.start, span.end - span.start);
    }

} // namespace

std::string RegexPattern::FormatReplacement(std::string_view subject, const RegexMatch& match,
                                            std::string_view replacementTemplate) const {
    std::string out;
    out.reserve(replacementTemplate.size());

    const std::string_view t = replacementTemplate;
    for (std::size_t i = 0; i < t.size();) {
        if (t[i] != '$' || i + 1 >= t.size()) {
            out.push_back(t[i]);
            ++i;
            continue;
        }

        const char next = t[i + 1];
        if (next == '$') {
            out.push_back('$');
            i += 2;
        }
        else if (next == '&') {
            out.append(GroupText(subject, match, 0));
            i += 2;
        }
        else if (next == '`') {
            out.append(subject.substr(0, match.start));
            i += 2;
        }
        else if (next == '\'') {
            out.append(subject.substr(match.end));
            i += 2;
        }
        else if (next >= '0' && next <= '9') {
            // $n / $nn -- prefer the two-digit reference when it names a
            // real group, else fall back to one digit (std::regex's own
            // rule), else the '$' is literal.
            std::size_t digits = (i + 2 < t.size() && t[i + 2] >= '0' && t[i + 2] <= '9') ? 2 : 1;
            std::size_t value  = 0;
            for (; digits > 0; --digits) {
                value = 0;
                for (std::size_t d = 0; d < digits; ++d) {
                    value = value * 10 + static_cast<std::size_t>(t[i + 1 + d] - '0');
                }
                if (value <= impl_->captureCount) { // $0 is the whole match
                    break;
                }
            }
            if (digits == 0) {
                out.push_back('$');
                ++i;
            }
            else {
                out.append(GroupText(subject, match, value));
                i += 1 + digits;
            }
        }
        else if (next == '{') {
            const std::size_t close = t.find('}', i + 2);
            if (close == std::string_view::npos || close == i + 2) {
                out.push_back('$'); // no closing brace / empty name: literal, brace re-parsed as plain text
                ++i;
                continue;
            }
            const std::string name(t.substr(i + 2, close - i - 2));

            std::optional<std::size_t> groupNumber;
            if (name.find_first_not_of("0123456789") == std::string::npos) {
                const std::size_t value = std::stoul(name);
                if (value <= impl_->captureCount) {
                    groupNumber = value;
                }
            }
            else {
                const int number = pcre2_substring_number_from_name(impl_->code,
                                                                    reinterpret_cast<PCRE2_SPTR>(name.c_str()));
                if (number > 0) {
                    groupNumber = static_cast<std::size_t>(number);
                }
            }

            if (!groupNumber.has_value()) {
                out.push_back('$'); // unknown group/name: literal
                ++i;
                continue;
            }
            out.append(GroupText(subject, match, *groupNumber));
            i = close + 1;
        }
        else {
            out.push_back('$');
            ++i;
        }
    }

    return out;
}

RegexPattern::ReplaceAllResult RegexPattern::ReplaceAll(std::string_view subject,
                                                        std::string_view replacementTemplate) const {
    ReplaceAllResult result;
    std::size_t      copiedThrough = 0; // subject bytes already appended to result.text
    std::size_t      searchFrom    = 0;

    while (const std::optional<RegexMatch> match = Search(subject, searchFrom)) {
        result.text.append(subject.substr(copiedThrough, match->start - copiedThrough));
        result.text.append(FormatReplacement(subject, *match, replacementTemplate));
        ++result.count;
        copiedThrough = match->end;

        if (match->end == match->start) {
            // Zero-width match: copy one codepoint through and continue past
            // it, or stop if there's nothing left to step over.
            const std::size_t next = text::NextCodepointBoundary(subject, match->end);
            if (next == match->end) {
                break;
            }
            result.text.append(subject.substr(match->end, next - match->end));
            copiedThrough = next;
            searchFrom    = next;
        }
        else {
            searchFrom = match->end;
        }
    }

    result.text.append(subject.substr(copiedThrough));
    return result;
}

} // namespace ned::editor
