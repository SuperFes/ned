#include "IssueBuffer.h"

#include "Editor/ModeOverrides.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor::tracker {

namespace {

    void AppendField(std::string& out, const char* label, const std::string& value) {
        if (!value.empty()) {
            out += std::string("- **") + label + ":** " + value + "\n";
        }
    }

    std::string JoinLabels(const std::vector<std::string>& labels) {
        std::string joined;
        for (const std::string& label : labels) {
            joined += (joined.empty() ? "" : ", ") + label;
        }
        return joined;
    }

    // GitHub bodies arrive with CRLF line endings, and trailing newlines vary
    // by tracker; the renderer owns the spacing.
    std::string NormalizeText(const std::string& text) {
        std::string normalized;
        normalized.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '\r' || i + 1 >= text.size() || text[i + 1] != '\n') {
                normalized += text[i];
            }
        }
        while (!normalized.empty() && (normalized.back() == '\n' || normalized.back() == '\r')) {
            normalized.pop_back();
        }
        return normalized;
    }

} // namespace

std::string IssueBufferName(const std::string& key) {
    return "*issue " + key + "*";
}

std::string RenderIssue(const IssueDetail& detail) {
    const Issue& issue = detail.issue;
    std::string  out   = "# " + issue.key + (issue.title.empty() ? "" : ": " + issue.title) + "\n\n";
    AppendField(out, "Status", issue.status);
    AppendField(out, "Assignee", issue.assignee);
    AppendField(out, "Labels", JoinLabels(issue.labels));
    AppendField(out, "Updated", issue.updated);
    AppendField(out, "URL", issue.url.empty() ? "" : "<" + issue.url + ">");

    const std::string body = NormalizeText(detail.body);
    out += "\n" + (body.empty() ? std::string("_No description._") : body) + "\n";

    if (!detail.comments.empty()) {
        out += "\n## Comments (" + std::to_string(detail.comments.size()) + ")\n";
        for (const Comment& comment : detail.comments) {
            std::string heading = comment.author.empty() ? "(unknown)" : comment.author;
            if (!comment.created.empty()) {
                heading += " — " + comment.created;
            }
            out += "\n### " + heading + "\n\n" + NormalizeText(comment.body) + "\n";
        }
    }
    return out;
}

text::Buffer& ShowIssue(text::BufferList& bufferList, const IssueDetail& detail) {
    const std::string name   = IssueBufferName(detail.issue.key);
    text::Buffer*     buffer = bufferList.Find(name);
    if (buffer == nullptr) {
        buffer = &bufferList.CreateBuffer(name);
        SetChosenModeForBuffer(*buffer, "markdown-mode");
    }
    buffer->SetReadOnly(false);
    buffer->BeginUndoGroup();
    if (buffer->Size() > 0) {
        buffer->DeleteRange(0, buffer->Size());
    }
    buffer->InsertAtPoint(RenderIssue(detail));
    buffer->SetPoint(0);
    buffer->EndUndoGroup();
    buffer->SetReadOnly(true);
    return *buffer;
}

} // namespace ned::editor::tracker
