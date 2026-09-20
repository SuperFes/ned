// The astro external scanner, ported from https://github.com/virchau13/tree-sitter-astro (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::astro {

using namespace ned::editor::parse::scanner;

// --- tag.h (from the same grammar) ---

typedef enum {
    AREA,
    BASE,
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
    // Technically not a 'tag' as such, but rather Astro's curly brace
    // HTML interpolations.
    INTERPOLATION,

    // Fragment tags,
    // e.g. <> </>.
    FRAGMENT,

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
    // INTERPOLATION is deliberately excluded from this list,
    // as it has no name.
};

static const TagType TAG_TYPES_NOT_ALLOWED_IN_PARAGRAPHS[] = {
    ADDRESS,  ARTICLE,    ASIDE,  BLOCKQUOTE, DETAILS, DIV, DL,
    FIELDSET, FIGCAPTION, FIGURE, FOOTER,     FORM,    H1,  H2,
    H3,       H4,         H5,     H6,         HEADER,  HR,  MAIN,
    NAV,      OL,         P,      PRE,        SECTION,
};

static TagType tag_type_for_name(const String *tag_name) {
    for (int i = 0; i < 126; i++) {
        const TagMapEntry *entry = &TAG_TYPES_BY_TAG_NAME[i];
        if (
            strlen(entry->tag_name) == tag_name->size &&
            memcmp(tag_name->contents, entry->tag_name, tag_name->size) == 0
        ) {
            return entry->tag_type;
        }
    }
    return CUSTOM;
}

static inline Tag tag_new() {
    Tag tag;
    tag.type = END_;
    tag.custom_tag_name = (String) array_new();
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

static inline void tag_free(Tag *tag) {
    if (tag->type == CUSTOM) {
        array_delete(&tag->custom_tag_name);
    }
}

static inline bool tag_is_void(const Tag *self) {
    return self->type < END_OF_VOID_TAGS;
}

static inline bool tag_eq(const Tag *self, const Tag *other) {
    if (self->type != other->type) return false;
    if (self->type == CUSTOM) {
        if (self->custom_tag_name.size != other->custom_tag_name.size) {
            return false;
        }
        if (memcmp(
            self->custom_tag_name.contents,
            other->custom_tag_name.contents,
            self->custom_tag_name.size
        ) != 0) {
            return false;
        }
    }
    return true;
}

static bool tag_can_contain(Tag *self, const Tag *other) {
    TagType child = other->type;

    if (child == INTERPOLATION) {
        // can be contained anywhere
        return true;
    }

    switch (self->type) {
        case LI:
            return child != LI;

        case DT:
        case DD:
            return child != DT && child != DD;

        case P:
            for (int i = 0; i < sizeof(TAG_TYPES_NOT_ALLOWED_IN_PARAGRAPHS) /
                                    sizeof(TagType);
                 i++) {
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
    HTML_INTERPOLATION_START,
    HTML_INTERPOLATION_END,
    FRONTMATTER_JS_BLOCK,
    ATTRIBUTE_JS_EXPR,
    ATTRIBUTE_BACKTICK_STRING,
    PERMISSIBLE_TEXT,
    FRAGMENT_TAG_DELIM,
};

typedef struct {
    Array(Tag) tags;
} Scanner;

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define IS_ASCII_ALPHA(e) (('a' <= (e) && (e) <= 'z') || ('A' <= (e) && (e) <= 'Z'))

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
            buffer[size++] = tag.type;
            buffer[size++] = (char)name_length;
            strncpy(&buffer[size], tag.custom_tag_name.contents, name_length);
            size += name_length;
        } else {
            if (size + 1 >= kSerializationBufferSize) {
                break;
            }
            buffer[size++] = tag.type;
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
                    if (name_length > 0) {
                        array_reserve(&tag.custom_tag_name, name_length);
                        memcpy(tag.custom_tag_name.contents, &buffer[size], name_length);
                    }
                    tag.custom_tag_name.size = name_length;
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
    while (iswalnum(lexer->lookahead) || 
            lexer->lookahead == '-' || 
            lexer->lookahead == ':' || lexer->lookahead == '.') {
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
            default:
                dashes = 0;
        }
        advance(lexer);
    }
    return false;
}

enum RawTextEndType { 
    // corresponds to the ending delimiter "\n---"
    EndFrontmatter, 
    // corresponds to the ending delimiter "}",
    // used for JS backtick strings and attribute interpolations.
    // we have to balance brackets with this one.
    EndCurly 
};

static inline void scan_js_expr_with_delimiter(Lexer *lexer, enum RawTextEndType end_type);

static inline void scan_js_backtick_string(Lexer *lexer) {
    // Advance past backtick
    lexer->advance(lexer, false);
    while (lexer->lookahead != '\0') {
        if (lexer->lookahead == '$') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == '{') {
                // String interpolation
                lexer->advance(lexer, false);
                scan_js_expr_with_delimiter(lexer, EndCurly);
                // Advance past the final curly
            } else {
                // Reprocess this character
                continue;
            }
        } else if (lexer->lookahead == '`') {
            // End of string
            lexer->advance(lexer, false);
            break;
        }
        lexer->advance(lexer, false);
    }
}

static inline void scan_js_string(Lexer *lexer) {
    // Assumes the lookahead char is actually valid
    if (lexer->lookahead == '`') {
        scan_js_backtick_string(lexer);
    } else {
        // Start and end chars are the same
        char str_end_char = (char)lexer->lookahead;
        lexer->advance(lexer, false);
        while (lexer->lookahead != '\0') {
            // Note that this doesn't take into account newlines in basic
            // strings, for the same reason why tree-sitter-javascript
            // doesn't.
            // https://github.com/tree-sitter/tree-sitter-javascript/blob/fdeb68ac8d2bd5a78b943528bb68ceda3aade2eb/grammar.js#L860
            if (lexer->lookahead == '\\') {
                // Accept any next char
                lexer->advance(lexer, false);
            } else if (lexer->lookahead == str_end_char) {
                // End of string
                lexer->advance(lexer, false);
                return;
            }
            lexer->advance(lexer, false);
        }
    }
}

static inline void scan_js_expr_with_delimiter(Lexer *lexer, enum RawTextEndType end_type) {
    lexer->markEnd(lexer);
    // `delimiter_index` is only used when `end_type == EndFrontmatter`.
    // We assign "1" here because we only enter this function when "---\n" is parsed by tree-sitter,
    // but tree-sitter leaves out the extra newline when giving the lexer to us.
    // "1" reflects the true delimiter status.
    // (This ensures parsing empty delimiters correctly.)
    unsigned delimiter_index = 1;
    unsigned curly_count = 0;

    enum CommentState { NotInComment, SingleLine, MultiLine };
    enum CommentState in_comment = NotInComment;

    while (lexer->lookahead != '\0') {
        if (in_comment == NotInComment) {
            // delimiter check
            if (end_type == EndFrontmatter) {
                // We have to `mark_end` at index 0, always,
                // so just pre-emptively do it here.
                if(delimiter_index == 0) {
                    lexer->markEnd(lexer);
                }
                char const * const END = "\n---";
                if (lexer->lookahead == END[delimiter_index]) {
                    delimiter_index++;
                    if (delimiter_index == strlen(END)) {
                        break;
                    }
                } else {
                    // In case we stumble into a newline. This could happen if e.g.
                    // ---
                    // -
                    // ---
                    // was the frontmatter.
                    lexer->markEnd(lexer);
                    delimiter_index = (lexer->lookahead == '\n' ? 1 : 0);
                }
            } else if (end_type == EndCurly) {
                lexer->markEnd(lexer);
                // balance braces
                if (lexer->lookahead == '{') {
                    curly_count++;
                } else if (lexer->lookahead == '}') {
                    if (curly_count == 0) {
                        // delimiter, break
                        lexer->markEnd(lexer);
                        break;
                    }
                    curly_count--;
                }
            }
            if (lexer->lookahead == '"' || lexer->lookahead == '\'' ||
                lexer->lookahead == '`') {
                scan_js_string(lexer);
                continue;
            }
            if (lexer->lookahead == '/') {
                // comment?
                lexer->advance(lexer, false);
                if (lexer->lookahead == '/') {
                    in_comment = SingleLine;
                } else if (lexer->lookahead == '*') {
                    in_comment = MultiLine;
                }
                continue;
            }
        } else if (in_comment == SingleLine) {
            if (lexer->lookahead == '\n') {
                // End of comment
                in_comment = NotInComment;
                // Frontmatter delimiters start with 1 newline.
                delimiter_index = (end_type == EndFrontmatter ? 1 : 0);
                // `mark_end` here ensures that we don't accidentally skip a frontmatter delimiter start point.
                // If this was left out, delimiters of the form
                // ---
                // // comment
                // ---
                // would never `mark_end` before the delimiter, causing the returned token to be truncated in size.
                lexer->markEnd(lexer);
            }
        } else if (in_comment == MultiLine) {
            if (lexer->lookahead == '*') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == '/') {
                    // End of comment
                    in_comment = NotInComment;
                    delimiter_index = 0;
                } else {
                    continue;
                }
            }
        }
        lexer->advance(lexer, false);
    }
}

static bool scan_raw_text(Scanner *scanner, Lexer *lexer) {
    lexer->markEnd(lexer);

    const char *end_delimiter = array_back(&scanner->tags)->type == SCRIPT ? "</script" : "</style";

    unsigned delimiter_index = 0;
    while (lexer->lookahead) {
        // TODO add test for uppercase SCRIPT not conflicting with lowercase script
        if (lexer->lookahead == end_delimiter[delimiter_index]) {
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

static void pop_tag(Scanner *scanner) {
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
        // the case of malformed Astro)
        for (unsigned i = scanner->tags.size; i > 0; i--) {
            if (scanner->tags.contents[i - 1].type == next_tag.type) {
                pop_tag(scanner);
                lexer->resultSymbol = IMPLICIT_END_TAG;
                tag_free(&next_tag);
                return true;
            }
        }
    } else if (
        parent &&
        (
            !tag_can_contain(parent, &next_tag) ||
            ((parent->type == HTML || parent->type == HEAD || parent->type == BODY) && lexer->eof(lexer))
        )
    ) {
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
        // Fragment tags don't contain spaces.
        if (lexer->lookahead == '>') {
            advance(lexer);
            Tag tag = {.type = FRAGMENT, .custom_tag_name = {0}};
            array_push(&scanner->tags, tag);
            lexer->resultSymbol = FRAGMENT_TAG_DELIM;
            return true;
        } else {
            return false;
        }
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
        if (lexer->lookahead == '>') {
            advance(lexer);
            if (scanner->tags.size > 0 && array_back(&scanner->tags)->type == FRAGMENT) {
                pop_tag(scanner);
                lexer->resultSymbol = FRAGMENT_TAG_DELIM;
                return true;
            } else {
                lexer->resultSymbol = ERRONEOUS_END_TAG_NAME;
                return true;
            }
        } else {
            // Closing fragment tags don't have spaces.
            return false;
        }
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

static bool scan_permissible_text(Lexer *lexer) {
    bool there_is_text = false;

    while (lexer->lookahead != '\0') {
        if(lexer->lookahead == '{' || lexer->lookahead == '}') {
            // Start of interpolation / end of interpolation, break
            break;
        }
        if(lexer->lookahead == '\'' || lexer->lookahead == '"' || lexer->lookahead == '`') {
            // skip string
            scan_js_string(lexer);
            there_is_text = true;
            goto text_found;
        }
        if(lexer->lookahead == '/') {
            // check for a comment
            // (c.f. https://github.com/withastro/compiler/blob/e8b6cdfc89f940a411304787632efd8140535feb/internal/token.go#L1017)
            advance(lexer);
            there_is_text = true;
            if (lexer->lookahead == '/') {
                // single-line comment
                while(lexer->lookahead != '\r' && lexer->lookahead != '\n' && lexer->lookahead != '\0') {
                    advance(lexer);
                }
            }
            if (lexer->lookahead == '*') {
                // multiline comment
                while (lexer->lookahead != '\0') {
                    advance(lexer);
                    if (lexer->lookahead == '*') {
                        advance(lexer);
                        if (lexer->lookahead == '/') {
                            // end of multi-line comment
                            advance(lexer);
                            break;
                        }
                    }
                }
            }
            goto text_found;
        }
        if (lexer->lookahead == '<') {
            advance(lexer);
            // This is the same logic the Astro compiler uses
            // to find when to terminate a text node.
            // https://github.com/withastro/compiler/blob/e8b6cdfc89f940a411304787632efd8140535feb/internal/token.go#L1737
            if (IS_ASCII_ALPHA(lexer->lookahead)) {
                // Potential <start> tag
                break;
            }
            if (lexer->lookahead == '/') {
                // Potential </end> tag
                break;
            }
            if (lexer->lookahead == '/' || lexer->lookahead == '?') {
                // Potential <!-- comment --> tag
                // or <!DOCTYPE ...> tag
                // or <?xml processing instructions?> tag
                break;
            }
            if (lexer->lookahead == '>') {
                // Fragment tag
                // (e.g. `<> <p> hi </p> </>`)
                break;
            }
            // If none of the above conditions pass,
            // there's definitely text here.
            goto text_found;
        } 
        advance(lexer);
text_found:
        there_is_text = true;
        lexer->markEnd(lexer);
    }

    // If there isn't any text,
    // then this can't be permissible text.
    if (there_is_text) {
        lexer->resultSymbol = PERMISSIBLE_TEXT;
        return true;
    } else {
        return false;
    }
}

static bool scan(Scanner *scanner, Lexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[FRONTMATTER_JS_BLOCK] && scanner->tags.size == 0) {
        scan_js_expr_with_delimiter(lexer, EndFrontmatter);
        lexer->resultSymbol = FRONTMATTER_JS_BLOCK;
        return true;
    }

    if (valid_symbols[RAW_TEXT] && !valid_symbols[START_TAG_NAME] && !valid_symbols[END_TAG_NAME]) {
        return scan_raw_text(scanner, lexer);
    }

    if (valid_symbols[ATTRIBUTE_JS_EXPR]) {
        scan_js_expr_with_delimiter(lexer, EndCurly);
        lexer->resultSymbol = ATTRIBUTE_JS_EXPR;
        return true;
    }

    if (valid_symbols[PERMISSIBLE_TEXT]) {
        if(iswspace(lexer->lookahead)) {
            // Can't be anything else.
            return scan_permissible_text(lexer);
        }
    } else {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }
    }

    bool definitely_not_permissible_text = false;

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

            if (valid_symbols[PERMISSIBLE_TEXT]) {
                bool invalid = 
                    IS_ASCII_ALPHA(lexer->lookahead)
                    || lexer->lookahead == '/'
                    || lexer->lookahead == '?'
                    || lexer->lookahead == '>';
                if (invalid) {
                    // This looks like an element,
                    // so it can't be permissible text.
                    definitely_not_permissible_text = true;
                }
            }

            break;

        case '\0':
            definitely_not_permissible_text = true;
            if (valid_symbols[IMPLICIT_END_TAG]) {
                return scan_implicit_end_tag(scanner, lexer);
            }
            break;

        case '/':
            if (valid_symbols[SELF_CLOSING_TAG_DELIMITER]) {
                return scan_self_closing_tag_delimiter(scanner, lexer);
            }
            break;

        case '{':
            if (valid_symbols[HTML_INTERPOLATION_START]) {
                lexer->advance(lexer, false);
                Tag tag = (Tag){INTERPOLATION, {0}};
                array_push(&scanner->tags, tag);
                lexer->resultSymbol = HTML_INTERPOLATION_START;
                return true;
            }
            break;
        case '}':
            // Close any void tags before exiting the interpolation node.
            if (valid_symbols[IMPLICIT_END_TAG]) {
                return scan_implicit_end_tag(scanner, lexer);
            }

            if (valid_symbols[HTML_INTERPOLATION_END] &&
                    scanner->tags.size > 0 &&
                    array_back(&scanner->tags)->type == INTERPOLATION) {
                lexer->advance(lexer, false);
                array_pop(&scanner->tags);
                lexer->resultSymbol = HTML_INTERPOLATION_END;
                return true;
            }
            break;

        case '`':
            if (valid_symbols[ATTRIBUTE_BACKTICK_STRING]) {
                scan_js_backtick_string(lexer);
                lexer->markEnd(lexer);
                lexer->resultSymbol = ATTRIBUTE_BACKTICK_STRING;
                return true;
            }
            break;

        default:
            if ((valid_symbols[START_TAG_NAME] || valid_symbols[END_TAG_NAME]) && !valid_symbols[RAW_TEXT]) {
                return valid_symbols[START_TAG_NAME] ? scan_start_tag_name(scanner, lexer)
                                                     : scan_end_tag_name(scanner, lexer);
            }
    }

    if (!definitely_not_permissible_text && valid_symbols[PERMISSIBLE_TEXT]) {
        // There are no other choices, it's this or nothing.
        return scan_permissible_text(lexer);
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
        tag_free(&scanner->tags.contents[i]);
    }
    array_delete(&scanner->tags);
    free(scanner);
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::astro
