#include "LineEndingPolicy.h"

#include <mutex>

namespace ned::editor {

namespace {

std::mutex& LineEndingPolicyMutex() {
    static std::mutex mutex;
    return mutex;
}

LineEndingPolicy& LineEndingPolicyStorage() {
    static LineEndingPolicy policy;
    return policy;
}

} // namespace

void SetLineEndingPolicy(LineEndingPolicy policy) {
    const std::lock_guard<std::mutex> lock(LineEndingPolicyMutex());
    LineEndingPolicyStorage() = policy;
}

LineEndingPolicy GetLineEndingPolicy() {
    const std::lock_guard<std::mutex> lock(LineEndingPolicyMutex());
    return LineEndingPolicyStorage();
}

void SetLineEndingPolicyFromString(const std::string& value) {
    if (value == "preserve") {
        SetLineEndingPolicy({LineEndingPolicyMode::Preserve, ned::text::LineEnding::LF});
    }
    else if (value == "lf") {
        SetLineEndingPolicy({LineEndingPolicyMode::Force, ned::text::LineEnding::LF});
    }
    else if (value == "crlf") {
        SetLineEndingPolicy({LineEndingPolicyMode::Force, ned::text::LineEnding::CRLF});
    }
    else if (value == "cr") {
        SetLineEndingPolicy({LineEndingPolicyMode::Force, ned::text::LineEnding::CR});
    }
    // else: unrecognized -- leave the current policy unchanged.
}

ned::text::LineEnding ResolveLineEndingForSave(ned::text::LineEnding bufferEnding) {
    const LineEndingPolicy policy = GetLineEndingPolicy();
    if (policy.mode == LineEndingPolicyMode::Force) {
        return policy.forcedEnding;
    }
    return bufferEnding;
}

} // namespace ned::editor
