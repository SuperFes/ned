//
// Which of AcpPanel's pickers a command asks for. Commands run in
// BufferView, the panel lives above it, so the request is forwarded up
// through WindowManager to main.cpp's wiring.
//

#ifndef NED_EDITOR_ACP_PANELPICKER_H
#define NED_EDITOR_ACP_PANELPICKER_H

namespace ned::editor::acp {

enum class PanelPicker { Rewind,
                         Mode,
                         Model,
                         Options,
                         Sessions,
                         Copy };

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_PANELPICKER_H
