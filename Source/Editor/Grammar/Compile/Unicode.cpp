#include "Unicode.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>

#include <utf8proc.h>

#include "Editor/Grammar/Compile/UnicodeTables.h"

namespace ned::editor::grammar::compile::unicode {

namespace {

    CharacterSet FromTable(const std::uint32_t* table, std::size_t count) {
        CharacterSet set;
        for (std::size_t i = 0; i < count; ++i)
            set = set.AddRange(table[2 * i], table[2 * i + 1] - 1);
        return set;
    }

    // utf8proc's category enum in two-letter spelling order.
    std::optional<utf8proc_category_t> CategoryFor(std::string_view name) {
        static const std::unordered_map<std::string_view, utf8proc_category_t> kCategories = {
            {"Lu", UTF8PROC_CATEGORY_LU},
            {"Ll", UTF8PROC_CATEGORY_LL},
            {"Lt", UTF8PROC_CATEGORY_LT},
            {"Lm", UTF8PROC_CATEGORY_LM},
            {"Lo", UTF8PROC_CATEGORY_LO},
            {"Mn", UTF8PROC_CATEGORY_MN},
            {"Mc", UTF8PROC_CATEGORY_MC},
            {"Me", UTF8PROC_CATEGORY_ME},
            {"Nd", UTF8PROC_CATEGORY_ND},
            {"Nl", UTF8PROC_CATEGORY_NL},
            {"No", UTF8PROC_CATEGORY_NO},
            {"Pc", UTF8PROC_CATEGORY_PC},
            {"Pd", UTF8PROC_CATEGORY_PD},
            {"Ps", UTF8PROC_CATEGORY_PS},
            {"Pe", UTF8PROC_CATEGORY_PE},
            {"Pi", UTF8PROC_CATEGORY_PI},
            {"Pf", UTF8PROC_CATEGORY_PF},
            {"Po", UTF8PROC_CATEGORY_PO},
            {"Sm", UTF8PROC_CATEGORY_SM},
            {"Sc", UTF8PROC_CATEGORY_SC},
            {"Sk", UTF8PROC_CATEGORY_SK},
            {"So", UTF8PROC_CATEGORY_SO},
            {"Zs", UTF8PROC_CATEGORY_ZS},
            {"Zl", UTF8PROC_CATEGORY_ZL},
            {"Zp", UTF8PROC_CATEGORY_ZP},
            {"Cc", UTF8PROC_CATEGORY_CC},
            {"Cf", UTF8PROC_CATEGORY_CF},
            {"Cs", UTF8PROC_CATEGORY_CS},
            {"Co", UTF8PROC_CATEGORY_CO},
            {"Cn", UTF8PROC_CATEGORY_CN},
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

    CharacterSet ScanCategories(const std::vector<utf8proc_category_t>& categories) {
        CharacterSet  set;
        std::uint32_t runStart = 0;
        bool          inRun    = false;
        for (std::uint32_t c = 0; c < kCodepointEnd; ++c) {
            const utf8proc_category_t category = utf8proc_category(static_cast<utf8proc_int32_t>(c));
            const bool                member   = std::find(categories.begin(), categories.end(), category) != categories.end();
            if (member && !inRun) {
                runStart = c;
                inRun    = true;
            }
            else if (!member && inRun) {
                set   = set.AddRange(runStart, c - 1);
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
    // when tolower/toupper/totitle connect them. Built once over the whole
    // codepoint space.
    class FoldOrbits {
      public:
        FoldOrbits() {
            parent_.resize(kCodepointEnd);
            for (std::uint32_t c = 0; c < kCodepointEnd; ++c)
                parent_[c] = c;
            for (std::uint32_t c = 0; c < kCodepointEnd; ++c) {
                const auto cp = static_cast<utf8proc_int32_t>(c);
                Join(c, static_cast<std::uint32_t>(utf8proc_tolower(cp)));
                Join(c, static_cast<std::uint32_t>(utf8proc_toupper(cp)));
                Join(c, static_cast<std::uint32_t>(utf8proc_totitle(cp)));
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
    else {
        const std::string_view canonical = CanonicalCategory(name);
        if (canonical.size() == 2) {
            if (const auto category = CategoryFor(canonical))
                result = ScanCategories({*category});
        }
        else if (canonical.size() == 1) {
            std::vector<utf8proc_category_t> categories;
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
    switch (utf8proc_category(static_cast<utf8proc_int32_t>(c))) {
        case UTF8PROC_CATEGORY_LU:
        case UTF8PROC_CATEGORY_LL:
        case UTF8PROC_CATEGORY_LT:
        case UTF8PROC_CATEGORY_LM:
        case UTF8PROC_CATEGORY_LO:
        case UTF8PROC_CATEGORY_NL:
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
