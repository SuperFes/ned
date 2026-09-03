#include "LspContent.h"

#include "Text/Utf8.h"

namespace ned::editor::lsp {

namespace {

    // signature-help follow-up. Converts a UTF-16 code-unit offset into a
    // plain string (an LSP ParameterInformation.label's own [start, end)
    // tuple form) to a byte offset -- the same tolerant per-codepoint walk
    // LspPosition.cpp's BytePositionToLsp/LspPositionToByte do against a
    // Rope, just against a bare string here. A codepoint needs two UTF-16
    // units exactly when its UTF-8 encoding is 4 bytes long (every
    // codepoint above U+FFFF is encoded that way and no other codepoint
    // is), so NextCodepointBoundary's step size alone is enough -- no need
    // to actually decode the codepoint's value. Clamps to text.size() the
    // same way LspPositionToByte clamps to its line's end.
    std::size_t Utf16OffsetToByte(std::string_view text, std::size_t utf16Offset) {
        std::size_t byteOffset = 0;
        std::size_t utf16Count = 0;
        while (byteOffset < text.size() && utf16Count < utf16Offset) {
            const std::size_t next = text::NextCodepointBoundary(text, byteOffset);
            utf16Count += (next - byteOffset == 4) ? 2 : 1;
            byteOffset = next;
        }
        return byteOffset;
    }

    // signature-help follow-up. Resolves a ParameterInformation's own
    // "label" (either a substring of signatureLabel, or a [start, end)
    // UTF-16-offset pair into it) to a byte range within signatureLabel.
    // nullopt for a missing/malformed label, a substring that isn't
    // actually found, or a range that doesn't land inside the label.
    std::optional<std::pair<std::size_t, std::size_t>> ParameterLabelRange(const Json& parameter, std::string_view signatureLabel) {
        const auto labelIt = parameter.find("label");
        if (labelIt == parameter.end()) {
            return std::nullopt;
        }
        if (labelIt->is_string()) {
            const std::string needle = labelIt->get<std::string>();
            if (needle.empty()) {
                return std::nullopt;
            }
            const std::size_t pos = signatureLabel.find(needle);
            if (pos == std::string_view::npos) {
                return std::nullopt;
            }
            return std::make_pair(pos, pos + needle.size());
        }
        if (labelIt->is_array() && labelIt->size() == 2 && (*labelIt)[0].is_number() && (*labelIt)[1].is_number()) {
            const std::size_t start = Utf16OffsetToByte(signatureLabel, (*labelIt)[0].get<std::size_t>());
            const std::size_t end   = Utf16OffsetToByte(signatureLabel, (*labelIt)[1].get<std::size_t>());
            if (start <= end && end <= signatureLabel.size()) {
                return std::make_pair(start, end);
            }
        }
        return std::nullopt;
    }

    std::string TextFromHoverContentsEntry(const Json& entry) {
        if (entry.is_string()) {
            return entry.get<std::string>();
        }
        if (entry.is_object()) {
            return entry.value("value", std::string());
        }
        return {};
    }

    LspPosition PositionFromJson(const Json& position) {
        return LspPosition{
            .line      = position.value("line", static_cast<std::size_t>(0)),
            .character = position.value("character", static_cast<std::size_t>(0)),
        };
    }

    // Shared by ExtractWorkspaceEditChanges/ExtractFormattingEdits: one
    // "changes" map entry's (or a bare formatting response's) TextEdit[]
    // into WorkspaceTextEdits. An entry missing "range" is skipped, not
    // treated as a parse error, matching every other ExtractX function in
    // this file.
    std::vector<WorkspaceTextEdit> ParseTextEditArray(const Json& editArray) {
        std::vector<WorkspaceTextEdit> edits;
        for (const Json& textEdit : editArray) {
            if (!textEdit.is_object() || !textEdit.contains("range")) {
                continue;
            }
            const Json& range = textEdit["range"];
            edits.push_back(WorkspaceTextEdit{
                .start   = PositionFromJson(range.value("start", Json::object())),
                .end     = PositionFromJson(range.value("end", Json::object())),
                .newText = textEdit.value("newText", std::string()),
            });
        }
        return edits;
    }

    // project-undo follow-up: shared by ExtractSingleCodeAction and
    // ExtractRenameEdits -- parses a WorkspaceEdit's "changes" map into one
    // RenameEdit per named URI, however many that is. Sets
    // touchesUnsupportedForm and returns empty if the edit uses
    // "documentChanges" instead of "changes" at all (a real, more general
    // form -- file creation/rename/deletion, not just edits to existing
    // ones -- this v1 doesn't parse); the caller refuses the whole result
    // wholesale in that case rather than applying a partial fix. A URI
    // whose own edit array is empty after parsing is dropped rather than
    // kept as a no-op entry.
    std::vector<RenameEdit> ExtractWorkspaceEditChanges(const Json& edit, bool& touchesUnsupportedForm) {
        std::vector<RenameEdit> result;
        if (!edit.is_object()) {
            return result;
        }

        if (edit.contains("documentChanges")) {
            touchesUnsupportedForm = true;
            return result;
        }

        const auto changesIt = edit.find("changes");
        if (changesIt == edit.end() || !changesIt->is_object()) {
            return result; // no "changes" map at all -- an edit with nothing to apply
        }

        for (const auto& [uri, editArray] : changesIt->items()) {
            if (!editArray.is_array()) {
                continue;
            }
            std::vector<WorkspaceTextEdit> edits = ParseTextEditArray(editArray);
            if (!edits.empty()) {
                result.push_back(RenameEdit{.uri = uri, .edits = std::move(edits)});
            }
        }
        return result;
    }

    // symbol-search follow-up. Recurses a single DocumentSymbol object
    // (possibly with "children") into out, threading parentName down as
    // each child's own containerName -- the top-level call passes "" (a
    // root symbol has no container).
    void CollectDocumentSymbol(const Json& item, const std::string& ownUri, const std::string& parentName,
                               std::vector<SymbolEntry>& out) {
        if (!item.is_object()) {
            return;
        }
        const auto nameIt = item.find("name");
        if (nameIt == item.end() || !nameIt->is_string()) {
            return;
        }
        const std::string name = nameIt->get<std::string>();

        // selectionRange is the symbol's own identifier -- what a caret
        // should land on, the same "precise, not the whole enclosing
        // block" reasoning ExtractDefinitionLocations applies to
        // targetSelectionRange over targetRange.
        const Json& selectionRange = item.value("selectionRange", item.value("range", Json::object()));
        out.push_back(SymbolEntry{
            .name          = name,
            .containerName = parentName,
            .kind          = item.value("kind", 0),
            .uri           = ownUri,
            .position      = PositionFromJson(selectionRange.value("start", Json::object())),
        });

        if (const auto childrenIt = item.find("children"); childrenIt != item.end() && childrenIt->is_array()) {
            for (const Json& child : *childrenIt) {
                CollectDocumentSymbol(child, ownUri, name, out);
            }
        }
    }

    // symbol-search follow-up. Parses one SymbolInformation/WorkspaceSymbol
    // item -- both carry "name"/"kind"/"location"/optional "containerName"
    // at the top level, differing only in whether "location" is guaranteed
    // to carry a "range" (SymbolInformation always does; a WorkspaceSymbol
    // may send {uri} alone). Appends nothing for a malformed entry (missing
    // "name", or a "location" missing "uri" entirely).
    void CollectFlatSymbol(const Json& item, std::vector<SymbolEntry>& out) {
        if (!item.is_object()) {
            return;
        }
        const auto nameIt     = item.find("name");
        const auto locationIt = item.find("location");
        if (nameIt == item.end() || !nameIt->is_string() || locationIt == item.end() || !locationIt->is_object()) {
            return;
        }
        const auto uriIt = locationIt->find("uri");
        if (uriIt == locationIt->end() || !uriIt->is_string()) {
            return;
        }
        LspPosition position{}; // value-initialized to {0, 0} -- see this function's own doc comment on WorkspaceSymbol's optional range
        if (const auto rangeIt = locationIt->find("range"); rangeIt != locationIt->end() && rangeIt->is_object()) {
            position = PositionFromJson(rangeIt->value("start", Json::object()));
        }
        out.push_back(SymbolEntry{
            .name          = nameIt->get<std::string>(),
            .containerName = item.value("containerName", std::string()),
            .kind          = item.value("kind", 0),
            .uri           = uriIt->get<std::string>(),
            .position      = position,
        });
    }

} // namespace

std::string_view SymbolKindLabel(int kind) {
    // LSP SymbolKind, 1-26 per the spec (3.17) -- every currently defined
    // value gets its own word; nothing else in this codebase needs the
    // numeric form once a picker has this label, so no reverse mapping.
    switch (kind) {
        case 1:
            return "file";
        case 2:
            return "module";
        case 3:
            return "namespace";
        case 4:
            return "package";
        case 5:
            return "class";
        case 6:
            return "method";
        case 7:
            return "property";
        case 8:
            return "field";
        case 9:
            return "constructor";
        case 10:
            return "enum";
        case 11:
            return "interface";
        case 12:
            return "function";
        case 13:
            return "variable";
        case 14:
            return "constant";
        case 15:
            return "string";
        case 16:
            return "number";
        case 17:
            return "boolean";
        case 18:
            return "array";
        case 19:
            return "object";
        case 20:
            return "key";
        case 21:
            return "null";
        case 22:
            return "enum member";
        case 23:
            return "struct";
        case 24:
            return "event";
        case 25:
            return "operator";
        case 26:
            return "type parameter";
        default:
            return "symbol";
    }
}

std::vector<SymbolEntry> ExtractSymbols(const Json& result, const std::string& ownUri) {
    std::vector<SymbolEntry> entries;
    if (!result.is_array()) {
        return entries;
    }
    for (const Json& item : result) {
        if (!item.is_object()) {
            continue;
        }
        // DocumentSymbol has "selectionRange"; SymbolInformation/WorkspaceSymbol
        // have "location" instead -- the one field that tells the two wire
        // shapes apart (both otherwise carry "name"/"kind").
        if (item.contains("location")) {
            CollectFlatSymbol(item, entries);
        }
        else {
            CollectDocumentSymbol(item, ownUri, std::string(), entries);
        }
    }
    return entries;
}

std::optional<std::string> ExtractHoverText(const Json& result) {
    if (result.is_null() || !result.contains("contents")) {
        return std::nullopt;
    }
    const Json& contents = result["contents"];

    std::string text;
    if (contents.is_array()) {
        for (const Json& entry : contents) {
            if (!text.empty()) {
                text += "\n\n";
            }
            text += TextFromHoverContentsEntry(entry);
        }
    }
    else {
        text = TextFromHoverContentsEntry(contents);
    }

    if (text.empty()) {
        return std::nullopt;
    }
    return text;
}

std::vector<CompletionItem> ExtractCompletionItems(const Json& result) {
    std::vector<CompletionItem> items;
    if (result.is_null()) {
        return items;
    }

    const Json* rawItems = &result;
    if (result.is_object()) {
        static const Json kEmptyArray = Json::array();
        const auto        it          = result.find("items");
        rawItems                      = (it != result.end()) ? &*it : &kEmptyArray;
    }
    if (!rawItems->is_array()) {
        return items;
    }

    for (const Json& item : *rawItems) {
        if (!item.is_object() || !item.contains("label")) {
            continue;
        }
        std::string label      = item.value("label", std::string());
        std::string insertText = item.value("insertText", label);
        // Item-level insertTextFormat only (1 = PlainText is the spec's own
        // default); a completion list's itemDefaults.insertTextFormat is a
        // documented v1 cut.
        const bool  isSnippet = item.value("insertTextFormat", 1) == 2;
        const int   kind      = item.value("kind", 0);
        std::string detail    = item.value("detail", std::string());
        // completion-popup-preview follow-up: wraps the raw "documentation" value in
        // the same {"contents": ...} shape ExtractHoverText already expects, reusing
        // its string-or-MarkupContent extraction verbatim instead of duplicating it.
        std::string documentation;
        if (const auto docIt = item.find("documentation"); docIt != item.end()) {
            documentation = ExtractHoverText(Json{{"contents", *docIt}}).value_or(std::string());
        }
        items.push_back(CompletionItem{.label         = std::move(label),
                                       .insertText    = std::move(insertText),
                                       .isSnippet     = isSnippet,
                                       .kind          = kind,
                                       .detail        = std::move(detail),
                                       .documentation = std::move(documentation)});
    }
    return items;
}

CodeAction ExtractSingleCodeAction(const Json& item, const std::string& ownUri) {
    CodeAction action;
    if (!item.is_object()) {
        return action;
    }
    action.title       = item.value("title", std::string());
    action.raw         = item;
    action.kind        = item.value("kind", std::string());
    action.isPreferred = item.value("isPreferred", false);

    if (const auto editIt = item.find("edit"); editIt != item.end()) {
        action.hasEdit = true;
        action.edits   = ExtractWorkspaceEditChanges(*editIt, action.touchesUnsupportedForm);
    }
    else {
        // code-actions-resolve follow-up: "kind" is a real-CodeAction-only
        // field (a bare Command object never has one) -- an item shaped
        // like this but missing "edit" is a server deferring the edit
        // computation to a later codeAction/resolve request, not a
        // genuinely edit-less action.
        action.resolvable = item.contains("kind");
    }

    // executeCommand follow-up: a real CodeAction nests its Command as an
    // object under "command"; a bare Command response item instead *is*
    // that Command, with "command" as the (string) command name directly on
    // item itself -- is_object()/is_string() is exactly how those two shapes
    // differ on this shared key.
    if (const auto commandIt = item.find("command"); commandIt != item.end()) {
        if (commandIt->is_object()) {
            action.command = CodeAction::CodeActionCommand{
                .name      = commandIt->value("command", std::string()),
                .arguments = commandIt->value("arguments", Json::array()),
            };
        }
        else if (commandIt->is_string()) {
            action.command = CodeAction::CodeActionCommand{
                .name      = commandIt->get<std::string>(),
                .arguments = item.value("arguments", Json::array()),
            };
        }
    }
    return action;
}

std::vector<CodeAction> ExtractCodeActions(const Json& result, const std::string& ownUri) {
    std::vector<CodeAction> actions;
    if (!result.is_array()) {
        return actions;
    }

    for (const Json& item : result) {
        if (!item.is_object() || !item.contains("title")) {
            continue;
        }
        actions.push_back(ExtractSingleCodeAction(item, ownUri));
    }
    return actions;
}

std::vector<DefinitionLocation> ExtractDefinitionLocations(const Json& result) {
    std::vector<DefinitionLocation> locations;
    if (result.is_null()) {
        return locations;
    }

    // A bare Location is a single object; Location[]/LocationLink[] are
    // arrays -- normalize to one loop either way.
    std::vector<const Json*> items;
    if (result.is_array()) {
        for (const Json& item : result) {
            items.push_back(&item);
        }
    }
    else if (result.is_object()) {
        items.push_back(&result);
    }
    else {
        return locations;
    }

    for (const Json* itemPtr : items) {
        const Json& item = *itemPtr;
        if (!item.is_object()) {
            continue;
        }
        std::string uri;
        const Json* range = nullptr;
        if (const auto targetUriIt = item.find("targetUri"); targetUriIt != item.end()) {
            // LocationLink -- targetSelectionRange is the precise range of
            // the definition's own identifier, targetRange the whole
            // enclosing declaration; the former is what a caret should land
            // on, matching what a Location's plain "range" already means.
            uri = targetUriIt->get<std::string>();
            if (const auto rangeIt = item.find("targetSelectionRange"); rangeIt != item.end()) {
                range = &*rangeIt;
            }
        }
        else if (const auto uriIt = item.find("uri"); uriIt != item.end()) {
            uri = uriIt->get<std::string>();
            if (const auto rangeIt = item.find("range"); rangeIt != item.end()) {
                range = &*rangeIt;
            }
        }
        else {
            continue;
        }
        if (uri.empty() || range == nullptr || !range->is_object() || !range->contains("start")) {
            continue;
        }
        locations.push_back(DefinitionLocation{
            .uri      = std::move(uri),
            .position = PositionFromJson((*range)["start"]),
        });
    }
    return locations;
}

RenameResult ExtractRenameEdits(const Json& result) {
    RenameResult renameResult;
    renameResult.edits   = ExtractWorkspaceEditChanges(result, renameResult.touchesUnsupportedForm);
    renameResult.hasEdit = !renameResult.edits.empty();
    return renameResult;
}

std::vector<WorkspaceTextEdit> ExtractFormattingEdits(const Json& result) {
    if (!result.is_array()) {
        return {};
    }
    return ParseTextEditArray(result);
}

std::vector<DocumentHighlight> ExtractDocumentHighlights(const Json& result) {
    std::vector<DocumentHighlight> highlights;
    if (!result.is_array()) {
        return highlights;
    }
    for (const Json& item : result) {
        if (!item.is_object() || !item.contains("range")) {
            continue;
        }
        const Json& range = item["range"];
        if (!range.is_object() || !range.contains("start") || !range.contains("end")) {
            continue;
        }
        highlights.push_back(DocumentHighlight{
            .start = PositionFromJson(range["start"]),
            .end   = PositionFromJson(range["end"]),
            .kind  = item.value("kind", 1),
        });
    }
    return highlights;
}

std::optional<std::string> ExtractSignatureHelp(const Json& result) {
    if (!result.is_object()) {
        return std::nullopt;
    }
    const auto signaturesIt = result.find("signatures");
    if (signaturesIt == result.end() || !signaturesIt->is_array() || signaturesIt->empty()) {
        return std::nullopt;
    }
    const Json& signatures = *signaturesIt;

    std::size_t activeSignature = 0;
    if (const auto it = result.find("activeSignature"); it != result.end() && it->is_number()) {
        const std::size_t index = it->get<std::size_t>();
        if (index < signatures.size()) {
            activeSignature = index;
        }
    }

    const Json& signature = signatures[activeSignature];
    const auto  labelIt   = signature.find("label");
    if (labelIt == signature.end() || !labelIt->is_string()) {
        return std::nullopt;
    }
    std::string label = labelIt->get<std::string>();

    std::optional<std::size_t> activeParameter;
    if (const auto it = signature.find("activeParameter"); it != signature.end() && it->is_number()) {
        activeParameter = it->get<std::size_t>();
    }
    else if (const auto topIt = result.find("activeParameter"); topIt != result.end() && topIt->is_number()) {
        activeParameter = topIt->get<std::size_t>();
    }

    if (activeParameter) {
        const auto parametersIt = signature.find("parameters");
        if (parametersIt != signature.end() && parametersIt->is_array() && *activeParameter < parametersIt->size()) {
            if (const auto range = ParameterLabelRange((*parametersIt)[*activeParameter], label)) {
                label = label.substr(0, range->first) + "**" + label.substr(range->first, range->second - range->first) + "**" +
                        label.substr(range->second);
            }
        }
    }
    return label;
}

namespace {

[[nodiscard]] std::vector<std::string> StringArray(const Json& array) {
    std::vector<std::string> values;
    if (!array.is_array()) {
        return values;
    }
    values.reserve(array.size());
    for (const Json& entry : array) {
        if (entry.is_string()) {
            values.push_back(entry.get<std::string>());
        }
    }
    return values;
}

} // namespace

std::optional<SemanticTokensLegend> ExtractSemanticTokensLegend(const Json& initializeResult) {
    if (!initializeResult.is_object()) {
        return std::nullopt;
    }
    const auto capabilitiesIt = initializeResult.find("capabilities");
    if (capabilitiesIt == initializeResult.end() || !capabilitiesIt->is_object()) {
        return std::nullopt;
    }
    const auto providerIt = capabilitiesIt->find("semanticTokensProvider");
    if (providerIt == capabilitiesIt->end() || !providerIt->is_object()) {
        return std::nullopt;
    }
    const auto legendIt = providerIt->find("legend");
    if (legendIt == providerIt->end() || !legendIt->is_object()) {
        return std::nullopt;
    }
    const auto typesIt = legendIt->find("tokenTypes");
    if (typesIt == legendIt->end() || !typesIt->is_array()) {
        return std::nullopt;
    }
    SemanticTokensLegend legend;
    legend.tokenTypes = StringArray(*typesIt);
    if (const auto modifiersIt = legendIt->find("tokenModifiers"); modifiersIt != legendIt->end()) {
        legend.tokenModifiers = StringArray(*modifiersIt);
    }
    return legend;
}

std::optional<OnTypeFormattingTriggers> ExtractOnTypeFormattingTriggers(const Json& initializeResult) {
    if (!initializeResult.is_object()) {
        return std::nullopt;
    }
    const auto capabilitiesIt = initializeResult.find("capabilities");
    if (capabilitiesIt == initializeResult.end() || !capabilitiesIt->is_object()) {
        return std::nullopt;
    }
    const auto providerIt = capabilitiesIt->find("documentOnTypeFormattingProvider");
    if (providerIt == capabilitiesIt->end() || !providerIt->is_object()) {
        return std::nullopt;
    }
    const auto firstIt = providerIt->find("firstTriggerCharacter");
    if (firstIt == providerIt->end() || !firstIt->is_string()) {
        return std::nullopt;
    }
    OnTypeFormattingTriggers triggers;
    triggers.first = firstIt->get<std::string>();
    if (const auto moreIt = providerIt->find("moreTriggerCharacter"); moreIt != providerIt->end()) {
        triggers.more = StringArray(*moreIt);
    }
    return triggers;
}

std::optional<TextDocumentSyncKind> ExtractTextDocumentSyncKind(const Json& initializeResult) {
    if (!initializeResult.is_object()) {
        return std::nullopt;
    }
    const auto capabilitiesIt = initializeResult.find("capabilities");
    if (capabilitiesIt == initializeResult.end() || !capabilitiesIt->is_object()) {
        return std::nullopt;
    }
    const auto syncIt = capabilitiesIt->find("textDocumentSync");
    if (syncIt == capabilitiesIt->end()) {
        return std::nullopt;
    }
    std::optional<int> change;
    if (syncIt->is_number_integer()) {
        change = syncIt->get<int>(); // legacy bare-int form
    }
    else if (syncIt->is_object()) {
        const auto changeIt = syncIt->find("change");
        if (changeIt != syncIt->end() && changeIt->is_number_integer()) {
            change = changeIt->get<int>();
        }
    }
    if (!change || *change < 0 || *change > 2) {
        return std::nullopt;
    }
    return static_cast<TextDocumentSyncKind>(*change);
}

std::optional<std::vector<PullDiagnosticItem>> ExtractPullDiagnosticReport(const Json& result) {
    if (!result.is_object()) {
        return std::nullopt;
    }
    // Some servers omit "kind" entirely despite the spec listing it as
    // required -- treated the same as "full" (the only shape carrying an
    // "items" array to parse), matching every other ExtractX function's
    // tolerant-of-a-missing-field convention in this file.
    const std::string kind = result.value("kind", std::string("full"));
    if (kind == "unchanged") {
        return std::nullopt; // nothing new -- caller keeps its existing slice
    }
    if (kind != "full") {
        return std::nullopt; // an unrecognized report kind, not a parse error
    }
    const auto itemsIt = result.find("items");
    if (itemsIt == result.end() || !itemsIt->is_array()) {
        return std::nullopt;
    }
    std::vector<PullDiagnosticItem> items;
    items.reserve(itemsIt->size());
    for (const Json& item : *itemsIt) {
        const Json& range = item.value("range", Json::object());
        if (!range.contains("start") || !range.contains("end")) {
            continue;
        }
        items.push_back(PullDiagnosticItem{
            .start    = PositionFromJson(range["start"]),
            .end      = PositionFromJson(range["end"]),
            .severity = item.value("severity", 3),
            .message  = item.value("message", std::string()),
        });
    }
    return items;
}

std::vector<SemanticToken> ExtractSemanticTokens(const Json& result) {
    std::vector<SemanticToken> tokens;
    if (!result.is_object()) {
        return tokens;
    }
    const auto dataIt = result.find("data");
    if (dataIt == result.end() || !dataIt->is_array() || dataIt->size() % 5 != 0) {
        return tokens;
    }
    tokens.reserve(dataIt->size() / 5);
    std::size_t line      = 0;
    std::size_t character = 0;
    for (std::size_t i = 0; i < dataIt->size(); i += 5) {
        const std::size_t deltaLine      = (*dataIt)[i].get<std::size_t>();
        const std::size_t deltaStartChar = (*dataIt)[i + 1].get<std::size_t>();
        line += deltaLine;
        character = (deltaLine == 0) ? character + deltaStartChar : deltaStartChar;
        tokens.push_back(SemanticToken{
            .start          = LspPosition{.line = line, .character = character},
            .length         = (*dataIt)[i + 2].get<std::size_t>(),
            .tokenTypeIndex = (*dataIt)[i + 3].get<std::size_t>(),
            .tokenModifiers = (*dataIt)[i + 4].get<std::uint32_t>(),
        });
    }
    return tokens;
}

std::vector<InlayHint> ExtractInlayHints(const Json& result) {
    std::vector<InlayHint> hints;
    if (!result.is_array()) {
        return hints;
    }
    hints.reserve(result.size());
    for (const Json& item : result) {
        if (!item.is_object() || !item.contains("position") || !item.contains("label")) {
            continue;
        }
        std::string label;
        const Json& labelJson = item["label"];
        if (labelJson.is_string()) {
            label = labelJson.get<std::string>();
        }
        else if (labelJson.is_array()) {
            for (const Json& part : labelJson) {
                if (part.is_object() && part.value("value", std::string()).size() > 0) {
                    label += part["value"].get<std::string>();
                }
            }
        }
        if (label.empty()) {
            continue; // nothing to render either way
        }
        hints.push_back(InlayHint{.position = PositionFromJson(item["position"]), .label = std::move(label)});
    }
    return hints;
}

CodeLens ExtractSingleCodeLens(const Json& item) {
    CodeLens lens;
    lens.raw = item;
    const Json& range = item.value("range", Json::object());
    if (range.is_object() && range.contains("start") && range.contains("end")) {
        lens.start = PositionFromJson(range["start"]);
        lens.end   = PositionFromJson(range["end"]);
    }
    if (const auto commandIt = item.find("command"); commandIt != item.end() && commandIt->is_object()) {
        lens.title            = commandIt->value("title", std::string());
        lens.commandName      = commandIt->value("command", std::string());
        lens.commandArguments = commandIt->value("arguments", Json::array());
        lens.hasCommand       = !lens.commandName.empty();
    }
    return lens;
}

std::vector<CodeLens> ExtractCodeLenses(const Json& result) {
    std::vector<CodeLens> lenses;
    if (!result.is_array()) {
        return lenses;
    }
    lenses.reserve(result.size());
    for (const Json& item : result) {
        if (!item.is_object() || !item.contains("range")) {
            continue;
        }
        lenses.push_back(ExtractSingleCodeLens(item));
    }
    return lenses;
}

namespace {

    // call/type-hierarchy follow-up. Parses one CallHierarchyItem/
    // TypeHierarchyItem object -- nullopt for one missing "name", "uri", or
    // "selectionRange", matching every other ExtractX function's "skip a
    // malformed entry" convention.
    std::optional<HierarchyItem> HierarchyItemFromJson(const Json& item) {
        if (!item.is_object()) {
            return std::nullopt;
        }
        const auto nameIt = item.find("name");
        const auto uriIt  = item.find("uri");
        if (nameIt == item.end() || !nameIt->is_string() || uriIt == item.end() || !uriIt->is_string()) {
            return std::nullopt;
        }
        const auto selectionRangeIt = item.find("selectionRange");
        if (selectionRangeIt == item.end() || !selectionRangeIt->is_object() || !selectionRangeIt->contains("start")) {
            return std::nullopt;
        }
        return HierarchyItem{
            .name     = nameIt->get<std::string>(),
            .detail   = item.value("detail", std::string()),
            .kind     = item.value("kind", 0),
            .uri      = uriIt->get<std::string>(),
            .position = PositionFromJson((*selectionRangeIt)["start"]),
            .raw      = item,
        };
    }

    // Shared by ExtractIncomingCalls/ExtractOutgoingCalls -- itemField is
    // "from" or "to" per spec, everything else about the two response shapes
    // is identical.
    std::vector<HierarchyCall> ExtractHierarchyCalls(const Json& result, const char* itemField) {
        std::vector<HierarchyCall> calls;
        if (!result.is_array()) {
            return calls;
        }
        calls.reserve(result.size());
        for (const Json& entry : result) {
            if (!entry.is_object()) {
                continue;
            }
            const auto itemIt = entry.find(itemField);
            if (itemIt == entry.end()) {
                continue;
            }
            std::optional<HierarchyItem> item = HierarchyItemFromJson(*itemIt);
            if (!item) {
                continue;
            }
            HierarchyCall call{.item = std::move(*item)};
            if (const auto rangesIt = entry.find("fromRanges"); rangesIt != entry.end() && rangesIt->is_array()) {
                call.callSites.reserve(rangesIt->size());
                for (const Json& range : *rangesIt) {
                    if (range.is_object() && range.contains("start")) {
                        call.callSites.push_back(PositionFromJson(range["start"]));
                    }
                }
            }
            calls.push_back(std::move(call));
        }
        return calls;
    }

} // namespace

std::vector<HierarchyItem> ExtractHierarchyItems(const Json& result) {
    std::vector<HierarchyItem> items;
    if (!result.is_array()) {
        return items;
    }
    items.reserve(result.size());
    for (const Json& entry : result) {
        if (std::optional<HierarchyItem> item = HierarchyItemFromJson(entry)) {
            items.push_back(std::move(*item));
        }
    }
    return items;
}

std::vector<HierarchyCall> ExtractIncomingCalls(const Json& result) {
    return ExtractHierarchyCalls(result, "from");
}

std::vector<HierarchyCall> ExtractOutgoingCalls(const Json& result) {
    return ExtractHierarchyCalls(result, "to");
}

} // namespace ned::editor::lsp
