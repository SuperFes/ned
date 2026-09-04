#include "ValgrindOutputParser.h"

#include <charconv>
#include <cctype>
#include <optional>

namespace ned::editor {

namespace {

    std::string_view Trim(std::string_view s) {
        while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) != 0)) {
            s.remove_prefix(1);
        }
        while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) != 0)) {
            s.remove_suffix(1);
        }
        return s;
    }

    std::optional<std::size_t> ParseSize(std::string_view s) {
        std::size_t value    = 0;
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
        if (ec != std::errc() || ptr != s.data() + s.size()) {
            return std::nullopt;
        }
        return value;
    }

    // Valgrind's own XML text content never needs more than these five --
    // TestOutputParser.h's DecodeXmlEntities' own entity set, duplicated
    // rather than shared (that one lives in an anonymous namespace in a
    // different translation unit; small-helper duplication is this
    // codebase's own stated precedent for this exact situation, see
    // SanitizerOutputParser.cpp's own Trim/SplitLines).
    std::string DecodeXmlEntities(std::string_view text) {
        std::string decoded;
        decoded.reserve(text.size());
        std::size_t i = 0;
        while (i < text.size()) {
            if (text[i] != '&') {
                decoded += text[i++];
                continue;
            }
            const std::size_t semi = text.find(';', i);
            if (semi == std::string_view::npos || semi - i > 10) {
                decoded += text[i++];
                continue;
            }
            const std::string_view entity = text.substr(i + 1, semi - i - 1);
            if (entity == "lt") {
                decoded += '<';
            }
            else if (entity == "gt") {
                decoded += '>';
            }
            else if (entity == "amp") {
                decoded += '&';
            }
            else if (entity == "quot") {
                decoded += '"';
            }
            else if (entity == "apos") {
                decoded += '\'';
            }
            else {
                decoded += text.substr(i, semi - i + 1); // unrecognized -- pass through verbatim
                i = semi + 1;
                continue;
            }
            i = semi + 1;
        }
        return decoded;
    }

    // Extracts the content of the first bare (attribute-free) <tagName>...
    // </tagName> pair found at or after `from` within `text`. When outEnd is
    // given, it's set to the byte offset just past the closing tag, so a
    // caller can resume scanning for siblings rather than re-finding from 0.
    std::optional<std::string_view> ExtractTag(std::string_view text, std::string_view tagName, std::size_t from = 0,
                                                std::size_t* outEnd = nullptr) {
        const std::string openTag  = "<" + std::string(tagName) + ">";
        const std::string closeTag = "</" + std::string(tagName) + ">";
        const std::size_t openPos  = text.find(openTag, from);
        if (openPos == std::string_view::npos) {
            return std::nullopt;
        }
        const std::size_t contentStart = openPos + openTag.size();
        const std::size_t closePos     = text.find(closeTag, contentStart);
        if (closePos == std::string_view::npos) {
            return std::nullopt;
        }
        if (outEnd != nullptr) {
            *outEnd = closePos + closeTag.size();
        }
        return text.substr(contentStart, closePos - contentStart);
    }

    // The first frame in `stackBody` (a <stack>...</stack> block's own inner
    // content) that carries both <file> and a positive <line> -- the finding's
    // location. A frame with no debug info (library code, the malloc/new
    // interceptor itself) is skipped in favor of the next one.
    void LocateFirstDebuggableFrame(std::string_view stackBody, std::string& outFile, std::size_t& outLine) {
        std::size_t frameFrom = 0;
        while (true) {
            std::size_t frameEnd = 0;
            const std::optional<std::string_view> frameBody = ExtractTag(stackBody, "frame", frameFrom, &frameEnd);
            if (!frameBody) {
                return;
            }
            frameFrom = frameEnd;

            const std::optional<std::string_view> fileTag = ExtractTag(*frameBody, "file");
            const std::optional<std::string_view> lineTag = ExtractTag(*frameBody, "line");
            if (!fileTag || !lineTag) {
                continue;
            }
            const std::optional<std::size_t> lineVal = ParseSize(Trim(*lineTag));
            if (!lineVal || *lineVal == 0) {
                continue;
            }

            std::string file = DecodeXmlEntities(Trim(*fileTag));
            if (const std::optional<std::string_view> dirTag = ExtractTag(*frameBody, "dir")) {
                const std::string_view dir = Trim(*dirTag);
                if (!dir.empty()) {
                    file = DecodeXmlEntities(dir) + "/" + file;
                }
            }
            outFile = std::move(file);
            outLine = *lineVal;
            return;
        }
    }

} // namespace

std::vector<ValgrindFinding> ParseValgrindXml(std::string_view output) {
    std::vector<ValgrindFinding> findings;

    std::string tool;
    if (const std::optional<std::string_view> toolText = ExtractTag(output, "tool")) {
        tool = std::string(Trim(*toolText));
    }

    std::size_t searchFrom = 0;
    while (true) {
        std::size_t                           errorEnd = 0;
        const std::optional<std::string_view> errorBody = ExtractTag(output, "error", searchFrom, &errorEnd);
        if (!errorBody) {
            break;
        }
        searchFrom = errorEnd;

        ValgrindFinding finding;
        finding.tool = tool;
        if (const std::optional<std::string_view> kind = ExtractTag(*errorBody, "kind")) {
            finding.kind = std::string(Trim(*kind));
        }

        if (const std::optional<std::string_view> xwhat = ExtractTag(*errorBody, "xwhat")) {
            if (const std::optional<std::string_view> text = ExtractTag(*xwhat, "text")) {
                finding.message = DecodeXmlEntities(*text);
            }
        }
        if (finding.message.empty()) {
            if (const std::optional<std::string_view> what = ExtractTag(*errorBody, "what")) {
                finding.message = DecodeXmlEntities(*what);
            }
        }

        if (const std::optional<std::string_view> stackBody = ExtractTag(*errorBody, "stack")) {
            LocateFirstDebuggableFrame(*stackBody, finding.file, finding.line);
        }

        if (finding.kind.empty() && finding.message.empty()) {
            continue; // not a real error block -- malformed/unexpected input, skip rather than emit junk
        }
        findings.push_back(std::move(finding));
    }

    return findings;
}

} // namespace ned::editor
