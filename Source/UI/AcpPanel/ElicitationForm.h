//
// An agent's structured question (ACP elicitation) as a keyboard form in
// the panel: the flat JSON Schema subset ACP allows -- single choice
// (`oneOf`/`enum` strings), multiple choice (arrays of `anyOf`/`enum`),
// text, numbers and booleans -- or a URL to visit. UI-state only: the
// panel paints Format() and sends Content() back through the Manager.
//

#ifndef NED_UI_ACPPANEL_ELICITATIONFORM_H
#define NED_UI_ACPPANEL_ELICITATIONFORM_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Editor/Acp/Client.h"
#include "Editor/Key.h"
#include "TranscriptFormat.h"

namespace ned::ui::acppanel {

class ElicitationForm {
  public:
    enum class FieldKind { Choice,
                           MultiChoice,
                           Text,
                           Number,
                           Integer,
                           Boolean };
    struct Option {
        std::string value;
        std::string title;
        std::string description;
    };
    struct Field {
        std::string                key;
        std::string                title;
        std::string                description;
        FieldKind                  kind     = FieldKind::Text;
        bool                       required = false;
        std::vector<Option>        options;           // Choice, MultiChoice
        std::optional<std::size_t> chosen;            // Choice
        std::vector<bool>          checked;           // MultiChoice
        std::string                text;              // Text, Number, Integer
        bool                       flag      = false; // Boolean
        std::size_t                highlight = 0;     // the option under the cursor
    };

    // A form from its `requestedSchema`.
    ElicitationForm(std::string message, const editor::acp::Json& schema);
    // A URL to visit.
    [[nodiscard]] static ElicitationForm ForUrl(std::string message, std::string url);

    enum class KeyResult { Handled,
                           Submit,
                           Decline,
                           Cancel };
    KeyResult HandleKey(const editor::KeyChord& chord);

    // The answers, keyed by field; nullopt with `problem` set (and that
    // field made current) when a required one is missing or a number
    // doesn't parse.
    [[nodiscard]] std::optional<editor::acp::Json> Content(std::string& problem);
    // The answers in a line, for the transcript.
    [[nodiscard]] std::string Summary() const;

    [[nodiscard]] std::vector<DisplayLine> Format(int maxRows) const;

    [[nodiscard]] const std::vector<Field>& Fields() const {
        return fields_;
    }
    [[nodiscard]] std::size_t CurrentField() const {
        return current_;
    }
    [[nodiscard]] const std::string& Url() const {
        return url_;
    }

  private:
    ElicitationForm() = default;
    KeyResult Advance(); // to the next field, or submit from the last

    std::string        message_;
    std::string        url_;
    std::vector<Field> fields_;
    std::size_t        current_ = 0;
};

} // namespace ned::ui::acppanel

#endif // NED_UI_ACPPANEL_ELICITATIONFORM_H
