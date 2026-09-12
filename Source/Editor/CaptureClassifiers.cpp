#include "CaptureClassifiers.h"

#include <map>
#include <mutex>
#include <utility>

namespace ned::editor {

namespace {

    std::mutex g_mutex;
    // (language key, capture name) -> classifier; std::map so a per-language
    // presence check is a lower_bound rather than a scan.
    std::map<std::pair<std::string, std::string>, CaptureClassifier> g_classifiers;

} // namespace

void RegisterCaptureClassifier(const std::string& languageKey, const std::string& captureName,
                               CaptureClassifier classifier) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (!classifier) {
        g_classifiers.erase({languageKey, captureName});
        return;
    }
    g_classifiers.insert_or_assign({languageKey, captureName}, std::move(classifier));
}

CaptureClassifier FindCaptureClassifier(std::string_view languageKey, std::string_view captureName) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto                        it = g_classifiers.find({std::string(languageKey), std::string(captureName)});
    return it != g_classifiers.end() ? it->second : CaptureClassifier{};
}

bool HasCaptureClassifiers(std::string_view languageKey) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto                        it = g_classifiers.lower_bound({std::string(languageKey), std::string()});
    return it != g_classifiers.end() && it->first.first == languageKey;
}

void ClearCaptureClassifiers() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_classifiers.clear();
}

} // namespace ned::editor
