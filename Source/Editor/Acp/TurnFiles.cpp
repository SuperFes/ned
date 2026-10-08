#include "TurnFiles.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <variant>

#include "Editor/Backup.h"
#include "Editor/FormatEdit.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/FilePreservation.h"

namespace ned::editor::acp {

namespace {

    // Only the differing middle is replaced, so point and marks outside it
    // stay put.
    void ReplaceText(text::Buffer& buffer, const std::string& text) {
        const std::string current = buffer.Text();
        std::size_t       prefix  = 0;
        while (prefix < current.size() && prefix < text.size() && current[prefix] == text[prefix]) {
            ++prefix;
        }
        std::size_t suffix = 0;
        while (suffix < current.size() - prefix && suffix < text.size() - prefix &&
               current[current.size() - 1 - suffix] == text[text.size() - 1 - suffix]) {
            ++suffix;
        }
        ApplyFormatTextEdits(buffer, {FormatTextEdit{.start = prefix, .end = current.size() - suffix, .text = text.substr(prefix, text.size() - prefix - suffix)}});
    }

} // namespace

std::optional<FileState> ReadTurnFile(const text::BufferList& bufferList, const text::FileRef& file) {
    FileState state;
    if (const text::Buffer* buffer = bufferList.FindFile(file)) {
        if (buffer->IsLoading() || buffer->Content().ByteLength() > kMaxTurnFileBytes) {
            return std::nullopt;
        }
        state = FileState{.exists = true, .text = buffer->Text()};
    }
    else if (std::holds_alternative<text::RemoteFile>(file)) {
        return std::nullopt;
    }
    else {
        const std::filesystem::path& path = std::get<std::filesystem::path>(file);
        std::error_code              ec;
        if (!std::filesystem::exists(path, ec)) {
            return FileState{};
        }
        if (!std::filesystem::is_regular_file(path, ec) || std::filesystem::file_size(path, ec) > kMaxTurnFileBytes || ec) {
            return std::nullopt;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return std::nullopt;
        }
        std::ostringstream content;
        content << input.rdbuf();
        state = FileState{.exists = true, .text = content.str()};
    }
    if (state.text.find('\0') != std::string::npos) {
        return std::nullopt; // binary
    }
    return state;
}

void WriteTurnFile(text::BufferList& bufferList, const text::FileRef& file, const FileState& state) {
    text::Buffer* buffer = bufferList.FindFile(file);
    if (buffer && buffer->IsLoading()) {
        throw std::runtime_error(text::PathOf(file).filename().string() + " is still loading");
    }
    if (const auto* remote = std::get_if<text::RemoteFile>(&file)) {
        if (!buffer) {
            throw std::runtime_error(text::RemoteBufferName(*remote) + " isn't open here");
        }
        if (!state.exists) {
            throw std::runtime_error("can't delete " + text::RemoteBufferName(*remote) + " on its host");
        }
        ReplaceText(*buffer, state.text);
        return;
    }
    const std::filesystem::path& path = std::get<std::filesystem::path>(file);
    if (!state.exists) {
        if (buffer && buffer->Modified()) {
            throw std::runtime_error(path.filename().string() + " has unsaved changes");
        }
        BackupFileBeforeSave(path);
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec) {
            throw std::runtime_error("cannot delete " + path.string() + ": " + ec.message());
        }
        return;
    }
    if (buffer && buffer->Modified()) {
        ReplaceText(*buffer, state.text);
        return;
    }
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        BackupFileBeforeSave(path);
    }
    else if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    WriteFileAtomically(path, state.text);
    if (buffer) {
        buffer->Revert();
    }
}

void WriteFileAtomically(const std::filesystem::path& path, const std::string& content) {
    const std::filesystem::path         target     = text::ResolveSaveTarget(path);
    const text::PreservedFileAttributes attributes = text::CaptureFileAttributes(target);

    if (text::ShouldWriteInPlace(attributes)) {
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot open " + target.string() + " for writing");
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!output) {
            throw std::runtime_error("write failed for " + target.string());
        }
        return;
    }

    std::filesystem::path tempPath = target;
    tempPath += ".ned-tmp";
    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot open " + tempPath.string() + " for writing");
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!output) {
            throw std::runtime_error("write failed for " + tempPath.string());
        }
    }

    text::ApplyFileAttributes(tempPath, attributes);

    std::error_code ec;
    std::filesystem::rename(tempPath, target, ec);
    if (ec) {
        throw std::runtime_error("rename failed for " + target.string() + ": " + ec.message());
    }
}

} // namespace ned::editor::acp
