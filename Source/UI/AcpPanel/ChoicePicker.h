//
// A keyboard-driven list AcpPanel shows in place of its transcript: rewind
// targets, session modes, config option values, resumable sessions. Typing
// narrows it (fuzzy), Up/Down moves, Enter or a digit 1-9 picks, Escape
// closes. Pure state plus formatting; the panel owns painting.
//

#ifndef NED_UI_ACPPANEL_CHOICEPICKER_H
#define NED_UI_ACPPANEL_CHOICEPICKER_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Editor/Key.h"

#include "TranscriptFormat.h"

namespace ned::ui::acppanel {

struct ChoiceItem {
    std::string label;
    std::string detail;          // dimmed, after the label
    bool        current = false; // marked as the value in effect
};

class ChoicePicker {
  public:
    // `onChoose` receives the index into `items` of the picked item.
    ChoicePicker(std::string title, std::vector<ChoiceItem> items, std::function<void(std::size_t)> onChoose);

    enum class KeyResult { Handled,
                           Chosen,
                           Closed };
    // Every key is consumed while the picker is open. Chosen/Closed mean the
    // caller should close it; Chosen has already run `onChoose`.
    KeyResult HandleKey(const editor::KeyChord& chord);

    // Lets Delete or C-d remove the selected item, once confirmed with y.
    // `onDelete` gets the item's index and says whether it went; one that
    // did leaves the list.
    void SetOnDelete(std::function<bool(std::size_t)> onDelete);

    // The title, the filter when there is one, then as many items as fit in
    // `maxRows` around the selection.
    [[nodiscard]] std::vector<DisplayLine> Format(int maxRows) const;

    // Indices into the items passed at construction, filtered and ranked by
    // what has been typed.
    [[nodiscard]] std::vector<std::size_t> Visible() const;
    [[nodiscard]] std::size_t              Selection() const {
        return selection_;
    }

  private:
    std::string                      title_;
    std::vector<ChoiceItem>          items_;
    std::function<void(std::size_t)> onChoose_;
    std::function<bool(std::size_t)> onDelete_;
    std::optional<std::size_t>       confirmingDelete_; // the item a y would delete
    std::vector<bool>                removed_;
    std::string                      filter_;
    std::size_t                      selection_ = 0;
};

} // namespace ned::ui::acppanel

#endif // NED_UI_ACPPANEL_CHOICEPICKER_H
