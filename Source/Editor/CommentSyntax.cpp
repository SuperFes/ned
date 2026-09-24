#include "CommentSyntax.h"

#include <optional>

#include "LanguageRegistry.h"

namespace ned::editor {

namespace {

    CommentSyntax SyntaxOf(const std::string& lineComment, const std::string& blockOpen, const std::string& blockClose,
                           std::string language) {
        if (!lineComment.empty()) {
            return CommentSyntax{.prefix = lineComment, .suffix = {}, .language = std::move(language)};
        }
        return CommentSyntax{.prefix = blockOpen, .suffix = blockClose, .language = std::move(language)};
    }

} // namespace

CommentSyntax CommentSyntaxAt(const Mode& host, std::string_view text, std::size_t position) {
    const CommentSyntax hostSyntax = SyntaxOf(host.lineCommentPrefix, host.blockCommentOpen, host.blockCommentClose, {});
    if (!host.injectedRegions) {
        return hostSyntax;
    }
    std::optional<InjectionRegion>    innermost;
    std::optional<LanguageDefinition> innermostDefinition;
    for (const InjectionRegion& region : host.injectedRegions(text)) {
        if (position < region.startByte || position >= region.endByte) {
            continue;
        }
        if (innermost && region.endByte - region.startByte >= innermost->endByte - innermost->startByte) {
            continue;
        }
        std::optional<LanguageDefinition> definition = FindLanguageDefinition(region.language);
        if (!definition || (definition->extensions.empty() && definition->filenames.empty())) {
            continue; // a highlighting-only layer (markdown-inline), not a language someone writes
        }
        innermost           = region;
        innermostDefinition = std::move(definition);
    }
    if (!innermostDefinition) {
        return hostSyntax;
    }
    return SyntaxOf(innermostDefinition->lineCommentPrefix, innermostDefinition->blockCommentOpen,
                    innermostDefinition->blockCommentClose, innermostDefinition->name);
}

} // namespace ned::editor
