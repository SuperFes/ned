#include "InlineDebugValues.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <optional>
#include <utility>

#include "LocalScopes.h"

namespace ned::editor {

namespace {

    std::mutex& EnabledMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& EnabledStorage() {
        static bool enabled = true;
        return enabled;
    }

    // Does `text` contain `word` bounded by non-identifier characters on
    // both sides? The whole of InlineDebugValueTier::Textual -- see that
    // enum for what it cannot know.
    bool ContainsWholeWord(std::string_view text, std::string_view word) {
        if (word.empty() || word.size() > text.size()) {
            return false;
        }
        const auto isIdentifier = [](char character) {
            return (static_cast<unsigned char>(character) & 0x80U) != 0 ||
                   std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' || character == '$';
        };
        for (std::size_t at = text.find(word); at != std::string_view::npos; at = text.find(word, at + 1)) {
            const bool leftClear  = at == 0 || !isIdentifier(text[at - 1]);
            const bool rightClear = at + word.size() >= text.size() || !isIdentifier(text[at + word.size()]);
            if (leftClear && rightClear) {
                return true;
            }
        }
        return false;
    }

    using Range = locals::Range;

    bool Contains(Range outer, std::size_t point) {
        return outer.first <= point && point <= outer.second;
    }

    // Can the frame stopped at `stopByte` see this binding? A file-level
    // binding is visible from anywhere in the file; any other is visible
    // exactly inside the scope that owns it.
    bool VisibleFromStop(const locals::LocalBinding& binding, std::size_t stopByte) {
        if (binding.scopeIsFile || !binding.scope) {
            return true;
        }
        return Contains(*binding.scope, stopByte);
    }

    // How far out a binding sits, for picking between two the stop can see
    // both of. A file-level binding is the outermost there is.
    std::size_t ScopeWidth(const locals::LocalBinding& binding, std::size_t textLength) {
        if (binding.scopeIsFile || !binding.scope) {
            return textLength + 1;
        }
        return binding.scope->second - binding.scope->first;
    }

    // Every byte range in `text` at which `name` refers to the binding the
    // stopped frame's value for `name` actually belongs to.
    //
    // Visibility alone is not enough, and this is the subtle half: where an
    // inner scope shadows an outer one, the stop can see BOTH -- the outer
    // binding's scope encloses the inner block the stop sits in. The
    // adapter reports one value per name, the innermost one
    // (Dap::Manager::FrameLocals says so), so the innermost visible binding
    // is the only one that value describes, and annotating the shadowed
    // outer occurrences with it would state something false.
    //
    // Resolution is per BINDING, not per occurrence: ResolveBindingAt hands
    // back the complete occurrence list of whatever binding it resolved, so
    // one call settles every other occurrence of that same binding at once.
    // Only a genuinely different binding of the same name -- a shadow, or
    // the same name in an unrelated function -- costs a second call.
    std::vector<Range> VisibleOccurrencesOf(std::string_view text, std::span<const LocalCapture> captures,
                                            std::string_view name, std::size_t stopByte) {
        std::optional<locals::LocalBinding> innermost;
        std::vector<Range>                  settled;
        for (const LocalCapture& capture : captures) {
            if (capture.kind == LocalCaptureKind::Scope) {
                continue;
            }
            if (capture.startByte >= capture.endByte || capture.endByte > text.size()) {
                continue; // a stale capture list against shorter text -- never trusted, never fatal
            }
            if (capture.endByte - capture.startByte != name.size() ||
                text.compare(capture.startByte, name.size(), name) != 0) {
                continue;
            }
            const Range range{capture.startByte, capture.endByte};
            if (std::find(settled.begin(), settled.end(), range) != settled.end()) {
                continue; // already covered by the binding some earlier occurrence resolved to
            }
            const std::optional<locals::LocalBinding> binding =
                locals::ResolveBindingAt(captures, text, capture.startByte);
            if (!binding || binding->name != name) {
                settled.push_back(range);
                continue;
            }
            settled.insert(settled.end(), binding->occurrences.begin(), binding->occurrences.end());
            if (!VisibleFromStop(*binding, stopByte)) {
                continue;
            }
            if (!innermost || ScopeWidth(*binding, text.size()) < ScopeWidth(*innermost, text.size())) {
                innermost = binding;
            }
        }
        if (!innermost) {
            return {};
        }
        std::vector<Range> accepted = innermost->occurrences;
        std::sort(accepted.begin(), accepted.end());
        accepted.erase(std::unique(accepted.begin(), accepted.end()), accepted.end());
        return accepted;
    }

    bool AnyOccurrenceOnLine(const std::vector<Range>& occurrences, const InlineDebugValueLine& line) {
        return std::any_of(occurrences.begin(), occurrences.end(), [&line](Range range) {
            return range.first >= line.startByte && range.second <= line.endByte;
        });
    }

} // namespace

void SetInlineDebugValuesEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    EnabledStorage() = enabled;
}

bool InlineDebugValuesEnabled() {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    return EnabledStorage();
}

std::vector<InlineDebugValue> ResolveInlineDebugValues(std::string_view text, std::span<const LocalCapture> captures,
                                                       InlineDebugValueTier                      tier,
                                                       const std::map<std::string, std::string>& frameLocals,
                                                       std::size_t stopByte, std::span<const InlineDebugValueLine> lines,
                                                       std::size_t maxPerLine) {
    std::vector<InlineDebugValue> values;
    if (frameLocals.empty() || lines.empty() || maxPerLine == 0) {
        return values;
    }

    // Resolved once per name for the whole request rather than per line:
    // a binding's occurrence list already spans every line it appears on,
    // and a viewport routinely shows several lines mentioning the same
    // local.
    std::map<std::string, std::vector<Range>> occurrences;
    if (tier == InlineDebugValueTier::Scoped) {
        for (const auto& [name, value] : frameLocals) {
            if (name.empty()) {
                continue;
            }
            std::vector<Range> found = VisibleOccurrencesOf(text, captures, name, stopByte);
            if (!found.empty()) {
                occurrences.emplace(name, std::move(found));
            }
        }
        if (occurrences.empty()) {
            return values;
        }
    }

    for (const InlineDebugValueLine& line : lines) {
        if (line.endByte <= line.startByte || line.endByte > text.size()) {
            continue;
        }
        std::size_t shown = 0;
        for (const auto& [name, value] : frameLocals) {
            if (shown >= maxPerLine) {
                break;
            }
            bool mentioned = false;
            if (tier == InlineDebugValueTier::Scoped) {
                const auto entry = occurrences.find(name);
                mentioned        = entry != occurrences.end() && AnyOccurrenceOnLine(entry->second, line);
            }
            else {
                mentioned = ContainsWholeWord(text.substr(line.startByte, line.endByte - line.startByte), name);
            }
            if (!mentioned) {
                continue;
            }
            values.push_back(InlineDebugValue{.line = line.line, .name = name, .value = value});
            ++shown;
        }
    }
    return values;
}

} // namespace ned::editor
