#include "Editor/AutoPair.h"

#include <cctype>
#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& AutoPairMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& AutoPairStorage() {
        static bool enabled = true;
        return enabled;
    }

    [[nodiscard]] bool IsWordChar(std::string_view grapheme) {
        if (grapheme.size() != 1) {
            return false; // ASCII-only word-char check; a multi-byte grapheme never counts
        }
        const unsigned char c = static_cast<unsigned char>(grapheme[0]);
        return std::isalnum(c) || c == '_';
    }

    [[nodiscard]] bool IsQuotePair(const std::pair<char, char>& pair) {
        return pair.first == pair.second;
    }

    [[nodiscard]] const std::pair<char, char>* FindByOpen(char c, const std::vector<std::pair<char, char>>& pairs) {
        for (const auto& pair : pairs) {
            if (pair.first == c) {
                return &pair;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const std::pair<char, char>* FindByClose(char c, const std::vector<std::pair<char, char>>& pairs) {
        for (const auto& pair : pairs) {
            if (pair.second == c) {
                return &pair;
            }
        }
        return nullptr;
    }

    // Whether charAfter is a sane place to open a new quoted string -- end of
    // buffer/line, whitespace, or some other configured pair's closer (e.g.
    // typing a quote right before a `)` that's about to close an argument
    // list). Deliberately permissive about punctuation like `,`/`;` since a
    // quoted string as a function argument or statement is a common,
    // unsurprising place to want pairing (`foo(|)` -> type `"` -> `foo("|")`
    // reads naturally with `,`/`;` immediately after too).
    [[nodiscard]] bool IsSaneQuoteOpenBoundary(std::string_view grapheme, const std::vector<std::pair<char, char>>& pairs) {
        if (grapheme.empty()) {
            return true; // end of line/buffer
        }
        if (grapheme.size() != 1) {
            return false;
        }
        const char c = grapheme[0];
        if (std::isspace(static_cast<unsigned char>(c))) {
            return true;
        }
        if (c == ',' || c == ';') {
            return true;
        }
        for (const auto& pair : pairs) {
            if (c == pair.second) {
                return true;
            }
        }
        return false;
    }

} // namespace

const std::vector<std::pair<char, char>>& DefaultAutoPairs() {
    static const std::vector<std::pair<char, char>> kDefault = {
        {'(', ')'},
        {'[', ']'},
        {'{', '}'},
        {'"', '"'},
        {'\'', '\''},
    };
    return kDefault;
}

const std::vector<std::pair<char, char>>& LispAutoPairs() {
    static const std::vector<std::pair<char, char>> kLisp = {
        {'(', ')'},
        {'[', ']'},
        {'{', '}'},
        {'"', '"'},
    };
    return kLisp;
}

void SetAutoPairEnabled(bool enabled) {
    const std::lock_guard lock(AutoPairMutex());
    AutoPairStorage() = enabled;
}

bool AutoPairEnabled() {
    const std::lock_guard lock(AutoPairMutex());
    return AutoPairStorage();
}

std::optional<char> ClosingCharFor(char opener, const std::vector<std::pair<char, char>>& pairs) {
    if (const std::pair<char, char>* pair = FindByOpen(opener, pairs)) {
        return pair->second;
    }
    return std::nullopt;
}

PairAction DecideSelfInsert(const AutoPairQuery& query) {
    if (!query.pairs || query.pairs->empty()) {
        return PairAction::InsertPlain;
    }
    const std::vector<std::pair<char, char>>& pairs = *query.pairs;

    if (query.hasSelection) {
        // Only an opener (which, for a symmetric quote pair, is also the
        // only entry -- first == typed) wraps the selection. A bare closer
        // (')' with no matching '(' entry keyed by that close char alone)
        // just replaces the selection like an ordinary self-insert -- typing
        // ')' around a selection isn't a "surround with" gesture in any
        // mainstream editor.
        if (FindByOpen(query.typed, pairs) != nullptr) {
            return PairAction::WrapSelection;
        }
        return PairAction::InsertPlain;
    }

    if (const std::pair<char, char>* pair = FindByOpen(query.typed, pairs)) {
        if (IsQuotePair(*pair)) {
            // Already inside a string/comment: closing an unterminated
            // string, or typing a literal quote character inside prose,
            // should never pair.
            if (query.classAtPoint == SyntaxClass::String || query.classAtPoint == SyntaxClass::StringEscape ||
                query.classAtPoint == SyntaxClass::Comment || query.classAtPoint == SyntaxClass::DocComment) {
                return PairAction::InsertPlain;
            }
            // Typing the closing half of a quote pair that's already sitting
            // right after point -- move over it instead of nesting a second
            // pair.
            if (query.charAfter.size() == 1 && query.charAfter[0] == pair->second) {
                return PairAction::SkipOver;
            }
            // "don't"/"it's": a quote immediately after a word character is
            // almost always a contraction/apostrophe, not the start of a new
            // string.
            if (IsWordChar(query.charBefore)) {
                return PairAction::InsertPlain;
            }
            if (!IsSaneQuoteOpenBoundary(query.charAfter, pairs)) {
                return PairAction::InsertPlain;
            }
            return PairAction::InsertPair;
        }
        // A bracket opener always pairs, regardless of what's on either
        // side -- nesting `(` before an existing `)` is expected to insert a
        // new pair, not skip over the outer one.
        return PairAction::InsertPair;
    }

    if (const std::pair<char, char>* pair = FindByClose(query.typed, pairs); pair != nullptr && !IsQuotePair(*pair)) {
        if (query.charAfter.size() == 1 && query.charAfter[0] == pair->second) {
            return PairAction::SkipOver;
        }
        return PairAction::InsertPlain;
    }

    return PairAction::InsertPlain;
}

bool ShouldDeleteAdjacentPair(std::string_view charBefore, std::string_view charAfter,
                              const std::vector<std::pair<char, char>>& pairs) {
    if (charBefore.size() != 1 || charAfter.size() != 1) {
        return false;
    }
    for (const auto& pair : pairs) {
        if (charBefore[0] == pair.first && charAfter[0] == pair.second) {
            return true;
        }
    }
    return false;
}

} // namespace ned::editor
