//
// A command's terminal output as plain text for the transcript: escape
// sequences dropped, and a bare carriage return rewriting its line the way
// a progress bar expects. Output arrives in arbitrary chunks, so a sequence
// or a CR split across two chunks carries over in TerminalTextState.
//

#ifndef NED_EDITOR_ACP_TERMINALTEXT_H
#define NED_EDITOR_ACP_TERMINALTEXT_H

#include <cstddef>
#include <string>
#include <string_view>

namespace ned::editor::acp {

struct TerminalTextState {
    enum class Mode { Text,
                      Escape,      // after ESC
                      Csi,         // ESC [ ... final byte
                      Osc,         // ESC ] ... BEL or ST
                      OscEscape }; // ESC inside an OSC, maybe starting ST
    Mode mode      = Mode::Text;
    bool pendingCr = false; // a CR whose meaning depends on the next byte
};

void AppendTerminalText(std::string& out, TerminalTextState& state, std::string_view data);

// Cuts `text` to at most about `maxBytes`, dropping whole lines from the
// front. Returns whether anything was dropped.
bool KeepTail(std::string& text, std::size_t maxBytes);

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_TERMINALTEXT_H
