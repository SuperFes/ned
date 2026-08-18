#include "ProjectAgenda.h"

#include <fstream>
#include <sstream>

#include "ProjectTree.h"

namespace ned::editor {

namespace {

    std::string ReadFileOrEmpty(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

    std::string FormatHeadlineSummary(const org::Headline& headline) {
        std::string summary = headline.todoKeyword;
        if (headline.priority) {
            summary += " [#";
            summary += *headline.priority;
            summary += ']';
        }
        summary += ' ';
        summary += headline.title;
        if (!headline.tags.empty()) {
            summary += " :";
            for (const std::string& tag : headline.tags) {
                summary += tag;
                summary += ':';
            }
        }
        return summary;
    }

} // namespace

std::vector<SearchMatch> CollectProjectTodos(const std::filesystem::path& root, const std::vector<std::string>& todoKeywords) {
    std::vector<SearchMatch> todos;
    if (todoKeywords.empty()) {
        return todos; // nothing can ever be "the done state" -- nothing to collect
    }
    const std::string& doneKeyword = todoKeywords.back();

    for (const ProjectTreeEntry& entry : BuildProjectTree(root)) {
        if (entry.isDirectory || entry.path.extension() != ".org") {
            continue;
        }

        const std::string fileText = ReadFileOrEmpty(entry.path);
        for (const org::Headline& headline : org::ParseOutline(fileText, todoKeywords)) {
            if (headline.todoKeyword.empty() || headline.todoKeyword == doneKeyword) {
                continue; // no keyword at all, or the configured "done" state
            }
            todos.push_back(SearchMatch{
                .file       = entry.path,
                .lineNumber = headline.lineNumber + 1, // SearchMatch's own convention is 1-indexed
                .lineText   = FormatHeadlineSummary(headline),
            });
        }
    }
    return todos;
}

} // namespace ned::editor
