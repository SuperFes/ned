#include "CommentBuffer.h"

#include <cstddef>
#include <unordered_map>
#include <utility>

#include "Text/Buffer.h"

namespace ned::editor::tracker {

namespace {

    // Keyed by InstanceId, not address: a buffer freed without passing
    // through the close funnel must not hand its target to whatever is
    // allocated where it was.
    std::unordered_map<std::size_t, CommentTarget>& Targets() {
        static std::unordered_map<std::size_t, CommentTarget> targets;
        return targets;
    }

} // namespace

std::string CommentBufferName(const std::string& key) {
    return "*comment " + key + "*";
}

void AttachComment(const text::Buffer& buffer, CommentTarget target) {
    Targets()[buffer.InstanceId()] = std::move(target);
}

std::optional<CommentTarget> FindComment(const text::Buffer& buffer) {
    const auto found = Targets().find(buffer.InstanceId());
    if (found == Targets().end()) {
        return std::nullopt;
    }
    return found->second;
}

void DetachComment(const text::Buffer& buffer) {
    Targets().erase(buffer.InstanceId());
}

std::string CommentText(std::string_view bufferText) {
    std::size_t begin = 0;
    // Leading blank lines go, but a first line's own indentation stays.
    for (std::size_t at = 0; at < bufferText.size(); ++at) {
        if (bufferText[at] == '\n') {
            begin = at + 1;
        }
        else if (bufferText[at] != ' ' && bufferText[at] != '\t' && bufferText[at] != '\r') {
            break;
        }
    }
    std::size_t end = bufferText.size();
    while (end > begin && (bufferText[end - 1] == '\n' || bufferText[end - 1] == ' ' || bufferText[end - 1] == '\t' ||
                           bufferText[end - 1] == '\r')) {
        --end;
    }
    return std::string(bufferText.substr(begin, end - begin));
}

} // namespace ned::editor::tracker
