// The html external scanner, ported from https://github.com/tree-sitter/tree-sitter-html (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::html {

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
    char    tag_name[16];
    TagType tag_type;
} TagMapEntry;

typedef struct {
    TagType type;
    String  custom_tag_name;
} Tag;

static const TagMapEntry TAG_TYPES_BY_TAG_NAME[126] = {
    {"AREA", AREA},
    {"BASE", BASE},
    {"BASEFONT", BASEFONT},
    {"BGSOUND", BGSOUND},
    {"BR", BR},
    {"COL", COL},
    {"COMMAND", COMMAND},
    {"EMBED", EMBED},
    {"FRAME", FRAME},
    {"HR", HR},
    {"IMAGE", IMAGE},
    {"IMG", IMG},
    {"INPUT", INPUT},
    {"ISINDEX", ISINDEX},
    {"KEYGEN", KEYGEN},
    {"LINK", LINK},
    {"MENUITEM", MENUITEM},
    {"META", META},
    {"NEXTID", NEXTID},
    {"PARAM", PARAM},
    {"SOURCE", SOURCE},
    {"TRACK", TRACK},
    {"WBR", WBR},
    {"A", A},
    {"ABBR", ABBR},
    {"ADDRESS", ADDRESS},
    {"ARTICLE", ARTICLE},
    {"ASIDE", ASIDE},
    {"AUDIO", AUDIO},
    {"B", B},
    {"BDI", BDI},
    {"BDO", BDO},
    {"BLOCKQUOTE", BLOCKQUOTE},
    {"BODY", BODY},
    {"BUTTON", BUTTON},
    {"CANVAS", CANVAS},
    {"CAPTION", CAPTION},
    {"CITE", CITE},
    {"CODE", CODE},
    {"COLGROUP", COLGROUP},
    {"DATA", DATA},
    {"DATALIST", DATALIST},
    {"DD", DD},
    {"DEL", DEL},
    {"DETAILS", DETAILS},
    {"DFN", DFN},
    {"DIALOG", DIALOG},
    {"DIV", DIV},
    {"DL", DL},
    {"DT", DT},
    {"EM", EM},
    {"FIELDSET", FIELDSET},
    {"FIGCAPTION", FIGCAPTION},
    {"FIGURE", FIGURE},
    {"FOOTER", FOOTER},
    {"FORM", FORM},
    {"H1", H1},
    {"H2", H2},
    {"H3", H3},
    {"H4", H4},
    {"H5", H5},
    {"H6", H6},
    {"HEAD", HEAD},
    {"HEADER", HEADER},
    {"HGROUP", HGROUP},
    {"HTML", HTML},
    {"I", I},
    {"IFRAME", IFRAME},
    {"INS", INS},
    {"KBD", KBD},
    {"LABEL", LABEL},
    {"LEGEND", LEGEND},
    {"LI", LI},
    {"MAIN", MAIN},
    {"MAP", MAP},
    {"MARK", MARK},
    {"MATH", MATH},
    {"MENU", MENU},
    {"METER", METER},
    {"NAV", NAV},
    {"NOSCRIPT", NOSCRIPT},
    {"OBJECT", OBJECT},
    {"OL", OL},
    {"OPTGROUP", OPTGROUP},
    {"OPTION", OPTION},
    {"OUTPUT", OUTPUT},
    {"P", P},
    {"PICTURE", PICTURE},
    {"PRE", PRE},
    {"PROGRESS", PROGRESS},
    {"Q", Q},
    {"RB", RB},
    {"RP", RP},
    {"RT", RT},
    {"RTC", RTC},
    {"RUBY", RUBY},
    {"S", S},
    {"SAMP", SAMP},
    {"SCRIPT", SCRIPT},
    {"SECTION", SECTION},
    {"SELECT", SELECT},
    {"SLOT", SLOT},
    {"SMALL", SMALL},
    {"SPAN", SPAN},
    {"STRONG", STRONG},
    {"STYLE", STYLE},
    {"SUB", SUB},
    {"SUMMARY", SUMMARY},
    {"SUP", SUP},
    {"SVG", SVG},
    {"TABLE", TABLE},
    {"TBODY", TBODY},
    {"TD", TD},
    {"TEMPLATE", TEMPLATE},
    {"TEXTAREA", TEXTAREA},
    {"TFOOT", TFOOT},
    {"TH", TH},
    {"THEAD", THEAD},
    {"TIME", TIME},
    {"TITLE", TITLE},
    {"TR", TR},
    {"U", U},
    {"UL", UL},
    {"VAR", VAR},
    {"VIDEO", VIDEO},
    {"CUSTOM", CUSTOM},
};

static const TagType TAG_TYPES_NOT_ALLOWED_IN_PARAGRAPHS[] = {
    ADDRESS,
    ARTICLE,
    ASIDE,
    BLOCKQUOTE,
    DETAILS,
    DIV,
    DL,
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
    HEADER,
    HR,
    MAIN,
    NAV,
    OL,
    P,
    PRE,
    SECTION,
};

static TagType tag_type_for_name(const String* tag_name) {
    for (int i = 0; i < 126; i++) {
        const TagMapEntry* entry = &TAG_TYPES_BY_TAG_NAME[i];
        if (
            strlen(entry->tag_name) == tag_name->size &&
            memcmp(tag_name->contents, entry->tag_name, tag_name->size) == 0) {
            return entry->tag_type;
        }
    }
    return CUSTOM;
}

static inline Tag tag_new() {
    Tag tag;
    tag.type            = END_;
    tag.custom_tag_name = (String)array_new();
    return tag;
}

static inline Tag tag_for_name(String name) {
    Tag tag  = tag_new();
    tag.type = tag_type_for_name(&name);
    if (tag.type == CUSTOM) {
        tag.custom_tag_name = name;
    }
    else {
        array_delete(&name);
    }
    return tag;
}

static inline void tag_free(Tag* tag) {
    if (tag->type == CUSTOM) {
        array_delete(&tag->custom_tag_name);
    }
}

static inline bool tag_is_void(const Tag* self) {
    return self->type < END_OF_VOID_TAGS;
}

static inline bool tag_eq(const Tag* self, const Tag* other) {
    if (self->type != other->type)
        return false;
    if (self->type == CUSTOM) {
        if (self->custom_tag_name.size != other->custom_tag_name.size) {
            return false;
        }
        if (memcmp(
                self->custom_tag_name.contents,
                other->custom_tag_name.contents,
                self->custom_tag_name.size) != 0) {
            return false;
        }
    }
    return true;
}

static bool tag_can_contain(Tag* self, const Tag* other) {
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
};

typedef struct {
    Array(Tag) tags;
} Scanner;

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static inline void advance(Lexer* lexer) {
    lexer->advance(lexer, false);
}

static inline void skip(Lexer* lexer) {
    lexer->advance(lexer, true);
}

static unsigned serialize(Scanner* scanner, char* buffer) {
    uint16_t tag_count            = scanner->tags.size > UINT16_MAX ? UINT16_MAX : scanner->tags.size;
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
        }
        else {
            if (size + 1 >= kSerializationBufferSize) {
                break;
            }
            buffer[size++] = (char)tag.type;
        }
    }

    memcpy(&buffer[0], &serialized_tag_count, sizeof(serialized_tag_count));
    return size;
}

static void deserialize(Scanner* scanner, const char* buffer, unsigned length) {
    for (unsigned i = 0; i < scanner->tags.size; i++) {
        tag_free(&scanner->tags.contents[i]);
    }
    array_clear(&scanner->tags);

    if (length > 0) {
        unsigned size                 = 0;
        uint16_t tag_count            = 0;
        uint16_t serialized_tag_count = 0;

        memcpy(&serialized_tag_count, &buffer[size], sizeof(serialized_tag_count));
        size += sizeof(serialized_tag_count);

        memcpy(&tag_count, &buffer[size], sizeof(tag_count));
        size += sizeof(tag_count);

        array_reserve(&scanner->tags, tag_count);
        if (tag_count > 0) {
            unsigned iter = 0;
            for (iter = 0; iter < serialized_tag_count; iter++) {
                Tag tag  = tag_new();
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

static String scan_tag_name(Lexer* lexer) {
    String tag_name = array_new();
    while (iswalnum(lexer->lookahead) || lexer->lookahead == '-' || lexer->lookahead == ':') {
        array_push(&tag_name, towupper(lexer->lookahead));
        advance(lexer);
    }
    return tag_name;
}

static bool scan_comment(Lexer* lexer) {
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

static bool scan_raw_text(Scanner* scanner, Lexer* lexer) {
    if (scanner->tags.size == 0) {
        return false;
    }

    lexer->markEnd(lexer);

    const char* end_delimiter = array_back(&scanner->tags)->type == SCRIPT ? "</SCRIPT" : "</STYLE";

    unsigned delimiter_index = 0;
    while (lexer->lookahead) {
        if (towupper(lexer->lookahead) == end_delimiter[delimiter_index]) {
            delimiter_index++;
            if (delimiter_index == strlen(end_delimiter)) {
                break;
            }
            advance(lexer);
        }
        else {
            delimiter_index = 0;
            advance(lexer);
            lexer->markEnd(lexer);
        }
    }

    lexer->resultSymbol = RAW_TEXT;
    return true;
}

static void pop_tag(Scanner* scanner) {
    Tag popped_tag = array_pop(&scanner->tags);
    tag_free(&popped_tag);
}

static bool scan_implicit_end_tag(Scanner* scanner, Lexer* lexer) {
    Tag* parent = scanner->tags.size == 0 ? NULL : array_back(&scanner->tags);

    bool is_closing_tag = false;
    if (lexer->lookahead == '/') {
        is_closing_tag = true;
        advance(lexer);
    }
    else {
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
        // the case of malformed HTML)
        for (unsigned i = scanner->tags.size; i > 0; i--) {
            if (scanner->tags.contents[i - 1].type == next_tag.type) {
                pop_tag(scanner);
                lexer->resultSymbol = IMPLICIT_END_TAG;
                tag_free(&next_tag);
                return true;
            }
        }
    }
    else if (
        parent &&
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

static bool scan_start_tag_name(Scanner* scanner, Lexer* lexer) {
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

static bool scan_end_tag_name(Scanner* scanner, Lexer* lexer) {
    String tag_name = scan_tag_name(lexer);

    if (tag_name.size == 0) {
        array_delete(&tag_name);
        return false;
    }

    Tag tag = tag_for_name(tag_name);
    if (scanner->tags.size > 0 && tag_eq(array_back(&scanner->tags), &tag)) {
        pop_tag(scanner);
        lexer->resultSymbol = END_TAG_NAME;
    }
    else {
        lexer->resultSymbol = ERRONEOUS_END_TAG_NAME;
    }

    tag_free(&tag);
    return true;
}

static bool scan_self_closing_tag_delimiter(Scanner* scanner, Lexer* lexer) {
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

static bool scan(Scanner* scanner, Lexer* lexer, const bool* valid_symbols) {
    if (valid_symbols[RAW_TEXT] && !valid_symbols[START_TAG_NAME] && !valid_symbols[END_TAG_NAME]) {
        return scan_raw_text(scanner, lexer);
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

void* Create() {
    Scanner* scanner = (Scanner*)scanner_calloc(1, sizeof(Scanner));
    return scanner;
}

static bool Scan(void* payload_, Lexer* lexer, const bool* valid_symbols) {
    VoidPtr  payload{payload_};
    Scanner* scanner = (Scanner*)payload;
    return scan(scanner, lexer, valid_symbols);
}

static unsigned Serialize(void* payload_, char* buffer) {
    VoidPtr  payload{payload_};
    Scanner* scanner = (Scanner*)payload;
    return serialize(scanner, buffer);
}

static void Deserialize(void* payload_, const char* buffer, unsigned length) {
    VoidPtr  payload{payload_};
    Scanner* scanner = (Scanner*)payload;
    deserialize(scanner, buffer, length);
}

static void Destroy(void* payload_) {
    VoidPtr  payload{payload_};
    Scanner* scanner = (Scanner*)payload;
    for (unsigned i = 0; i < scanner->tags.size; i++) {
        tag_free(&scanner->tags.contents[i]);
    }
    array_delete(&scanner->tags);
    free(scanner);
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::html

NED_TREE_SITTER_SCANNER_EXPORTS(html, ned::editor::languages::scanners::html)
