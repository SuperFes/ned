#include "LanguageDefinition.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "AutoPair.h"
#include "CaptureClassifiers.h"
#include "Key.h"
#include "LanguageFiles.h"
#include "SyntaxTheme.h"
#include "TreeSitter/Languages.h"
#include "TreeSitter/Query.h"

namespace ned::editor {

namespace {

    std::mutex                                  g_escapeMutex;
    std::unordered_map<std::string, ModeEscape> g_escapes;

    ModeEscape FindEscape(std::string_view name) {
        const std::lock_guard<std::mutex> lock(g_escapeMutex);
        const auto                        it = g_escapes.find(std::string(name));
        if (it == g_escapes.end()) {
            throw std::runtime_error("language definition names an unregistered escape: " + std::string(name));
        }
        return it->second;
    }

    void ApplyDefinition(Mode& mode, const LanguageDefinition& definition) {
        mode.lineCommentPrefix = definition.lineCommentPrefix;
        mode.wrapLines         = definition.wrapLines;
        mode.autoPairs         = definition.autoPairs == AutoPairSet::Lisp ? LispAutoPairs() : DefaultAutoPairs();
        for (const auto& [sequence, command] : definition.keymap) {
            mode.keymap.Bind(ParseKeySequence(sequence), command);
        }
        if (!definition.embeddedDocuments) {
            mode.embeddedRegions = EmbeddedRegionFunction();
        }
    }

    // The post-pass behind capture classifiers and :capture-spans, run over
    // whatever the highlight (generic or escape-installed) produced.
    // Classification first -- a classifier reads the capture's own text, and
    // a span rule may change what range that text covers.
    void ApplyCaptureRules(std::vector<HighlightSpan>& spans, std::string_view bufferText,
                           const std::vector<std::pair<CaptureId, CaptureSpanRule>>& spanRules,
                           const std::vector<CaptureId>& suppressedIds, std::string_view languageKey) {
        if (!suppressedIds.empty()) {
            std::erase_if(spans, [&suppressedIds](const HighlightSpan& span) {
                return span.captureId != kNoCapture &&
                       std::find(suppressedIds.begin(), suppressedIds.end(), span.captureId) != suppressedIds.end();
            });
        }
        if (HasCaptureClassifiers(languageKey)) {
            // Group span indices by capture id, then one batch call per
            // classified name -- see CaptureClassifiers.h for why batch.
            std::map<CaptureId, std::vector<std::size_t>> byId;
            for (std::size_t i = 0; i < spans.size(); ++i) {
                if (spans[i].captureId != kNoCapture) {
                    byId[spans[i].captureId].push_back(i);
                }
            }
            std::vector<bool> suppressed(spans.size(), false);
            for (const auto& [id, indices] : byId) {
                const CaptureClassifier classifier = FindCaptureClassifier(languageKey, CaptureNameForId(id));
                if (!classifier) {
                    continue;
                }
                std::vector<std::string_view> texts;
                texts.reserve(indices.size());
                for (const std::size_t i : indices) {
                    texts.push_back(bufferText.substr(spans[i].startByte, spans[i].endByte - spans[i].startByte));
                }
                const std::vector<CaptureClassification> results = classifier(texts);
                if (results.size() != texts.size()) {
                    continue; // wrong-sized result: all-Fallthrough, per the contract
                }
                for (std::size_t j = 0; j < indices.size(); ++j) {
                    switch (results[j].kind) {
                        case CaptureClassification::Kind::Classified:
                            spans[indices[j]].syntaxClass = results[j].cls;
                            break;
                        case CaptureClassification::Kind::Suppress:
                            suppressed[indices[j]] = true;
                            break;
                        case CaptureClassification::Kind::Fallthrough:
                            break;
                    }
                }
            }
            std::size_t kept = 0;
            for (std::size_t i = 0; i < spans.size(); ++i) {
                if (!suppressed[i]) {
                    spans[kept++] = spans[i];
                }
            }
            spans.resize(kept);
        }
        for (HighlightSpan& span : spans) {
            for (const auto& [id, rule] : spanRules) {
                if (span.captureId == id && rule == CaptureSpanRule::LineEnd) {
                    const std::size_t newline = bufferText.find('\n', span.endByte);
                    span.endByte              = newline == std::string_view::npos ? bufferText.size() : newline;
                }
            }
        }
    }

    Mode Finish(Mode mode, const LanguageDefinition& definition, const ModeBuildContext& context) {
        ApplyDefinition(mode, definition);
        for (const std::string& name : definition.escapes) {
            FindEscape(name)(mode, definition, context);
        }
        // After the escapes: an escape may install its own highlight, and
        // the rules apply to whatever actually runs. Always wrapped (when
        // there is a highlight at all) so a classifier registered later --
        // init.janet loads after the first modes are built -- takes effect
        // on the next repaint with no cache coupling; the no-rules,
        // no-classifier run costs one registry check.
        if (mode.highlight) {
            std::vector<std::pair<CaptureId, CaptureSpanRule>> spanRules;
            spanRules.reserve(definition.captureSpans.size());
            for (const auto& [name, rule] : definition.captureSpans) {
                spanRules.emplace_back(InternCaptureName(name), rule);
            }
            std::vector<CaptureId> suppressedIds;
            suppressedIds.reserve(definition.suppressedCaptures.size());
            for (const std::string& name : definition.suppressedCaptures) {
                suppressedIds.push_back(InternCaptureName(name));
            }
            mode.highlight = [inner = std::move(mode.highlight), spanRules = std::move(spanRules),
                              suppressedIds = std::move(suppressedIds),
                              languageKey   = definition.name](std::string_view bufferText,
                                                             HighlightWindow  window) -> std::vector<HighlightSpan> {
                std::vector<HighlightSpan> spans = inner(bufferText, window);
                ApplyCaptureRules(spans, bufferText, spanRules, suppressedIds, languageKey);
                return spans;
            };
        }
        return mode;
    }

} // namespace

std::string ModeNameFor(const LanguageDefinition& definition) {
    return definition.name + "-mode";
}

void RegisterModeEscape(std::string name, ModeEscape escape) {
    const std::lock_guard<std::mutex> lock(g_escapeMutex);
    g_escapes[std::move(name)] = std::move(escape);
}

bool HasModeEscape(std::string_view name) {
    const std::lock_guard<std::mutex> lock(g_escapeMutex);
    return g_escapes.contains(std::string(name));
}

Mode ModeFromDefinition(const LanguageDefinition& definition) {
    if (definition.grammarless) {
        Mode mode{.name = ModeNameFor(definition), .keymap = Keymap(), .highlight = HighlightFunction()};
        return Finish(std::move(mode), definition, ModeBuildContext{.languageKey = definition.name});
    }
    const std::string_view grammar  = definition.grammar.empty() ? std::string_view(definition.name) : definition.grammar;
    const auto             language = treesitter::LanguageByName(grammar);
    if (!language) {
        throw std::runtime_error("language definition '" + definition.name + "' names a grammar that is not bundled: " +
                                 std::string(grammar));
    }
    return ModeFromDefinition(definition, *language);
}

namespace {

    // The eight kinds, compiled; kept alive for the duration of the build
    // (TreeSitterModeFromLanguage retains none of the text).
    struct CompiledQueries {
        QueryText highlights, folds, imports, tags, tests, indents, locals, injections;

        [[nodiscard]] TreeSitterQuerySources Views() const {
            return {.highlights = highlights.text,
                    .folds      = folds.text,
                    .imports    = imports.text,
                    .tags       = tags.text,
                    .tests      = tests.text,
                    .indents    = indents.text,
                    .locals     = locals.text,
                    .injections = injections.text};
        }
    };

    CompiledQueries Compile(const QueryFiles& files) {
        return {.highlights = CompileQueryFiles(files.highlights),
                .folds      = CompileQueryFiles(files.folds),
                .imports    = CompileQueryFiles(files.imports),
                .tags       = CompileQueryFiles(files.tags),
                .tests      = CompileQueryFiles(files.tests),
                .indents    = CompileQueryFiles(files.indents),
                .locals     = CompileQueryFiles(files.locals),
                .injections = CompileQueryFiles(files.injections)};
    }

    // Which kind tree-sitter rejected, and where in which file: the generic
    // build compiles every kind in one go and its exception carries only a
    // byte offset, so on failure each kind is compiled again alone -- an
    // error path only, never paid on success.
    [[noreturn]] void RethrowLocated(const LanguageDefinition& definition, const treesitter::Language& language,
                                     const CompiledQueries& compiled, const treesitter::QueryCompileError& error) {
        for (const QueryText* text : {&compiled.highlights, &compiled.folds, &compiled.imports, &compiled.tags, &compiled.tests,
                                      &compiled.indents, &compiled.locals, &compiled.injections}) {
            if (text->text.empty()) {
                continue;
            }
            try {
                treesitter::Query probe(language, text->text);
            }
            catch (const treesitter::QueryCompileError& kindError) {
                throw std::runtime_error("language '" + definition.name + "': " + text->Locate(kindError.Offset()) +
                                         ": tree-sitter query error (" + std::string(kindError.Kind()) + ")");
            }
        }
        throw std::runtime_error("language '" + definition.name + "': " + error.what());
    }

} // namespace

Mode ModeFromDefinition(const LanguageDefinition& definition, const treesitter::Language& language) {
    const CompiledQueries compiled = Compile(definition.queries);
    ModeBuildContext      context;
    try {
        Mode mode = TreeSitterModeFromLanguage(ModeNameFor(definition), language, compiled.Views(), &context);
        return Finish(std::move(mode), definition, context);
    }
    catch (const treesitter::QueryCompileError& error) {
        RethrowLocated(definition, language, compiled, error);
    }
}

} // namespace ned::editor
