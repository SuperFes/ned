#include "InlineDiagnostics.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& EnabledMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& EnabledStorage() {
        static bool enabled = true;
        return enabled;
    }

    InlineDiagnosticStyle& StyleStorage() {
        static InlineDiagnosticStyle style = InlineDiagnosticStyle::EndOfLine;
        return style;
    }

} // namespace

void SetInlineDiagnosticsEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    EnabledStorage() = enabled;
}

bool InlineDiagnosticsEnabled() {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    return EnabledStorage();
}

void SetInlineDiagnosticStyle(InlineDiagnosticStyle style) {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    StyleStorage() = style;
}

InlineDiagnosticStyle GetInlineDiagnosticStyle() {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    return StyleStorage();
}

} // namespace ned::editor
