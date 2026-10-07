//
// A Nerd Font glyph the chrome draws before a name, with the colour it
// carries, if any.
//

#ifndef NED_EDITOR_GLYPH_H
#define NED_EDITOR_GLYPH_H

#include <cstdint>
#include <optional>
#include <string>

namespace ned::editor {

struct Glyph {
    std::string                  text;
    std::optional<std::uint32_t> rgb; // 0xRRGGBB; unset takes the surrounding text's colour

    [[nodiscard]] bool empty() const {
        return text.empty();
    }
    [[nodiscard]] bool operator==(const Glyph&) const = default;
};

} // namespace ned::editor

#endif // NED_EDITOR_GLYPH_H
