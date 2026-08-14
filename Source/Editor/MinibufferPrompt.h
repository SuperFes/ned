//
// A minimal, reusable "collect one line of text" primitive for
// minibuffer-style prompts (find-file, switch-to-buffer, and future
// M-x-style input) -- UI-agnostic, driven by BufferView the same way
// IncrementalSearch/QueryReplace are.
//
// Deliberately has no notion of "confirm" or "cancel" itself: the caller
// reads Text() when it decides the prompt is done (Enter) and just discards
// the object otherwise (Escape/C-g) -- there's nothing here to cancel.
//

#ifndef NED_EDITOR_MINIBUFFERPROMPT_H
#define NED_EDITOR_MINIBUFFERPROMPT_H

#include <string>

namespace ned::editor {

class MinibufferPrompt {
  public:
    explicit MinibufferPrompt(std::string label);

    void AppendChar(char32_t codepoint);
    void DeleteChar();              // removes the last character, if any
    void SetText(std::string text); // wholesale replace, e.g. for Tab-completion

    [[nodiscard]] const std::string& Text() const;
    [[nodiscard]] std::string        StatusText() const; // label + current text, for the echo area

  private:
    std::string label_;
    std::string text_;
};

} // namespace ned::editor

#endif // NED_EDITOR_MINIBUFFERPROMPT_H
