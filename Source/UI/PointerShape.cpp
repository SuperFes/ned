#include "PointerShape.h"

#include <cstdlib>
#include <string_view>

#include <unistd.h>

#include "Editor/Clipboard.h"
#include "Editor/PointerShapeSettings.h"
#include "UI/EventLoop.h"

namespace ned::ui {

namespace {

    PointerShape g_current = PointerShape::Default;
    PointerShape g_pending = PointerShape::Default;

    std::string_view CssName(PointerShape shape) {
        switch (shape) {
            case PointerShape::ColumnResize:
                return "ew-resize";
            case PointerShape::RowResize:
                return "ns-resize";
            case PointerShape::Default:
                break;
        }
        return "default";
    }

    void Write(PointerShape shape) {
        if (HeadlessOutputForTesting()) {
            return; // see SetHeadlessOutputForTesting's own doc comment
        }
        const char*       tmux     = std::getenv("TMUX");
        const std::string sequence = BuildPointerShapeSequence(shape, tmux != nullptr && *tmux != '\0');
        std::size_t       written  = 0;
        while (written < sequence.size()) {
            const ssize_t result = ::write(STDOUT_FILENO, sequence.data() + written, sequence.size() - written);
            if (result <= 0) {
                return; // best-effort: the pointer just keeps its shape
            }
            written += static_cast<std::size_t>(result);
        }
    }

    void Apply(PointerShape shape) {
        if (shape == g_current) {
            return;
        }
        g_current = shape;
        Write(shape);
    }

} // namespace

std::string BuildPointerShapeSequence(PointerShape shape, bool wrapForTmux) {
    std::string sequence = "\x1b]22;";
    sequence += CssName(shape);
    sequence += "\x1b\\";
    return wrapForTmux ? editor::WrapForTmuxPassthrough(sequence) : sequence;
}

void BeginPointerEvent(bool buttonHeld) {
    g_pending = buttonHeld ? g_current : PointerShape::Default;
}

void RequestPointerShape(PointerShape shape) {
    g_pending = shape;
}

void CommitPointerShape() {
    Apply(editor::PointerShapesEnabled() ? g_pending : PointerShape::Default);
}

void ResetPointerShape() {
    g_pending = PointerShape::Default;
    Apply(PointerShape::Default);
}

PointerShape CurrentPointerShape() {
    return g_current;
}

} // namespace ned::ui
