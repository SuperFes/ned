#include "ProjectReplace.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

#include "Text/Utf8.h"

namespace ned::editor {

namespace {

    std::size_t CountUniqueFiles(const std::vector<SearchMatch>& matches) {
        std::vector<std::filesystem::path> files;
        for (const SearchMatch& match : matches) {
            if (std::find(files.begin(), files.end(), match.file) == files.end()) {
                files.push_back(match.file);
            }
        }
        return files.size();
    }

} // namespace

ProjectReplace::ProjectReplace(std::filesystem::path root) : root_(std::move(root)) {
}

void ProjectReplace::AppendChar(char32_t codepoint) {
    if (stage_ == Stage::EnteringPattern) {
        patternText_ += text::EncodeCodepointUtf8(codepoint);
    }
    else if (stage_ == Stage::EnteringReplacement) {
        replacementText_ += text::EncodeCodepointUtf8(codepoint);
    }
}

void ProjectReplace::DeleteChar() {
    if (stage_ == Stage::EnteringPattern) {
        text::RemoveLastCodepoint(patternText_);
    }
    else if (stage_ == Stage::EnteringReplacement) {
        text::RemoveLastCodepoint(replacementText_);
    }
}

void ProjectReplace::ConfirmPattern() {
    if (stage_ != Stage::EnteringPattern || patternText_.empty()) {
        return;
    }
    matches_ = SearchDirectory(root_, patternText_); // throws std::regex_error on invalid syntax
    stage_   = Stage::EnteringReplacement;
}

void ProjectReplace::ConfirmReplacement() {
    if (stage_ != Stage::EnteringReplacement) {
        return;
    }
    stage_ = matches_.empty() ? Stage::Done : Stage::Confirming;
}

ReplaceSummary ProjectReplace::Confirm() {
    if (stage_ != Stage::Confirming) {
        return {};
    }
    stage_ = Stage::Done;
    return ReplaceMatches(matches_, patternText_, replacementText_);
}

void ProjectReplace::Cancel() {
    stage_ = Stage::Done;
}

ProjectReplace::Stage ProjectReplace::CurrentStage() const {
    return stage_;
}

std::string ProjectReplace::StatusText() const {
    switch (stage_) {
        case Stage::EnteringPattern:
            return "Project replace regex: " + patternText_;
        case Stage::EnteringReplacement:
            return "Replace \"" + patternText_ + "\" with: " + replacementText_;
        case Stage::Confirming: {
            const std::size_t fileCount = CountUniqueFiles(matches_);
            return "Replace matches on " + std::to_string(matches_.size()) + " line" +
                   (matches_.size() == 1 ? "" : "s") + " across " + std::to_string(fileCount) + " file" +
                   (fileCount == 1 ? "" : "s") + " with \"" + replacementText_ + "\"? (y/n)";
        }
        case Stage::Done:
            return matches_.empty() ? "No matches for \"" + patternText_ + "\"" : "";
    }
    return "";
}

const std::vector<SearchMatch>& ProjectReplace::Matches() const {
    return matches_;
}

ReplaceSummary ReplaceMatches(const std::vector<SearchMatch>& matches, const std::string& pattern,
                              const std::string& replacement) {
    const std::regex regex(pattern); // throws std::regex_error on invalid syntax

    std::vector<std::filesystem::path> files;
    for (const SearchMatch& match : matches) {
        if (std::find(files.begin(), files.end(), match.file) == files.end()) {
            files.push_back(match.file);
        }
    }

    ReplaceSummary summary;
    for (const std::filesystem::path& file : files) {
        std::ifstream input(file, std::ios::binary);
        if (!input) {
            continue;
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        input.close();
        const std::string content = buffer.str();

        const auto occurrences =
            std::distance(std::sregex_iterator(content.begin(), content.end(), regex), std::sregex_iterator());
        if (occurrences == 0) {
            continue;
        }

        const std::string replaced = std::regex_replace(content, regex, replacement);

        std::filesystem::path tempPath = file;
        tempPath += ".ned-tmp";
        {
            std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
            if (!output) {
                continue;
            }
            output.write(replaced.data(), static_cast<std::streamsize>(replaced.size()));
            if (!output) {
                continue;
            }
        }

        std::error_code ec;
        std::filesystem::rename(tempPath, file, ec);
        if (ec) {
            continue; // leaves the .ned-tmp file behind -- rare, and better than losing the original
        }

        summary.filesChanged += 1;
        summary.replacementCount += static_cast<std::size_t>(occurrences);
    }

    return summary;
}

} // namespace ned::editor
