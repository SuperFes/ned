#include "Completion.h"

namespace ned::editor {

int CompletionSourceRank(CompletionSource source) {
    switch (source) {
        case CompletionSource::Snippet:
            return 0;
        case CompletionSource::Lsp:
            return 1;
        case CompletionSource::JanetBinding:
            return 2;
        case CompletionSource::BufferWord:
            return 3;
    }
    return 4;
}

std::string_view CompletionSourceLabel(CompletionSource source) {
    switch (source) {
        case CompletionSource::Lsp:
            return {};
        case CompletionSource::Snippet:
            return "snippet";
        case CompletionSource::JanetBinding:
            return "janet";
        case CompletionSource::BufferWord:
            return "buffer";
    }
    return {};
}

Completion FromLspItem(lsp::CompletionItem item) {
    return Completion{
        .label            = std::move(item.label),
        .filterText       = std::move(item.filterText),
        .sortText         = std::move(item.sortText),
        .insertText       = std::move(item.insertText),
        .isSnippet        = item.isSnippet,
        .source           = CompletionSource::Lsp,
        .kind             = item.kind,
        .detail           = std::move(item.detail),
        .documentation    = std::move(item.documentation),
        .replaceEdit      = std::move(item.textEdit),
        .additionalEdits  = std::move(item.additionalTextEdits),
        .commitCharacters = std::move(item.commitCharacters),
        .preselect        = item.preselect,
        .raw              = std::move(item.raw),
    };
}

std::vector<Completion> FromLspItems(std::vector<lsp::CompletionItem> items) {
    std::vector<Completion> completions;
    completions.reserve(items.size());
    for (lsp::CompletionItem& item : items) {
        completions.push_back(FromLspItem(std::move(item)));
    }
    return completions;
}

lsp::CompletionItem ToLspItem(const Completion& completion) {
    return lsp::CompletionItem{
        .label               = completion.label,
        .insertText          = completion.insertText,
        .isSnippet           = completion.isSnippet,
        .kind                = completion.kind,
        .detail              = completion.detail,
        .documentation       = completion.documentation,
        .textEdit            = completion.replaceEdit,
        .sortText            = completion.sortText,
        .filterText          = completion.filterText,
        .raw                 = completion.raw,
        .additionalTextEdits = completion.additionalEdits,
        .preselect           = completion.preselect,
        .commitCharacters    = completion.commitCharacters,
    };
}

} // namespace ned::editor
