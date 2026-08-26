#include "LineEnding.h"

namespace ned::text {

LineEnding DetectLineEnding(std::string_view content) {
    std::size_t crlfCount = 0;
    std::size_t lfCount   = 0;
    std::size_t crCount   = 0;

    for (std::size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\r') {
            if (i + 1 < content.size() && content[i + 1] == '\n') {
                ++crlfCount;
                ++i; // consume the '\n' too
            }
            else {
                ++crCount;
            }
        }
        else if (content[i] == '\n') {
            ++lfCount;
        }
    }

    if (crlfCount >= lfCount && crlfCount >= crCount && crlfCount > 0) {
        return LineEnding::CRLF;
    }
    if (crCount > lfCount) {
        return LineEnding::CR;
    }
    return LineEnding::LF;
}

bool HasCarriageReturn(std::string_view content) {
    return content.find('\r') != std::string_view::npos;
}

std::string NormalizeToLf(std::string_view content) {
    std::string result;
    result.reserve(content.size());
    for (std::size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\r') {
            result.push_back('\n');
            if (i + 1 < content.size() && content[i + 1] == '\n') {
                ++i; // CRLF -> single '\n'
            }
        }
        else {
            result.push_back(content[i]);
        }
    }
    return result;
}

std::string ApplyLineEnding(std::string_view lfContent, LineEnding ending) {
    if (ending == LineEnding::LF) {
        return std::string(lfContent);
    }

    std::string_view target = (ending == LineEnding::CRLF) ? std::string_view("\r\n") : std::string_view("\r");

    std::string result;
    result.reserve(lfContent.size() + lfContent.size() / 8); // rough headroom for CRLF's extra byte
    for (char c : lfContent) {
        if (c == '\n') {
            result.append(target);
        }
        else {
            result.push_back(c);
        }
    }
    return result;
}

const char* LineEndingName(LineEnding ending) {
    switch (ending) {
        case LineEnding::LF: return "LF";
        case LineEnding::CRLF: return "CRLF";
        case LineEnding::CR: return "CR";
    }
    return "LF";
}

} // namespace ned::text
