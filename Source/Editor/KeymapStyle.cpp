#include "KeymapStyle.h"

#include <mutex>
#include <stdexcept>

#include "Vim/Settings.h"

namespace ned::editor {

namespace {

    std::mutex& StyleMutex() {
        static std::mutex mutex;
        return mutex;
    }

    KeymapStyle& StyleStorage() {
        static KeymapStyle style = KeymapStyle::Emacs;
        return style;
    }

} // namespace

void SetKeymapStyle(KeymapStyle style) {
    {
        const std::lock_guard<std::mutex> lock(StyleMutex());
        StyleStorage() = style;
    }
    // vim::ModeEnabled() is the flag the Vim engine actually polls every
    // keystroke (BufferView::OnKeyEvent) -- kept in lockstep here so the two
    // can never disagree about whether Vim is live. Outside this function's
    // own lock: SetModeEnabled takes its own.
    vim::SetModeEnabled(style == KeymapStyle::Vim);
}

KeymapStyle GetKeymapStyle() {
    const std::lock_guard<std::mutex> lock(StyleMutex());
    return StyleStorage();
}

std::optional<KeymapStyle> ParseKeymapStyle(std::string_view name) {
    if (name == "emacs") {
        return KeymapStyle::Emacs;
    }
    if (name == "vim") {
        return KeymapStyle::Vim;
    }
    if (name == "modern") {
        return KeymapStyle::Modern;
    }
    return std::nullopt;
}

std::string_view KeymapStyleName(KeymapStyle style) {
    switch (style) {
        case KeymapStyle::Emacs:
            return "emacs";
        case KeymapStyle::Vim:
            return "vim";
        case KeymapStyle::Modern:
            return "modern";
    }
    throw std::runtime_error("ned: internal error: unhandled KeymapStyle");
}

} // namespace ned::editor
