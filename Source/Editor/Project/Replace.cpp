#include "Replace.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "Editor/RegexPattern.h"
#include "Text/FilePreservation.h"
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

ProjectReplace::ProjectReplace(std::filesystem::path root, text::BufferList* liveBuffers) : root_(std::move(root)), liveBuffers_(liveBuffers) {
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
    // live-buffer-search follow-up: previews what the user is actually
    // looking at, unsaved edits included, when a buffer list is wired up.
    matches_ = liveBuffers_ ? SearchDirectory(root_, patternText_, *liveBuffers_)
                            : SearchDirectory(root_, patternText_); // throws SearchPatternError on invalid syntax
    stage_   = Stage::EnteringReplacement;
}

void ProjectReplace::ConfirmReplacement() {
    if (stage_ != Stage::EnteringReplacement) {
        return;
    }
    stage_ = matches_.empty() ? Stage::Done : Stage::Confirming;
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

const std::string& ProjectReplace::PatternText() const {
    return patternText_;
}

const std::string& ProjectReplace::ReplacementText() const {
    return replacementText_;
}

const std::vector<SearchMatch>& ProjectReplace::Matches() const {
    return matches_;
}

} // namespace ned::editor
