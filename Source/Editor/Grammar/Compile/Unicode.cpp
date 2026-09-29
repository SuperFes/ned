#include "Unicode.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Editor/Grammar/Compile/UnicodeTables.h"

namespace ned::editor::grammar::compile::unicode {

namespace {

    CharacterSet FromTable(const std::uint32_t* table, std::size_t count) {
        CharacterSet set;
        for (std::size_t i = 0; i < count; ++i)
            set = set.AddRange(table[2 * i], table[2 * i + 1] - 1);
        return set;
    }

    std::optional<Category> CategoryFor(std::string_view name) {
        static const std::unordered_map<std::string_view, Category> kCategories = {
            {"Lu", Category::Lu},
            {"Ll", Category::Ll},
            {"Lt", Category::Lt},
            {"Lm", Category::Lm},
            {"Lo", Category::Lo},
            {"Mn", Category::Mn},
            {"Mc", Category::Mc},
            {"Me", Category::Me},
            {"Nd", Category::Nd},
            {"Nl", Category::Nl},
            {"No", Category::No},
            {"Pc", Category::Pc},
            {"Pd", Category::Pd},
            {"Ps", Category::Ps},
            {"Pe", Category::Pe},
            {"Pi", Category::Pi},
            {"Pf", Category::Pf},
            {"Po", Category::Po},
            {"Sm", Category::Sm},
            {"Sc", Category::Sc},
            {"Sk", Category::Sk},
            {"So", Category::So},
            {"Zs", Category::Zs},
            {"Zl", Category::Zl},
            {"Zp", Category::Zp},
            {"Cc", Category::Cc},
            {"Cf", Category::Cf},
            {"Cs", Category::Cs},
            {"Co", Category::Co},
            {"Cn", Category::Cn},
        };
        const auto it = kCategories.find(name);
        return it == kCategories.end() ? std::nullopt : std::optional(it->second);
    }

    // The long names regex syntaxes accept for categories.
    std::string_view CanonicalCategory(std::string_view name) {
        static const std::unordered_map<std::string_view, std::string_view> kAliases = {
            {"Letter", "L"},
            {"Uppercase_Letter", "Lu"},
            {"Lowercase_Letter", "Ll"},
            {"Titlecase_Letter", "Lt"},
            {"Modifier_Letter", "Lm"},
            {"Other_Letter", "Lo"},
            {"Mark", "M"},
            {"Nonspacing_Mark", "Mn"},
            {"Spacing_Mark", "Mc"},
            {"Enclosing_Mark", "Me"},
            {"Number", "N"},
            {"Decimal_Number", "Nd"},
            {"Letter_Number", "Nl"},
            {"Other_Number", "No"},
            {"Punctuation", "P"},
            {"Connector_Punctuation", "Pc"},
            {"Dash_Punctuation", "Pd"},
            {"Open_Punctuation", "Ps"},
            {"Close_Punctuation", "Pe"},
            {"Initial_Punctuation", "Pi"},
            {"Final_Punctuation", "Pf"},
            {"Other_Punctuation", "Po"},
            {"Symbol", "S"},
            {"Math_Symbol", "Sm"},
            {"Currency_Symbol", "Sc"},
            {"Modifier_Symbol", "Sk"},
            {"Other_Symbol", "So"},
            {"Separator", "Z"},
            {"Space_Separator", "Zs"},
            {"Line_Separator", "Zl"},
            {"Paragraph_Separator", "Zp"},
            {"Other", "C"},
            {"Control", "Cc"},
            {"Format", "Cf"},
            {"Surrogate", "Cs"},
            {"Private_Use", "Co"},
            {"Unassigned", "Cn"},
        };
        const auto it = kAliases.find(name);
        return it == kAliases.end() ? name : it->second;
    }

    Category CategoryOf(std::uint32_t c) {
        const std::uint32_t* const end  = kCategoryRunStart + kCategoryRunStartCount;
        const std::uint32_t* const next = std::upper_bound(kCategoryRunStart, end, c);
        return static_cast<Category>(kCategoryRunValue[next - kCategoryRunStart - 1]);
    }

    // Member runs that touch are joined into one range, whichever of
    // `categories` each has.
    CharacterSet ScanCategories(const std::vector<Category>& categories) {
        CharacterSet  set;
        std::uint32_t runStart = 0;
        bool          inRun    = false;
        for (std::size_t i = 0; i < kCategoryRunStartCount; ++i) {
            const auto start  = kCategoryRunStart[i];
            const bool member = std::find(categories.begin(), categories.end(), static_cast<Category>(kCategoryRunValue[i])) !=
                                categories.end();
            if (member && !inRun) {
                runStart = start;
                inRun    = true;
            }
            else if (!member && inRun) {
                set   = set.AddRange(runStart, start - 1);
                inRun = false;
            }
        }
        if (inRun)
            set = set.AddRange(runStart, kCodepointEnd - 1);
        return set;
    }

    std::mutex                                    g_cacheMutex;
    std::unordered_map<std::string, CharacterSet> g_propertyCache;

    // Simple case folding as an equivalence: two characters share an orbit
    // when their simple lower/upper/title mappings connect them. Built once
    // over the whole codepoint space.
    class FoldOrbits {
      public:
        FoldOrbits() {
            parent_.resize(kCodepointEnd);
            for (std::uint32_t c = 0; c < kCodepointEnd; ++c)
                parent_[c] = c;
            for (std::size_t i = 0; i < kCaseMappingsCount; ++i) {
                const std::uint32_t* const mapping = kCaseMappings + 4 * i;
                Join(mapping[0], mapping[1]);
                Join(mapping[0], mapping[2]);
                Join(mapping[0], mapping[3]);
            }
            members_.resize(kCodepointEnd);
            for (std::uint32_t c = 0; c < kCodepointEnd; ++c)
                members_[Find(c)].push_back(c);
        }

        const std::vector<std::uint32_t>& Orbit(std::uint32_t c) {
            return members_[Find(c)];
        }

      private:
        std::uint32_t Find(std::uint32_t c) {
            while (parent_[c] != c) {
                parent_[c] = parent_[parent_[c]];
                c          = parent_[c];
            }
            return c;
        }
        void Join(std::uint32_t a, std::uint32_t b) {
            if (b >= kCodepointEnd)
                return;
            a = Find(a);
            b = Find(b);
            if (a != b)
                parent_[std::max(a, b)] = std::min(a, b);
        }

        std::vector<std::uint32_t>              parent_;
        std::vector<std::vector<std::uint32_t>> members_;
    };

    FoldOrbits& Orbits() {
        static FoldOrbits orbits;
        return orbits;
    }

} // namespace

std::optional<CharacterSet> Property(std::string_view name) {
    {
        const std::lock_guard<std::mutex> lock(g_cacheMutex);
        if (const auto it = g_propertyCache.find(std::string(name)); it != g_propertyCache.end())
            return it->second;
    }

    std::optional<CharacterSet> result;
    if (name == "XID_Start")
        result = FromTable(kXidStart, kXidStartCount);
    else if (name == "XID_Continue")
        result = FromTable(kXidContinue, kXidContinueCount);
    else if (name == "ID_Start")
        result = FromTable(kIdStart, kIdStartCount);
    else if (name == "ID_Continue")
        result = FromTable(kIdContinue, kIdContinueCount);
    // The emoji properties, with the short aliases the reference's regex
    // crate accepts (PropertyAliases.txt).
    else if (name == "Emoji")
        result = FromTable(kEmoji, kEmojiCount);
    else if (name == "Emoji_Presentation" || name == "EPres")
        result = FromTable(kEmojiPresentation, kEmojiPresentationCount);
    else if (name == "Emoji_Modifier" || name == "EMod")
        result = FromTable(kEmojiModifier, kEmojiModifierCount);
    else if (name == "Emoji_Modifier_Base" || name == "EBase")
        result = FromTable(kEmojiModifierBase, kEmojiModifierBaseCount);
    else if (name == "Emoji_Component" || name == "EComp")
        result = FromTable(kEmojiComponent, kEmojiComponentCount);
    else if (name == "Extended_Pictographic" || name == "ExtPict")
        result = FromTable(kExtendedPictographic, kExtendedPictographicCount);
    // White_Space (PropList.txt): the space separators plus the ASCII
    // controls and NEL.
    else if (name == "White_Space" || name == "space")
        result = ScanCategories({Category::Zs, Category::Zl, Category::Zp}).AddRange(0x09, 0x0D).AddChar(0x85);
    else {
        const std::string_view canonical = CanonicalCategory(name);
        if (canonical.size() == 2) {
            if (const auto category = CategoryFor(canonical))
                result = ScanCategories({*category});
        }
        else if (canonical.size() == 1) {
            std::vector<Category> categories;
            for (const std::string_view two : {"Lu", "Ll", "Lt", "Lm", "Lo", "Mn", "Mc", "Me", "Nd", "Nl", "No", "Pc", "Pd", "Ps",
                                               "Pe", "Pi", "Pf", "Po", "Sm", "Sc", "Sk", "So", "Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co",
                                               "Cn"}) {
                if (two[0] == canonical[0])
                    categories.push_back(*CategoryFor(two));
            }
            if (!categories.empty())
                result = ScanCategories(categories);
        }
    }

    if (result) {
        const std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_propertyCache.emplace(std::string(name), *result);
    }
    return result;
}

bool IsAlphabetic(std::uint32_t c) {
    if (c >= kCodepointEnd)
        return false;
    switch (CategoryOf(c)) {
        case Category::Lu:
        case Category::Ll:
        case Category::Lt:
        case Category::Lm:
        case Category::Lo:
        case Category::Nl:
            return true;
        default:
            return false;
    }
}

const std::vector<std::uint32_t>& CaseFoldOrbit(std::uint32_t c) {
    return Orbits().Orbit(c);
}

CharacterSet CaseFold(const CharacterSet& set) {
    CharacterSet result = set;
    for (const CodepointRange& range : set.Ranges()) {
        for (std::uint32_t c = range.start; c < range.end; ++c) {
            for (const std::uint32_t other : CaseFoldOrbit(c))
                if (!result.Contains(other))
                    result = result.AddChar(other);
        }
    }
    return result;
}

} // namespace ned::editor::grammar::compile::unicode
