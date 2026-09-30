#include "FileUri.h"

namespace ned::text {

namespace {

    constexpr std::string_view kFileScheme = "file://";

    int HexValue(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }

    bool KeepsLiteral(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
    }

} // namespace

std::string PathToFileUri(const std::filesystem::path& path) {
    static constexpr char kHex[] = "0123456789ABCDEF";

    const std::string raw = path.string();
    std::string       uri(kFileScheme);
    uri.reserve(uri.size() + raw.size());
    for (const char c : raw) {
        if (KeepsLiteral(c)) {
            uri += c;
            continue;
        }
        const auto byte = static_cast<unsigned char>(c);
        uri += '%';
        uri += kHex[byte >> 4];
        uri += kHex[byte & 0x0F];
    }
    return uri;
}

std::optional<std::filesystem::path> FileUriToPath(std::string_view uri) {
    if (!uri.starts_with(kFileScheme)) {
        return std::nullopt;
    }
    uri.remove_prefix(kFileScheme.size());
    std::string decoded;
    decoded.reserve(uri.size());
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size() && HexValue(uri[i + 1]) >= 0 && HexValue(uri[i + 2]) >= 0) {
            decoded += static_cast<char>(HexValue(uri[i + 1]) * 16 + HexValue(uri[i + 2]));
            i += 2;
            continue;
        }
        decoded += uri[i];
    }
    return std::filesystem::path(decoded);
}

} // namespace ned::text
