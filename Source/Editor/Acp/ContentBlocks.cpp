#include "ContentBlocks.h"

#include <cstdio>

#include "Text/FileUri.h"

namespace ned::editor::acp {

namespace {

    std::string Field(const Json& object, const char* key) {
        return object.is_object() && object.contains(key) && object[key].is_string() ? object[key].get<std::string>() : std::string();
    }

    // The last path segment of a uri, which is what a file-backed resource is
    // best known by.
    std::string UriName(const std::string& uri) {
        std::string path = uri;
        if (const std::size_t query = path.find_first_of("?#"); query != std::string::npos) {
            path.resize(query);
        }
        while (!path.empty() && path.back() == '/') {
            path.pop_back();
        }
        const std::size_t slash = path.rfind('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

} // namespace

std::uint64_t Base64DecodedSize(std::string_view base64) {
    std::uint64_t symbols = 0;
    for (const char c : base64) {
        if (c != '=' && c != '\n' && c != '\r' && c != ' ') {
            ++symbols;
        }
    }
    return symbols * 3 / 4;
}

std::string FormatByteSize(std::uint64_t bytes) {
    char buffer[32];
    if (bytes < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    }
    else if (bytes < 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%llu KB", static_cast<unsigned long long>((bytes + 512) / 1024));
    }
    else {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return buffer;
}

std::uint64_t NextImageId() {
    static std::uint64_t next = 0;
    return ++next;
}

std::optional<Manager::TranscriptImage> ImageFromBlock(const Json& block) {
    if (Field(block, "type") != "image") {
        return std::nullopt;
    }
    std::string data = Field(block, "data");
    if (data.empty()) {
        return std::nullopt;
    }
    return Manager::TranscriptImage{.id = NextImageId(), .mimeType = Field(block, "mimeType"), .data = std::move(data)};
}

std::optional<Manager::TranscriptEntry> AgentContentEntry(const Json& block) {
    using Entry            = Manager::TranscriptEntry;
    const std::string type = Field(block, "type");
    Entry             entry{.kind = Entry::Kind::AgentContent, .status = type};
    if (type == "image" || type == "audio") {
        entry.detail   = Field(block, "uri");
        entry.mimeType = Field(block, "mimeType");
        entry.byteSize = Base64DecodedSize(Field(block, "data"));
        if (entry.byteSize == 0 && entry.detail.empty()) {
            return std::nullopt;
        }
        entry.contentName = entry.detail.empty() ? std::string() : UriName(entry.detail);
        if (std::optional<Manager::TranscriptImage> image = ImageFromBlock(block)) {
            entry.images.push_back(std::move(*image));
        }
        return entry;
    }
    if (type == "resource_link") {
        entry.detail = Field(block, "uri");
        if (entry.detail.empty()) {
            return std::nullopt;
        }
        entry.contentName = Field(block, "title");
        if (entry.contentName.empty()) {
            entry.contentName = Field(block, "name");
        }
        if (entry.contentName.empty()) {
            entry.contentName = UriName(entry.detail);
        }
        entry.mimeType = Field(block, "mimeType");
        if (block.contains("size") && block["size"].is_number_integer() && block["size"].get<std::int64_t>() > 0) {
            entry.byteSize = static_cast<std::uint64_t>(block["size"].get<std::int64_t>());
        }
        return entry;
    }
    if (type == "resource" && block.contains("resource") && block["resource"].is_object()) {
        const Json& resource = block["resource"];
        entry.detail         = Field(resource, "uri");
        entry.contentName    = UriName(entry.detail);
        entry.mimeType       = Field(resource, "mimeType");
        if (resource.contains("text") && resource["text"].is_string()) {
            entry.text     = resource["text"].get<std::string>();
            entry.byteSize = entry.text.size();
        }
        else {
            entry.byteSize = Base64DecodedSize(Field(resource, "blob"));
        }
        return entry;
    }
    return std::nullopt;
}

std::string AttachmentName(const Json& block) {
    const std::string type = Field(block, "type");
    if (type == "image" || type == "audio") {
        const std::string uri = Field(block, "uri");
        return uri.empty() ? type : UriName(uri);
    }
    if (type == "resource" && block.contains("resource")) {
        const std::string name = UriName(Field(block["resource"], "uri"));
        return name.empty() ? std::string("resource") : name;
    }
    return {};
}

void AppendAttachmentName(std::string& text, const std::string& name) {
    static constexpr std::string_view kMarker = "\n\n[attached: ";
    const std::size_t                 marker  = text.rfind(kMarker);
    if (marker != std::string::npos && !text.empty() && text.back() == ']' && text.find('\n', marker + kMarker.size()) == std::string::npos) {
        text.insert(text.size() - 1, ", " + name);
        return;
    }
    text += std::string(kMarker) + name + "]";
}

std::optional<std::string> FileUriPath(std::string_view uri) {
    const std::optional<std::filesystem::path> path = text::FileUriToPath(uri);
    return path ? std::optional<std::string>(path->string()) : std::nullopt;
}

std::string ContentText(const Json& blocks) {
    if (blocks.is_object()) {
        return Field(blocks, "type") == "text" ? Field(blocks, "text") : std::string();
    }
    std::string text;
    for (const Json& block : blocks.is_array() ? blocks : Json::array()) {
        text += ContentText(block);
    }
    return text;
}

} // namespace ned::editor::acp
