#include "BinaryDetect.h"

#include <array>
#include <fstream>
#include <string>

namespace ned::text {

namespace {

    constexpr std::size_t kHeadBytes = 8192;

    std::optional<std::string> ReadHead(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return std::nullopt;
        }
        std::string head(kHeadBytes, '\0');
        file.read(head.data(), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<std::size_t>(file.gcount()));
        return head;
    }

} // namespace

bool HeadLooksBinary(std::string_view head, Charset charset) {
    if (head.find('\0') == std::string_view::npos) {
        return false;
    }
    if (charset != Charset::Utf16Le && charset != Charset::Utf16Be) {
        return true;
    }
    // The head may end partway through a unit or pair; only what decodes
    // before that is judged.
    CharsetDecoder decoder(charset);
    std::string    text;
    if (!decoder.Feed(head.substr(PreambleLength(head, charset)), text)) {
        return true;
    }
    return text.find('\0') != std::string::npos;
}

bool LooksBinary(const std::filesystem::path& path) {
    const std::optional<std::string> head = ReadHead(path);
    return !head || HeadLooksBinary(*head, SniffCharset(*head));
}

bool LooksBinaryToLoad(const std::filesystem::path& path, std::optional<Charset> chosen) {
    const std::optional<std::string> head = ReadHead(path);
    if (!head) {
        return true;
    }
    // Resolving can read .editorconfig files, so only a head that could be
    // binary pays for it.
    if (head->find('\0') == std::string::npos) {
        return false;
    }
    return HeadLooksBinary(*head, ResolveLoadCharset(path, *head, chosen));
}

} // namespace ned::text
