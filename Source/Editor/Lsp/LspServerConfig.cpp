#include "LspServerConfig.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace ned::editor::lsp {

namespace {

    std::mutex                                                g_mutex;
    std::unordered_map<std::string, std::vector<std::string>> g_commands;

    std::mutex g_autoCompleteMutex;
    bool       g_autoCompleteEnabled = true;

    std::mutex g_debounceMutex;
    int        g_completionDebounceMs = 500;

    std::mutex g_diagnosticsDebounceMutex;
    int        g_diagnosticsDebounceMs = 500;

    std::mutex g_syncDebounceMutex;
    int        g_syncDebounceMs = 150;

    std::mutex g_signatureHelpAutoTriggerMutex;
    bool       g_signatureHelpAutoTriggerEnabled = true;

    std::mutex g_formatOnSaveMutex;
    bool       g_formatOnSaveEnabled = false;

    std::mutex g_onTypeFormattingMutex;
    bool       g_onTypeFormattingEnabled = false;

    std::mutex g_pullDiagnosticsMutex;
    bool       g_pullDiagnosticsEnabled = false;

    std::mutex g_semanticHighlightingMutex;
    bool       g_semanticHighlightingEnabled = true;

    std::mutex g_inlayHintsMutex;
    bool       g_inlayHintsEnabled = true;

    std::mutex g_codeLensMutex;
    bool       g_codeLensEnabled = true;

} // namespace

void SetLspServerCommand(const std::string& language, std::vector<std::string> argv) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (argv.empty()) {
        g_commands.erase(language);
    }
    else {
        g_commands[language] = std::move(argv);
    }
}

std::optional<std::vector<std::string>> LspServerCommand(const std::string& language) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto                        it = g_commands.find(language);
    if (it == g_commands.end()) {
        return std::nullopt;
    }
    return it->second;
}

void SetLspAutoCompleteEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_autoCompleteMutex);
    g_autoCompleteEnabled = enabled;
}

bool LspAutoCompleteEnabled() {
    const std::lock_guard<std::mutex> lock(g_autoCompleteMutex);
    return g_autoCompleteEnabled;
}

void SetLspCompletionDebounceMs(int milliseconds) {
    const std::lock_guard<std::mutex> lock(g_debounceMutex);
    g_completionDebounceMs = (milliseconds > 0) ? milliseconds : 1;
}

int LspCompletionDebounceMs() {
    const std::lock_guard<std::mutex> lock(g_debounceMutex);
    return g_completionDebounceMs;
}

void SetLspDiagnosticsDebounceMs(int milliseconds) {
    const std::lock_guard<std::mutex> lock(g_diagnosticsDebounceMutex);
    g_diagnosticsDebounceMs = (milliseconds > 0) ? milliseconds : 1;
}

int LspDiagnosticsDebounceMs() {
    const std::lock_guard<std::mutex> lock(g_diagnosticsDebounceMutex);
    return g_diagnosticsDebounceMs;
}

void SetLspSyncDebounceMs(int milliseconds) {
    const std::lock_guard<std::mutex> lock(g_syncDebounceMutex);
    g_syncDebounceMs = (milliseconds > 0) ? milliseconds : 1;
}

int LspSyncDebounceMs() {
    const std::lock_guard<std::mutex> lock(g_syncDebounceMutex);
    return g_syncDebounceMs;
}

void SetLspSignatureHelpAutoTriggerEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_signatureHelpAutoTriggerMutex);
    g_signatureHelpAutoTriggerEnabled = enabled;
}

bool LspSignatureHelpAutoTriggerEnabled() {
    const std::lock_guard<std::mutex> lock(g_signatureHelpAutoTriggerMutex);
    return g_signatureHelpAutoTriggerEnabled;
}

void SetLspFormatOnSaveEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_formatOnSaveMutex);
    g_formatOnSaveEnabled = enabled;
}

bool LspFormatOnSaveEnabled() {
    const std::lock_guard<std::mutex> lock(g_formatOnSaveMutex);
    return g_formatOnSaveEnabled;
}

void SetLspOnTypeFormattingEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_onTypeFormattingMutex);
    g_onTypeFormattingEnabled = enabled;
}

bool LspOnTypeFormattingEnabled() {
    const std::lock_guard<std::mutex> lock(g_onTypeFormattingMutex);
    return g_onTypeFormattingEnabled;
}

void SetLspPullDiagnosticsEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_pullDiagnosticsMutex);
    g_pullDiagnosticsEnabled = enabled;
}

bool LspPullDiagnosticsEnabled() {
    const std::lock_guard<std::mutex> lock(g_pullDiagnosticsMutex);
    return g_pullDiagnosticsEnabled;
}

void SetLspSemanticHighlightingEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_semanticHighlightingMutex);
    g_semanticHighlightingEnabled = enabled;
}

bool LspSemanticHighlightingEnabled() {
    const std::lock_guard<std::mutex> lock(g_semanticHighlightingMutex);
    return g_semanticHighlightingEnabled;
}

void SetLspInlayHintsEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_inlayHintsMutex);
    g_inlayHintsEnabled = enabled;
}

bool LspInlayHintsEnabled() {
    const std::lock_guard<std::mutex> lock(g_inlayHintsMutex);
    return g_inlayHintsEnabled;
}

void SetLspCodeLensEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_codeLensMutex);
    g_codeLensEnabled = enabled;
}

bool LspCodeLensEnabled() {
    const std::lock_guard<std::mutex> lock(g_codeLensMutex);
    return g_codeLensEnabled;
}

} // namespace ned::editor::lsp
