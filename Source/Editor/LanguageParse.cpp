#include "LanguageParse.h"

#include <stdexcept>

#include "JanetData.h"
#include "SyntaxTheme.h"

namespace ned::editor {

namespace {

    using janetdata::Value;

    [[noreturn]] void Fail(std::string_view directory, int line, const std::string& message) {
        throw std::runtime_error(std::string(directory) + "/language.janet:" + std::to_string(line) + ": " + message);
    }

    std::string ExpectString(std::string_view directory, const Value& value, const char* what) {
        if (!value.IsString()) {
            Fail(directory, value.line, std::string(what) + " must be a string");
        }
        return value.text;
    }

    std::vector<std::string> ExpectStrings(std::string_view directory, const Value& value, const char* what) {
        if (!value.IsTuple()) {
            Fail(directory, value.line, std::string(what) + " must be a tuple of strings");
        }
        std::vector<std::string> out;
        out.reserve(value.items.size());
        for (const Value& item : value.items) {
            out.push_back(ExpectString(directory, item, what));
        }
        return out;
    }

    bool ExpectBool(std::string_view directory, const Value& value, const char* what) {
        if (!value.IsBool()) {
            Fail(directory, value.line, std::string(what) + " must be true or false");
        }
        return value.boolean;
    }

    // {"trigger" "body" ...} pairs, in file order.
    std::vector<std::pair<std::string, std::string>> ExpectStringPairs(std::string_view directory, const Value& value,
                                                                       const char* what) {
        if (!value.IsStruct()) {
            Fail(directory, value.line, std::string(what) + " must be a struct of string -> string");
        }
        std::vector<std::pair<std::string, std::string>> out;
        for (std::size_t i = 0; i + 1 < value.pairs.size(); i += 2) {
            out.emplace_back(ExpectString(directory, value.pairs[i], what),
                             ExpectString(directory, value.pairs[i + 1], what));
        }
        return out;
    }

    std::vector<std::string>* QueryListFor(QueryFiles& files, std::string_view kind) {
        if (kind == "highlights")
            return &files.highlights;
        if (kind == "folds")
            return &files.folds;
        if (kind == "imports")
            return &files.imports;
        if (kind == "tags")
            return &files.tags;
        if (kind == "tests")
            return &files.tests;
        if (kind == "indents")
            return &files.indents;
        if (kind == "locals")
            return &files.locals;
        if (kind == "injections")
            return &files.injections;
        return nullptr;
    }

    constexpr std::string_view kQueryKinds[] = {"highlights", "folds", "imports", "tags",
                                                "tests", "indents", "locals", "injections"};

} // namespace

LanguageDefinition ParseLanguageDefinition(std::string_view directoryName, std::string_view source) {
    Value root{};
    try {
        root = janetdata::ParseJanetData(source);
    }
    catch (const janetdata::JanetDataError& error) {
        throw std::runtime_error(std::string(directoryName) + "/language.janet:" + error.what());
    }
    if (!root.IsStruct()) {
        Fail(directoryName, root.line, "a definition is one {:key value ...} struct");
    }

    LanguageDefinition definition;
    definition.name = std::string(directoryName);

    for (std::size_t i = 0; i + 1 < root.pairs.size(); i += 2) {
        const Value& keyValue = root.pairs[i];
        const Value& value    = root.pairs[i + 1];
        if (!keyValue.IsKeyword()) {
            Fail(directoryName, keyValue.line, "definition keys are keywords (:name, :extensions, ...)");
        }
        const std::string& key = keyValue.text;

        if (key == "name") {
            if (ExpectString(directoryName, value, ":name") != directoryName) {
                Fail(directoryName, value.line, ":name must match the directory (\"" + std::string(directoryName) + "\")");
            }
        }
        else if (key == "grammar") {
            if (value.IsKeyword() && value.text == "none") {
                definition.grammarless = true;
            }
            else {
                definition.grammar = ExpectString(directoryName, value, ":grammar");
            }
        }
        else if (key == "extensions") {
            definition.extensions = ExpectStrings(directoryName, value, ":extensions");
            for (const std::string& extension : definition.extensions) {
                if (extension.empty() || extension.front() != '.') {
                    Fail(directoryName, value.line, ":extensions entries carry their leading dot (\".cpp\")");
                }
            }
        }
        else if (key == "filenames") {
            definition.filenames = ExpectStrings(directoryName, value, ":filenames");
        }
        else if (key == "line-comment") {
            definition.lineCommentPrefix = ExpectString(directoryName, value, ":line-comment");
        }
        else if (key == "auto-pairs") {
            if (!value.IsKeyword() || (value.text != "default" && value.text != "lisp")) {
                Fail(directoryName, value.line, ":auto-pairs is :default or :lisp");
            }
            definition.autoPairs = value.text == "lisp" ? AutoPairSet::Lisp : AutoPairSet::Default;
        }
        else if (key == "wrap-lines") {
            definition.wrapLines = ExpectBool(directoryName, value, ":wrap-lines");
        }
        else if (key == "embedded-documents") {
            definition.embeddedDocuments = ExpectBool(directoryName, value, ":embedded-documents");
        }
        else if (key == "keymap") {
            if (!value.IsTuple()) {
                Fail(directoryName, value.line, ":keymap is a tuple of [\"key sequence\" \"command\"] pairs");
            }
            for (const Value& entry : value.items) {
                if (!entry.IsTuple() || entry.items.size() != 2) {
                    Fail(directoryName, entry.line, ":keymap entries are [\"key sequence\" \"command\"] pairs");
                }
                definition.keymap.emplace_back(ExpectString(directoryName, entry.items[0], ":keymap"),
                                               ExpectString(directoryName, entry.items[1], ":keymap"));
            }
        }
        else if (key == "capture-classes") {
            if (!value.IsStruct()) {
                Fail(directoryName, value.line, ":capture-classes is {\"capture.name\" :syntax-class ...}");
            }
            for (std::size_t j = 0; j + 1 < value.pairs.size(); j += 2) {
                const Value& capture = value.pairs[j];
                const Value& cls     = value.pairs[j + 1];
                if (!capture.IsString() || !cls.IsKeyword()) {
                    Fail(directoryName, capture.line, ":capture-classes maps a \"capture.name\" to a :syntax-class keyword");
                }
                try {
                    definition.captureClasses.emplace_back(capture.text, SyntaxClassByName(cls.text));
                }
                catch (const std::exception&) {
                    Fail(directoryName, cls.line, "unknown syntax class :" + cls.text);
                }
            }
        }
        else if (key == "grammar-library") {
            definition.grammarLibrary = ExpectString(directoryName, value, ":grammar-library");
        }
        else if (key == "queries-dir") {
            definition.queriesDir = ExpectString(directoryName, value, ":queries-dir");
        }
        else if (key == "queries-from") {
            definition.queriesFrom = ExpectString(directoryName, value, ":queries-from");
        }
        else if (key == "queries") {
            if (!value.IsStruct()) {
                Fail(directoryName, value.line, ":queries is {:kind [\"path\" ...] ...}");
            }
            for (std::size_t j = 0; j + 1 < value.pairs.size(); j += 2) {
                const Value&              kind  = value.pairs[j];
                const Value&              paths = value.pairs[j + 1];
                std::vector<std::string>* list =
                    kind.IsKeyword() ? QueryListFor(definition.queries, kind.text) : nullptr;
                if (list == nullptr) {
                    Fail(directoryName, kind.line,
                         ":queries keys are :highlights/:folds/:imports/:tags/:tests/:indents/:locals/:injections");
                }
                *list = ExpectStrings(directoryName, paths, ":queries");
            }
        }
        else if (key == "escapes") {
            definition.escapes = ExpectStrings(directoryName, value, ":escapes");
        }
        else if (key == "lsp-root-markers") {
            definition.lspRootMarkers = ExpectStrings(directoryName, value, ":lsp-root-markers");
        }
        else if (key == "import-resolution") {
            if (!value.IsStruct()) {
                Fail(directoryName, value.line, ":import-resolution is a struct");
            }
            ImportResolutionConfig config;
            for (std::size_t j = 0; j + 1 < value.pairs.size(); j += 2) {
                const Value& field = value.pairs[j];
                const Value& v     = value.pairs[j + 1];
                if (field.IsKeyword() && field.text == "extensions") {
                    config.extensions = ExpectStrings(directoryName, v, ":extensions");
                }
                else if (field.IsKeyword() && field.text == "index-basenames") {
                    config.indexBasenames = ExpectStrings(directoryName, v, ":index-basenames");
                }
                else if (field.IsKeyword() && field.text == "search-package-dirs") {
                    config.searchPackageDirs = ExpectBool(directoryName, v, ":search-package-dirs");
                }
                else {
                    Fail(directoryName, field.line,
                         ":import-resolution keys are :extensions/:index-basenames/:search-package-dirs");
                }
            }
            definition.importResolution = std::move(config);
        }
        else if (key == "injection-aliases") {
            definition.injectionAliases = ExpectStrings(directoryName, value, ":injection-aliases");
        }
        else if (key == "snippets") {
            definition.snippets = ExpectStringPairs(directoryName, value, ":snippets");
        }
        else {
            Fail(directoryName, keyValue.line, "unknown definition key :" + key);
        }
    }
    return definition;
}

void DiscoverQueryFiles(LanguageDefinition& definition, const QueryFileExists& exists, std::string_view prefix) {
    if (definition.grammarless) {
        return;
    }
    const std::string dir = definition.queriesFrom.empty() ? definition.name : definition.queriesFrom;
    for (const std::string_view kind : kQueryKinds) {
        std::vector<std::string>* list = QueryListFor(definition.queries, kind);
        if (!list->empty()) {
            continue; // an explicit :queries entry replaces discovery for its kind
        }
        for (const std::string_view shape : {"/upstream/", "/"}) {
            const std::string path =
                std::string(prefix) + dir + std::string(shape) + std::string(kind) + ".janet";
            if (exists(path)) {
                list->push_back(path);
            }
        }
    }
}

} // namespace ned::editor
