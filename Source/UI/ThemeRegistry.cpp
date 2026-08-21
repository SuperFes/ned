#include "ThemeRegistry.h"

#include <algorithm>

namespace ned::ui {

namespace {

    struct ThemeFactory {
        std::string_view name;
        Theme (*make)();
    };

    // The one table (see the header comment). Each entry's name must match
    // the .name the factory itself sets -- checked by ThemeRegistryTest's
    // round-trip, not trusted.
    constexpr ThemeFactory kThemeFactories[] = {
        {"dark", DarkTheme},
        {"light", LightTheme},
        {"ansi-dark", AnsiDarkTheme},
        {"ansi-light", AnsiLightTheme},
    };

} // namespace

std::optional<Theme> ThemeByName(std::string_view name) {
    for (const ThemeFactory& factory : kThemeFactories) {
        if (factory.name == name) {
            return factory.make();
        }
    }
    return std::nullopt;
}

std::vector<std::string> ThemeNames() {
    std::vector<std::string> names;
    names.reserve(std::size(kThemeFactories));
    for (const ThemeFactory& factory : kThemeFactories) {
        names.emplace_back(factory.name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace ned::ui
