// The svelte external scanner, ported from https://github.com/tree-sitter-grammars/tree-sitter-svelte (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::svelte {

using namespace ned::editor::parse::scanner;

// --- tag.h (from the same grammar) ---

typedef enum {
    AREA,
    BASE,
    BASEFONT,
    BGSOUND,
    BR,
    COL,
    COMMAND,
    EMBED,
    FRAME,
    HR,
    IMAGE,
    IMG,
    INPUT,
    ISINDEX,
    KEYGEN,
    LINK,
    MENUITEM,
    META,
    NEXTID,
    PARAM,
    SOURCE,
    TRACK,
    WBR,
    END_OF_VOID_TAGS,

    A,
    ABBR,
    ADDRESS,
    ARTICLE,
    ASIDE,
    AUDIO,
    B,
    BDI,
    BDO,
    BLOCKQUOTE,
    BODY,
    BUTTON,
    CANVAS,
    CAPTION,
    CITE,
    CODE,
    COLGROUP,
    DATA,
    DATALIST,
    DD,
    DEL,
    DETAILS,
    DFN,
    DIALOG,
    DIV,
    DL,
    DT,
    EM,
    FIELDSET,
    FIGCAPTION,
    FIGURE,
    FOOTER,
    FORM,
    H1,
    H2,
    H3,
    H4,
    H5,
    H6,
    HEAD,
    HEADER,
    HGROUP,
    HTML,
    I,
    IFRAME,
    INS,
    KBD,
    LABEL,
    LEGEND,
    LI,
    MAIN,
    MAP,
    MARK,
    MATH,
    MENU,
    METER,
    NAV,
    NOSCRIPT,
    OBJECT,
    OL,
    OPTGROUP,
    OPTION,
    OUTPUT,
    P,
    PICTURE,
    PRE,
    PROGRESS,
    Q,
    RB,
    RP,
    RT,
    RTC,
    RUBY,
    S,
    SAMP,
    SCRIPT,
    SECTION,
    SELECT,
    SLOT,
    SMALL,
    SPAN,
    STRONG,
    STYLE,
    SUB,
    SUMMARY,
    SUP,
    SVG,
    TABLE,
    TBODY,
    TD,
    TEMPLATE,
    TEXTAREA,
    TFOOT,
    TH,
    THEAD,
    TIME,
    TITLE,
    TR,
    U,
    UL,
    VAR,
    VIDEO,

    CUSTOM,

    END_,
} TagType;

typedef Array(char) String;

typedef struct {
    char tag_name[16];
    TagType tag_type;
} TagMapEntry;

typedef struct {
    TagType type;
    String custom_tag_name;
} Tag;

static const TagMapEntry TAG_TYPES_BY_TAG_NAME[126] = {
    {"area",       AREA      },
    {"base",       BASE      },
    {"basefont",   BASEFONT  },
    {"bgsound",    BGSOUND   },
    {"br",         BR        },
    {"col",        COL       },
    {"command",    COMMAND   },
    {"embed",      EMBED     },
    {"frame",      FRAME     },
    {"hr",         HR        },
    {"image",      IMAGE     },
    {"img",        IMG       },
    {"input",      INPUT     },
    {"isindex",    ISINDEX   },
    {"keygen",     KEYGEN    },
    {"link",       LINK      },
    {"menuitem",   MENUITEM  },
    {"meta",       META      },
    {"nextid",     NEXTID    },
    {"param",      PARAM     },
    {"source",     SOURCE    },
    {"track",      TRACK     },
    {"wbr",        WBR       },
    {"a",          A         },
    {"abbr",       ABBR      },
    {"address",    ADDRESS   },
    {"article",    ARTICLE   },
    {"aside",      ASIDE     },
    {"audio",      AUDIO     },
    {"b",          B         },
    {"bdi",        BDI       },
    {"bdo",        BDO       },
    {"blockquote", BLOCKQUOTE},
    {"body",       BODY      },
    {"button",     BUTTON    },
    {"canvas",     CANVAS    },
    {"caption",    CAPTION   },
    {"cite",       CITE      },
    {"code",       CODE      },
    {"colgroup",   COLGROUP  },
    {"data",       DATA      },
    {"datalist",   DATALIST  },
    {"dd",         DD        },
    {"del",        DEL       },
    {"details",    DETAILS   },
    {"dfn",        DFN       },
    {"dialog",     DIALOG    },
    {"div",        DIV       },
    {"dl",         DL        },
    {"dt",         DT        },
    {"em",         EM        },
    {"fieldset",   FIELDSET  },
    {"figcaption", FIGCAPTION},
    {"figure",     FIGURE    },
    {"footer",     FOOTER    },
    {"form",       FORM      },
    {"h1",         H1        },
    {"h2",         H2        },
    {"h3",         H3        },
    {"h4",         H4        },
    {"h5",         H5        },
    {"h6",         H6        },
    {"head",       HEAD      },
    {"header",     HEADER    },
    {"hgroup",     HGROUP    },
    {"html",       HTML      },
    {"i",          I         },
    {"iframe",     IFRAME    },
    {"ins",        INS       },
    {"kbd",        KBD       },
    {"label",      LABEL     },
    {"legend",     LEGEND    },
    {"li",         LI        },
    {"main",       MAIN      },
    {"map",        MAP       },
    {"mark",       MARK      },
    {"math",       MATH      },
    {"menu",       MENU      },
    {"meter",      METER     },
    {"nav",        NAV       },
    {"noscript",   NOSCRIPT  },
    {"object",     OBJECT    },
    {"ol",         OL        },
    {"optgroup",   OPTGROUP  },
    {"option",     OPTION    },
    {"output",     OUTPUT    },
    {"p",          P         },
    {"picture",    PICTURE   },
    {"pre",        PRE       },
    {"progress",   PROGRESS  },
    {"q",          Q         },
    {"rb",         RB        },
    {"rp",         RP        },
    {"rt",         RT        },
    {"rtc",        RTC       },
    {"ruby",       RUBY      },
    {"s",          S         },
    {"samp",       SAMP      },
    {"script",     SCRIPT    },
    {"section",    SECTION   },
    {"select",     SELECT    },
    {"slot",       SLOT      },
    {"small",      SMALL     },
    {"span",       SPAN      },
    {"strong",     STRONG    },
    {"style",      STYLE     },
    {"sub",        SUB       },
    {"summary",    SUMMARY   },
    {"sup",        SUP       },
    {"svg",        SVG       },
    {"table",      TABLE     },
    {"tbody",      TBODY     },
    {"td",         TD        },
    {"template",   TEMPLATE  },
    {"textarea",   TEXTAREA  },
    {"tfoot",      TFOOT     },
    {"th",         TH        },
    {"thead",      THEAD     },
    {"time",       TIME      },
    {"title",      TITLE     },
    {"tr",         TR        },
    {"u",          U         },
    {"ul",         UL        },
    {"var",        VAR       },
    {"video",      VIDEO     },
    {"custom",     CUSTOM    },
};

static const TagType TAG_TYPES_NOT_ALLOWED_IN_PARAGRAPHS[] = {
    ADDRESS, ARTICLE, ASIDE, BLOCKQUOTE, DETAILS, DIV,    DL, FIELDSET, FIGCAPTION, FIGURE, FOOTER, FORM, H1,
    H2,      H3,      H4,    H5,         H6,      HEADER, HR, MAIN,     NAV,        OL,     P,      PRE,  SECTION,
};

static TagType tag_type_for_name(const String *tag_name) {
    for (int i = 0; i < 126; i++) {
        const TagMapEntry *entry = &TAG_TYPES_BY_TAG_NAME[i];
        if (strlen(entry->tag_name) == tag_name->size &&
            memcmp(tag_name->contents, entry->tag_name, tag_name->size) == 0) {
            return entry->tag_type;
        }
    }
    return CUSTOM;
}

static inline Tag tag_new() {
    Tag tag;
    tag.type = END_;
    tag.custom_tag_name = (String)array_new();
    return tag;
}

static inline Tag tag_for_name(String name) {
    Tag tag = tag_new();
    tag.type = tag_type_for_name(&name);
    if (tag.type == CUSTOM) {
        tag.custom_tag_name = name;
    } else {
        array_delete(&name);
    }
    return tag;
}

static inline void tag_free(Tag *self) {
    if (self->type == CUSTOM) {
        array_delete(&self->custom_tag_name);
    }
}

static inline bool tag_is_void(const Tag *self) { return self->type < END_OF_VOID_TAGS; }

static inline bool tag_eq(const Tag *self, const Tag *other) {
    if (self->type != other->type) {
        return false;
    }
    if (self->type == CUSTOM) {
        if (self->custom_tag_name.size != other->custom_tag_name.size) {
            return false;
        }
        if (memcmp(self->custom_tag_name.contents, other->custom_tag_name.contents, self->custom_tag_name.size) != 0) {
            return false;
        }
    }
    return true;
}

static bool tag_can_contain(Tag *self, const Tag *other) {
    TagType child = other->type;

    switch (self->type) {
        case LI:
            return child != LI;

        case DT:
        case DD:
            return child != DT && child != DD;

        case P:
            for (int i = 0; i < 26; i++) {
                if (child == TAG_TYPES_NOT_ALLOWED_IN_PARAGRAPHS[i]) {
                    return false;
                }
            }
            return true;

        case COLGROUP:
            return child == COL;

        case RB:
        case RT:
        case RP:
            return child != RB && child != RT && child != RP;

        case OPTGROUP:
            return child != OPTGROUP;

        case TR:
            return child != TR;

        case TD:
        case TH:
            return child != TD && child != TH && child != TR;

        default:
            return true;
    }
}

enum TokenType {
    START_TAG_NAME,
    SCRIPT_START_TAG_NAME,
    STYLE_START_TAG_NAME,
    END_TAG_NAME,
    ERRONEOUS_END_TAG_NAME,
    SELF_CLOSING_TAG_DELIMITER,
    IMPLICIT_END_TAG,
    RAW_TEXT,
    COMMENT,
    SVELTE_RAW_TEXT,
    SVELTE_RAW_TEXT_EACH,
    SVELTE_RAW_TEXT_SNIPPET_ARGUMENTS,
    AT,
    HASH,
    SLASH,
    COLON,
};

typedef struct {
    Array(Tag) tags;
} Scanner;

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static inline void advance(Lexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(Lexer *lexer) { lexer->advance(lexer, true); }

static unsigned serialize(Scanner *scanner, char *buffer) {
    uint16_t tag_count = scanner->tags.size > UINT16_MAX ? UINT16_MAX : scanner->tags.size;
    uint16_t serialized_tag_count = 0;

    unsigned size = sizeof(tag_count);
    memcpy(&buffer[size], &tag_count, sizeof(tag_count));
    size += sizeof(tag_count);

    for (; serialized_tag_count < tag_count; serialized_tag_count++) {
        Tag tag = scanner->tags.contents[serialized_tag_count];
        if (tag.type == CUSTOM) {
            unsigned name_length = tag.custom_tag_name.size;
            if (name_length > UINT8_MAX) {
                name_length = UINT8_MAX;
            }
            if (size + 2 + name_length >= kSerializationBufferSize) {
                break;
            }
            buffer[size++] = (char)tag.type;
            buffer[size++] = (char)name_length;
            strncpy(&buffer[size], tag.custom_tag_name.contents, name_length);
            size += name_length;
        } else {
            if (size + 1 >= kSerializationBufferSize) {
                break;
            }
            buffer[size++] = (char)tag.type;
        }
    }

    memcpy(&buffer[0], &serialized_tag_count, sizeof(serialized_tag_count));
    return size;
}

static void deserialize(Scanner *scanner, const char *buffer, unsigned length) {
    for (unsigned i = 0; i < scanner->tags.size; i++) {
        tag_free(&scanner->tags.contents[i]);
    }
    array_clear(&scanner->tags);

    if (length > 0) {
        unsigned size = 0;
        uint16_t tag_count = 0;
        uint16_t serialized_tag_count = 0;

        memcpy(&serialized_tag_count, &buffer[size], sizeof(serialized_tag_count));
        size += sizeof(serialized_tag_count);

        memcpy(&tag_count, &buffer[size], sizeof(tag_count));
        size += sizeof(tag_count);

        array_reserve(&scanner->tags, tag_count);
        if (tag_count > 0) {
            unsigned iter = 0;
            for (iter = 0; iter < serialized_tag_count; iter++) {
                Tag tag = tag_new();
                tag.type = (TagType)buffer[size++];
                if (tag.type == CUSTOM) {
                    uint16_t name_length = (uint8_t)buffer[size++];
                    array_reserve(&tag.custom_tag_name, name_length);
                    tag.custom_tag_name.size = name_length;
                    memcpy(tag.custom_tag_name.contents, &buffer[size], name_length);
                    size += name_length;
                }
                array_push(&scanner->tags, tag);
            }
            // add zero tags if we didn't read enough, this is because the
            // buffer had no more room but we held more tags.
            for (; iter < tag_count; iter++) {
                array_push(&scanner->tags, tag_new());
            }
        }
    }
}

static String scan_tag_name(Lexer *lexer) {
    String tag_name = array_new();
    while (iswalnum(lexer->lookahead) || lexer->lookahead == '-' || lexer->lookahead == ':' ||
           lexer->lookahead == '.') {
        // In `tree-sitter-html`, this is where each character is uppercased,
        // but we're preserving the original case. Why?
        //
        // The comparisons for HTML are case-insensitive, since browsers parse
        // HTML tag names in a case-insensitive manner. But Svelte enforces
        // that all usages of plain HTML are in lowercase! Imported Svelte
        // components, on the other hand, must have an initial capital letter.
        //
        // For the purposes of this parser, we'll enforce HTML's rules about
        // containment and void tags only on all-lowercase tag names.
        //
        // There are some hypothetical tag names that we could confidently flag
        // as invalid — element names like `inupt` that fail to meet the naming
        // requirements for both custom elements and Svelte components. We can
        // leave that for later, though.
        array_push(&tag_name, lexer->lookahead);
        advance(lexer);
    }
    return tag_name;
}

static bool scan_comment(Lexer *lexer) {
    if (lexer->lookahead != '-') {
        return false;
    }
    advance(lexer);
    if (lexer->lookahead != '-') {
        return false;
    }
    advance(lexer);

    unsigned dashes = 0;
    while (lexer->lookahead) {
        switch (lexer->lookahead) {
            case '-':
                ++dashes;
                break;
            case '>':
                if (dashes >= 2) {
                    lexer->resultSymbol = COMMENT;
                    advance(lexer);
                    lexer->markEnd(lexer);
                    return true;
                }
                dashes = 0;
                break;
            default:
                dashes = 0;
        }
        advance(lexer);
    }
    return false;
}

static bool scan_javascript_template_string(Lexer *lexer);

static bool scan_javascript_quoted_string(Lexer *lexer, int32_t delimiter);

// After consuming a forward slash and seeing an asterisk immediately after it,
// call this function to advance the lexer to the end of the JavaScript block
// comment.
static bool scan_javascript_block_comment(Lexer *lexer) {
    if (lexer->lookahead != '*') {
        return false;
    }
    advance(lexer);
    while (lexer->lookahead) {
        switch (lexer->lookahead) {
            case '*':
                advance(lexer);
                if (lexer->lookahead == '/') {
                    advance(lexer);
                    return true;
                }
                break;
            default:
                advance(lexer);
        }
    }
    return false;
}

// After consuming a forward slash and seeing another forward slash immediately
// after it, call this function to advance the lexer to the end of the
// JavaScript line comment.
static bool scan_javascript_line_comment(Lexer *lexer) {
    if (lexer->lookahead != '/') {
        return false;
    }
    advance(lexer);
    while (lexer->lookahead) {
        switch (lexer->lookahead) {
            case '\n':
            case '\r':
                advance(lexer);
                return true;
            default:
                advance(lexer);
        }
    }
    return false;
}

// When you see a `{` in front of you in a JavaScript context, call this
// function to scan through until the next balanced (unescaped) brace.
static bool scan_javascript_balanced_brace(Lexer *lexer) {
    if (lexer->lookahead != '{') {
        return false;
    }
    uint8_t brace_level = 0;
    advance(lexer);
    while (lexer->lookahead) {
        switch (lexer->lookahead) {
            case '`':
                scan_javascript_template_string(lexer);
                break;
            case '\\':
                // Escape character. Advance twice.
                advance(lexer);
                advance(lexer);
                break;
            case '\'':
            case '"':
                scan_javascript_quoted_string(lexer, lexer->lookahead);
                break;
            case '{':
                brace_level++;
                advance(lexer);
                break;
            case '}':
                advance(lexer);
                if (brace_level == 0) {
                    return true;
                }
                brace_level--;
                break;
            default:
                advance(lexer);
        }
    }
    return false;
}

// When you see a single or double quote that starts a string, call this
// function to scan through until the end of the quoted string.
static bool scan_javascript_quoted_string(Lexer *lexer, int32_t delimiter) {
    if (lexer->lookahead != delimiter) {
        return false;
    }
    advance(lexer);
    while (lexer->lookahead) {
        switch (lexer->lookahead) {
            case '\\':
                // Escape character. Advance again.
                advance(lexer);
                advance(lexer);
                break;
            default:
                if (lexer->lookahead == delimiter) {
                    advance(lexer);
                    return true;
                }
                advance(lexer);
        }
    }
    return false;
}

// When you see a backtick in a JavaScript context, call this function to scan
// through until the end of the template string.
static bool scan_javascript_template_string(Lexer *lexer) {
    if (lexer->lookahead != '`') {
        return false;
    }
    advance(lexer);
    while (lexer->lookahead) {
        switch (lexer->lookahead) {
            case '$':
                advance(lexer);
                if (lexer->lookahead == '{') {
                    scan_javascript_balanced_brace(lexer);
                }
                break;
            case '\\':
                // Escape character. Advance again.
                advance(lexer);
                advance(lexer);
                break;
            case '`':
                advance(lexer);
                return true;
            default:
                advance(lexer);
        }
    }
    return false;
}

static bool scan_raw_text(Scanner *scanner, Lexer *lexer) {
    if (scanner->tags.size == 0) {
        return false;
    }

    lexer->markEnd(lexer);

    const char *end_delimiter = array_back(&scanner->tags)->type == SCRIPT ? "</SCRIPT" : "</STYLE";

    unsigned delimiter_index = 0;
    while (lexer->lookahead) {
        if ((char)towupper(lexer->lookahead) == end_delimiter[delimiter_index]) {
            delimiter_index++;
            if (delimiter_index == strlen(end_delimiter)) {
                break;
            }
            advance(lexer);
        } else {
            delimiter_index = 0;
            advance(lexer);
            lexer->markEnd(lexer);
        }
    }

    lexer->resultSymbol = RAW_TEXT;
    return true;
}

// Like `scan_svelte_raw_text`, but designed to operate inside the parentheses
// of a `#snippet` definition. Consumes everything until just before the next
// balanced parenthesis.
static bool scan_svelte_raw_text_snippet(Lexer *lexer) {
    while (iswspace(lexer->lookahead)) {
        skip(lexer);
    }
    lexer->resultSymbol = SVELTE_RAW_TEXT_SNIPPET_ARGUMENTS;
    uint8_t paren_level = 0;
    bool advanced_once = false;
    while (!lexer->eof(lexer)) {
        switch (lexer->lookahead) {
            case '/':
                advance(lexer);
                if (lexer->lookahead == '*') {
                    scan_javascript_block_comment(lexer);
                } else if (lexer->lookahead == '/') {
                    scan_javascript_line_comment(lexer);
                }
                break;
            case '\\':
                // Escape mode. Advance again.
                advance(lexer);
                break;
            case '"':
            case '\'':
                // A quoted string is starting. Advance past the end of the
                // closing delimiter.
                scan_javascript_quoted_string(lexer, lexer->lookahead);
                break;
            case '`':
                // A template string is starting. Advance past the end of the
                // closing delimiter.
                scan_javascript_template_string(lexer);
                break;
            case ')':
                if (paren_level == 0) {
                    lexer->markEnd(lexer);
                    return advanced_once;
                }
                advance(lexer);
                paren_level--;
                break;
            case '(':
                advance(lexer);
                paren_level++;
                break;
            default:
                advance(lexer);
                break;
        }
        advanced_once = true;
    }
    return false;
}

static bool scan_svelte_raw_text(Lexer *lexer, const bool *valid_symbols) {
    while (iswspace(lexer->lookahead)) {
        skip(lexer);
    }

    if ((lexer->lookahead == '@' && valid_symbols[AT]) || (lexer->lookahead == '#' && valid_symbols[HASH]) ||
        (lexer->lookahead == ':' && valid_symbols[COLON])) {
        return false;
    }

    // The presence of a special Svelte sigil disqualifies this as a raw text
    // node. This helps us distinguish those nodes from things like `{:else}`.
    bool has_sigil = lexer->lookahead == '@' || lexer->lookahead == '#' || lexer->lookahead == ':';
    if (has_sigil) {
        return false;
    }

    // Keep track of whether we've advanced even once. If we haven't, then that
    // implies we've encountered `{}``, which isn't a valid `svelte_raw_text`
    // node.
    bool advanced_once = false;

    if (lexer->lookahead == '/' && valid_symbols[SLASH]) {
        advance(lexer);
        if (lexer->lookahead == '*') {
            return scan_javascript_block_comment(lexer);
        }
        if (lexer->lookahead != '/') { // JavaScript comment
            return false;
        }

        advanced_once = true;
    }

    lexer->resultSymbol = valid_symbols[SVELTE_RAW_TEXT_EACH] ? SVELTE_RAW_TEXT_EACH : SVELTE_RAW_TEXT;

    uint8_t brace_level = 0;

    // We're searching for a balanced `}`, but along the way we have to
    // consider characters that might put us into contexts for which braces
    // have a different meaning. For instance: a brace inside a comment
    // shouldn't count toward brace balancing, nor should a brace inside of a
    // string.
    while (!lexer->eof(lexer)) {
        switch (lexer->lookahead) {
            case '/':
                advance(lexer);
                advanced_once = true;
                if (lexer->lookahead == '*') {
                    scan_javascript_block_comment(lexer);
                } else if (lexer->lookahead == '/') {
                    scan_javascript_line_comment(lexer);
                }
                break;
            case '\\':
                // Escape mode. Advance again.
                advance(lexer);
                advanced_once = true;
                break;
            case '"':
            case '\'':
                // A quoted string is starting. Advance past the end of the
                // closing delimiter.
                scan_javascript_quoted_string(lexer, lexer->lookahead);
                advanced_once = true;
                break;
            case '`':
                // A template string is starting. Advance past the end of the
                // closing delimiter.
                scan_javascript_template_string(lexer);
                advanced_once = true;
                break;
            case '}':
                if (brace_level == 0) {
                    lexer->markEnd(lexer);
                    return advanced_once;
                }
                advance(lexer);
                brace_level--;
                advanced_once = true;
                break;

            case '{':
                advance(lexer);
                brace_level++;
                advanced_once = true;
                break;

            case 'a':
                if (lexer->resultSymbol == SVELTE_RAW_TEXT_EACH) {
                    lexer->markEnd(lexer);
                    advance(lexer);
                    advanced_once = true;
                    if (lexer->lookahead == 's') {
                        advance(lexer);
                        if (iswspace(lexer->lookahead)) {
                            return advanced_once;
                        }
                    }
                } else {
                    advance(lexer);
                    advanced_once = true;
                }
                break;

            default:
                advance(lexer);
                advanced_once = true;
                break;
        }
    }

    return false;
}

static inline void pop_tag(Scanner *scanner) {
    Tag popped_tag = array_pop(&scanner->tags);
    tag_free(&popped_tag);
}

static bool scan_implicit_end_tag(Scanner *scanner, Lexer *lexer) {
    Tag *parent = scanner->tags.size == 0 ? NULL : array_back(&scanner->tags);

    bool is_closing_tag = false;
    if (lexer->lookahead == '/') {
        is_closing_tag = true;
        advance(lexer);
    } else {
        if (parent && tag_is_void(parent)) {
            pop_tag(scanner);
            lexer->resultSymbol = IMPLICIT_END_TAG;
            return true;
        }
    }

    String tag_name = scan_tag_name(lexer);
    if (tag_name.size == 0 && !lexer->eof(lexer)) {
        array_delete(&tag_name);
        return false;
    }

    Tag next_tag = tag_for_name(tag_name);

    if (is_closing_tag) {
        // The tag correctly closes the topmost element on the stack
        if (scanner->tags.size > 0 && tag_eq(array_back(&scanner->tags), &next_tag)) {
            tag_free(&next_tag);
            return false;
        }

        // Otherwise, dig deeper and queue implicit end tags (to be nice in
        // the case of malformed Svelte)
        for (unsigned i = scanner->tags.size; i > 0; i--) {
            if (scanner->tags.contents[i - 1].type == next_tag.type) {
                pop_tag(scanner);
                lexer->resultSymbol = IMPLICIT_END_TAG;
                tag_free(&next_tag);
                return true;
            }
        }
    } else if (parent &&
               (!tag_can_contain(parent, &next_tag) ||
                ((parent->type == HTML || parent->type == HEAD || parent->type == BODY) && lexer->eof(lexer)))) {
        pop_tag(scanner);
        lexer->resultSymbol = IMPLICIT_END_TAG;
        tag_free(&next_tag);
        return true;
    }

    tag_free(&next_tag);
    return false;
}

static bool scan_start_tag_name(Scanner *scanner, Lexer *lexer) {
    String tag_name = scan_tag_name(lexer);

    if (tag_name.size == 0) {
        array_delete(&tag_name);
        return false;
    }

    Tag tag = tag_for_name(tag_name);
    array_push(&scanner->tags, tag);
    switch (tag.type) {
        case SCRIPT:
            lexer->resultSymbol = SCRIPT_START_TAG_NAME;
            break;
        case STYLE:
            lexer->resultSymbol = STYLE_START_TAG_NAME;
            break;
        default:
            lexer->resultSymbol = START_TAG_NAME;
            break;
    }

    return true;
}

static bool scan_end_tag_name(Scanner *scanner, Lexer *lexer) {
    String tag_name = scan_tag_name(lexer);

    if (tag_name.size == 0) {
        array_delete(&tag_name);
        return false;
    }

    Tag tag = tag_for_name(tag_name);
    if (scanner->tags.size > 0 && tag_eq(array_back(&scanner->tags), &tag)) {
        pop_tag(scanner);
        lexer->resultSymbol = END_TAG_NAME;
    } else {
        lexer->resultSymbol = ERRONEOUS_END_TAG_NAME;
    }

    tag_free(&tag);
    return true;
}

static bool scan_self_closing_tag_delimiter(Scanner *scanner, Lexer *lexer) {
    advance(lexer);
    if (lexer->lookahead == '>') {
        advance(lexer);
        if (scanner->tags.size > 0) {
            pop_tag(scanner);
            lexer->resultSymbol = SELF_CLOSING_TAG_DELIMITER;
        }
        return true;
    }
    return false;
}

static bool scan(Scanner *scanner, Lexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[RAW_TEXT] && !valid_symbols[START_TAG_NAME] && !valid_symbols[END_TAG_NAME]) {
        return scan_raw_text(scanner, lexer);
    }

    if (valid_symbols[SVELTE_RAW_TEXT_SNIPPET_ARGUMENTS]) {
        return scan_svelte_raw_text_snippet(lexer);
    }

    if (valid_symbols[SVELTE_RAW_TEXT] || valid_symbols[SVELTE_RAW_TEXT_EACH]) {
        return scan_svelte_raw_text(lexer, valid_symbols);
    }

    while (iswspace(lexer->lookahead)) {
        skip(lexer);
    }

    switch (lexer->lookahead) {
        case '<':
            lexer->markEnd(lexer);
            advance(lexer);

            if (lexer->lookahead == '!') {
                advance(lexer);
                return scan_comment(lexer);
            }

            if (valid_symbols[IMPLICIT_END_TAG]) {
                return scan_implicit_end_tag(scanner, lexer);
            }
            break;

        case '{':
        case '\0':
            if (valid_symbols[IMPLICIT_END_TAG]) {
                return scan_implicit_end_tag(scanner, lexer);
            }
            break;

        case '/':
            if (valid_symbols[SELF_CLOSING_TAG_DELIMITER]) {
                return scan_self_closing_tag_delimiter(scanner, lexer);
            }
            break;

        default:
            if ((valid_symbols[START_TAG_NAME] || valid_symbols[END_TAG_NAME]) && !valid_symbols[RAW_TEXT]) {
                return valid_symbols[START_TAG_NAME] ? scan_start_tag_name(scanner, lexer)
                                                     : scan_end_tag_name(scanner, lexer);
            }
    }

    return false;
}

void *Create() {
    Scanner *scanner = (Scanner *)scanner_calloc(1, sizeof(Scanner));
    return scanner;
}

static bool Scan(void *payload_, Lexer *lexer, const bool *valid_symbols) {
    VoidPtr payload{payload_};
    Scanner *scanner = (Scanner *)payload;
    return scan(scanner, lexer, valid_symbols);
}

static unsigned Serialize(void *payload_, char *buffer) {
    VoidPtr payload{payload_};
    Scanner *scanner = (Scanner *)payload;
    return serialize(scanner, buffer);
}

static void Deserialize(void *payload_, const char *buffer, unsigned length) {
    VoidPtr payload{payload_};
    Scanner *scanner = (Scanner *)payload;
    deserialize(scanner, buffer, length);
}

static void Destroy(void *payload_) {
    VoidPtr payload{payload_};
    Scanner *scanner = (Scanner *)payload;
    for (unsigned i = 0; i < scanner->tags.size; i++) {
        tag_free(array_get(&scanner->tags, i));
    }
    array_delete(&scanner->tags);
    free(scanner);
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::svelte
