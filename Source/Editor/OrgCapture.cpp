#include "Editor/OrgCapture.h"

#include <map>
#include <mutex>
#include <utility>

#include "Editor/Org.h"

namespace ned::editor::org {

namespace {

    std::mutex& RegistryMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::map<char, CaptureTemplate>& Registry() {
        static std::map<char, CaptureTemplate> registry;
        return registry;
    }

} // namespace

void RegisterCaptureTemplate(CaptureTemplate tmpl) {
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    const char                        key = tmpl.key;
    Registry()[key]                       = std::move(tmpl);
}

std::vector<CaptureTemplate> CaptureTemplates() {
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    std::vector<CaptureTemplate>      templates;
    templates.reserve(Registry().size());
    for (const auto& [key, tmpl] : Registry())
        templates.push_back(tmpl);
    return templates;
}

std::optional<CaptureTemplate> CaptureTemplateForKey(char key) {
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    const auto                        it = Registry().find(key);
    if (it == Registry().end())
        return std::nullopt;
    return it->second;
}

CaptureExpansion ExpandCaptureTemplate(const std::string& templateText) {
    const std::size_t pos = templateText.find("%?");
    if (pos == std::string::npos)
        return CaptureExpansion{templateText, std::nullopt};
    std::string text = templateText.substr(0, pos) + templateText.substr(pos + 2);
    return CaptureExpansion{std::move(text), pos};
}

CaptureResult InsertCapture(text::Buffer& target, const CaptureTemplate& tmpl) {
    const std::string bufferText    = target.Text();
    std::size_t       insertPoint   = bufferText.size();
    bool              headlineFound = true;

    if (!tmpl.headline.empty()) {
        const auto  headlines  = ParseOutline(bufferText);
        std::size_t matchIndex = headlines.size();
        for (std::size_t i = 0; i < headlines.size(); ++i) {
            if (headlines[i].title == tmpl.headline) {
                matchIndex = i;
                break;
            }
        }
        if (matchIndex < headlines.size()) {
            const std::size_t totalLines = target.Content().LineCount();
            const std::size_t endLine    = SubtreeEndLine(headlines, matchIndex, totalLines);
            // LineToByteOffset already clamps a one-past-the-end line index
            // to the buffer's own byte length, so this needs no separate
            // "did SubtreeEndLine hand back totalLines itself" check.
            insertPoint = target.Content().LineToByteOffset(endLine);
        }
        else {
            headlineFound = false;
        }
    }

    const CaptureExpansion expansion = ExpandCaptureTemplate(tmpl.templateText);

    // Land the inserted block on its own fresh line, and make sure it ends
    // with one too -- same "ensure a fresh line" precedent SetProperty's own
    // no-existing-drawer branch establishes, so filing under a headline that
    // already has children (or into a non-empty file) never runs the new
    // text onto an existing line.
    std::string body      = expansion.text;
    std::size_t prefixLen = 0;
    if (insertPoint > 0 && bufferText[insertPoint - 1] != '\n') {
        body      = "\n" + body;
        prefixLen = 1;
    }
    if (body.empty() || body.back() != '\n')
        body += '\n';

    target.InsertAt(insertPoint, body);

    const std::size_t cursorInBody   = expansion.cursorOffset.value_or(expansion.text.size());
    const std::size_t absoluteCursor = insertPoint + prefixLen + cursorInBody;

    return CaptureResult{absoluteCursor, headlineFound};
}

} // namespace ned::editor::org
