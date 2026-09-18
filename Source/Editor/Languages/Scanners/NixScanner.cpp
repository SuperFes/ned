// The nix external scanner, ported from https://github.com/nix-community/tree-sitter-nix (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::nix {

using namespace ned::editor::parse::scanner;

enum TokenType {
    STRING_FRAGMENT,
    INDENTED_STRING_FRAGMENT,
    PATH_START,
    PATH_FRAGMENT,
    DOLLAR_ESCAPE,
    INDENTED_DOLLAR_ESCAPE,
};

static void advance(Lexer* lexer) {
    lexer->advance(lexer, false);
}

static void skip(Lexer* lexer) {
    lexer->advance(lexer, true);
}

static bool scan_dollar_escape(Lexer* lexer) {
    lexer->resultSymbol = DOLLAR_ESCAPE;
    advance(lexer);
    lexer->markEnd(lexer);
    if (lexer->lookahead == '$') {
        return true;
    }
    else {
        return false;
    }
}

static bool scan_indented_dollar_escape(Lexer* lexer) {
    lexer->resultSymbol = INDENTED_DOLLAR_ESCAPE;
    advance(lexer);
    lexer->markEnd(lexer);
    if (lexer->lookahead == '$') {
        return true;
    }
    else {
        if (lexer->lookahead == '\\') {
            advance(lexer);
            if (lexer->lookahead == '$') {
                lexer->markEnd(lexer);
                return true;
            }
        }
        return false;
    }
}

// Here we only parse literal fragment inside a string.
// Delimiter, interpolation and escape sequence are handled by the parser and we
// simply stop at them.
//
// The implementation is inspired by tree-sitter-javascript:
// https://github.com/tree-sitter/tree-sitter-javascript/blob/fdeb68ac8d2bd5a78b943528bb68ceda3aade2eb/src/scanner.c#L19
static bool scan_string_fragment(Lexer* lexer) {
    lexer->resultSymbol = STRING_FRAGMENT;
    for (bool has_content = false;; has_content = true) {
        lexer->markEnd(lexer);
        switch (lexer->lookahead) {
            case '"':
            case '\\':
                return has_content;
            case '$':
                advance(lexer);
                if (lexer->lookahead == '{') {
                    return has_content;
                }
                else if (lexer->lookahead != '"' && lexer->lookahead != '\\') {
                    // Any char following '$' other than '"', '\\' and '{' (which was
                    // handled above) should be consumed as additional string content. This
                    // means `$${` doesn't start an interpolation, but `$$${` does.
                    advance(lexer);
                }
                break;
            // Simply give up on EOF or '\0'.
            case '\0':
                return false;
            default:
                advance(lexer);
        }
    }
}

// See comments of scan_string_fragment.
static bool scan_indented_string_fragment(Lexer* lexer) {
    lexer->resultSymbol = INDENTED_STRING_FRAGMENT;
    for (bool has_content = false;; has_content = true) {
        lexer->markEnd(lexer);
        switch (lexer->lookahead) {
            case '$':
                advance(lexer);
                if (lexer->lookahead == '{') {
                    return has_content;
                }
                else if (lexer->lookahead != '\'') {
                    // Any char following '$' other than '\'' and '{' (which was handled
                    // above) should be consumed as additional string content. This means
                    // `$${` doesn't start an interpolation, but `$$${` does.
                    advance(lexer);
                }
                break;
            case '\'':
                advance(lexer);
                if (lexer->lookahead == '\'') {
                    // Two single quotes always stop current string fragment.
                    // It can be either an end delimiter '', or escape sequences ''', ''$,
                    // ''\<any>
                    return has_content;
                }
                break;
            // Simply give up on EOF or '\0'.
            case '\0':
                return false;
            default:
                advance(lexer);
        }
    }
}

static bool is_path_char(int32_t c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') || c == '-' || c == '+' || c == '_' ||
           c == '.' || c == '/';
}

static bool scan_path_start(Lexer* lexer) {
    lexer->resultSymbol = PATH_START;

    bool    have_sep       = false;
    bool    have_after_sep = false;
    int32_t c              = lexer->lookahead;

    // unlike string_fragments which which are preceded by initial token (i.e.
    // '"') and thus will have all leading external whitespace consumed, we have
    // no such luxury with the path_start token.
    //
    // so we must skip over any leading whitespace here.
    while (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
        skip(lexer);
        c = lexer->lookahead;
    }

    while (true) {
        lexer->markEnd(lexer);
        c = lexer->lookahead;

        if (c == '/') {
            have_sep = true;
        }
        else if (is_path_char(c)) {
            if (have_sep) {
                have_after_sep = true;
            }
        }
        else if (c == '$') {
            // starting a interpolation,
            // so we have a valid token as long as we've seen a separator.
            // example: a/${x}
            return have_sep;
        }
        else {
            // we have a valid token if we've consumed anything after a separator.
            // example: a/b
            return have_after_sep;
        }

        advance(lexer);
    }
}

static bool scan_path_fragment(Lexer* lexer) {
    lexer->resultSymbol = PATH_FRAGMENT;

    for (bool has_content = false;; has_content = true) {
        lexer->markEnd(lexer);
        if (!is_path_char(lexer->lookahead)) {
            return has_content;
        }
        advance(lexer);
    }
}

void* Create() {
    return NULL;
}

static bool Scan(void* payload_, Lexer* lexer,
                 const bool* valid_symbols) {
    VoidPtr payload{payload_};
    // This never happens in valid grammar. Only during error recovery, everything
    // becomes valid. See: https://github.com/tree-sitter/tree-sitter/issues/1259
    //
    // We should not consume any content as string fragment during error recovery,
    // or we'll break more valid grammar below. The test 'attrset typing field
    // following string' covers this.
    if (valid_symbols[STRING_FRAGMENT] &&
        valid_symbols[INDENTED_STRING_FRAGMENT] && valid_symbols[PATH_START] &&
        valid_symbols[PATH_FRAGMENT] && valid_symbols[DOLLAR_ESCAPE] &&
        valid_symbols[INDENTED_DOLLAR_ESCAPE]) {
        return false;
    }
    else if (valid_symbols[STRING_FRAGMENT]) {
        if (lexer->lookahead == '\\') {
            return scan_dollar_escape(lexer);
        }
        return scan_string_fragment(lexer);
    }
    else if (valid_symbols[INDENTED_STRING_FRAGMENT]) {
        if (lexer->lookahead == '\'') {
            lexer->markEnd(lexer);
            advance(lexer);
            if (lexer->lookahead == '\'') {
                return scan_indented_dollar_escape(lexer);
            }
        }
        return scan_indented_string_fragment(lexer);
    }
    else if (valid_symbols[PATH_FRAGMENT] && is_path_char(lexer->lookahead)) {
        // path_fragments should be scanned as immediate tokens, with no preceding
        // extras. so we assert that the very first token is a path character, and
        // otherwise we fall through to the case below. example:
        //   a/b${c} d/e${f}
        //          ^--- note that scanning for the path_fragment will start here.
        //               this *should* be parsed as a function application.
        //               so we want to fall through to the path_start case below,
        //               which will skip the whitespace and correctly scan the
        //               following path_start.
        //
        // also, we want this above path_start, because wherever there's ambiguity
        // we want to parse another fragment instead of starting a new path.
        // example:
        //   a/b${c}d/e${f}
        // if we swap the precedence, we'd effectively parse the above as the
        // following function application:
        //   (a/b${c}) (d/e${f})
        return scan_path_fragment(lexer);
    }
    else if (valid_symbols[PATH_START]) {
        return scan_path_start(lexer);
    }

    return false;
}

static unsigned Serialize(void* payload_,
                          char* buffer) {
    VoidPtr payload{payload_};
    return 0;
}

static void Deserialize(void*       payload_,
                        const char* buffer,
                        unsigned    length) {
    VoidPtr payload{payload_};
}

static void Destroy(void* payload_) {
    VoidPtr payload{payload_};
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::nix
