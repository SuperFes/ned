//
// ACP ContentBlocks other than plain text, as the transcript shows them:
// an agent's image, audio, resource link or embedded resource becomes an
// entry of its own; a replayed prompt's attachments become the same
// "[attached: ...]" names a live prompt shows.
//

#ifndef NED_EDITOR_ACP_CONTENTBLOCKS_H
#define NED_EDITOR_ACP_CONTENTBLOCKS_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "Editor/Acp/Manager.h"

namespace ned::editor::acp {

// Bytes that `base64` decodes to.
[[nodiscard]] std::uint64_t Base64DecodedSize(std::string_view base64);

// "84 KB"-style.
[[nodiscard]] std::string FormatByteSize(std::uint64_t bytes);

// A Kind::AgentContent entry for a non-text block; nullopt for text or a
// block it can't make sense of.
[[nodiscard]] std::optional<Manager::TranscriptEntry> AgentContentEntry(const Json& block);

// What a prompt's non-text block is called in "[attached: ...]"; empty for
// text and for a resource link, which the prompt's own text already names.
[[nodiscard]] std::string AttachmentName(const Json& block);

// Adds `name` to the "[attached: ...]" marker that ends `text`, starting
// one if there isn't one.
void AppendAttachmentName(std::string& text, const std::string& name);

// The path a file:// uri names, percent-decoded; nullopt for any other uri.
[[nodiscard]] std::optional<std::string> FileUriPath(std::string_view uri);

// The text blocks of `blocks` (an array or a single block), joined.
[[nodiscard]] std::string ContentText(const Json& blocks);

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_CONTENTBLOCKS_H
