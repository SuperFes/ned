//
// The process-wide switches for inline colour swatches -- mirrors
// StatusGutterSettings.h's exact pattern.
//
// The swatch is virtual text: a cell painted in the colour a literal names,
// drawn just before that literal (Editor/ColorLiteral.h finds them,
// BufferView draws them through the same span list inlay hints ride).
//

#ifndef NED_EDITOR_COLORSWATCHSETTINGS_H
#define NED_EDITOR_COLORSWATCHSETTINGS_H

namespace ned::editor {

// Default true. Costs nothing on a buffer with no colour in it: the scan is
// viewport-windowed and the universal spellings it looks for (a 6/8-digit hex
// run, a functional notation) are cheap to reject.
void               SetColorSwatchesEnabled(bool enabled);
[[nodiscard]] bool ColorSwatchesEnabled();

enum class ColorSwatchStyle {
    // A filled cell before the literal, the conventional stylesheet-editor
    // marker. Costs one column, exactly as an inlay hint does.
    Block,
    // The literal's own cells are washed in the colour instead, costing no
    // columns at all -- for anyone who would rather the text not move.
    Underlay,
};

void                           SetColorSwatchStyle(ColorSwatchStyle style);
[[nodiscard]] ColorSwatchStyle GetColorSwatchStyle();

} // namespace ned::editor

#endif // NED_EDITOR_COLORSWATCHSETTINGS_H
