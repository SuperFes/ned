#include "ElicitationForm.h"

#include <algorithm>
#include <charconv>
#include <utility>

#include "Text/Utf8.h"

namespace ned::ui::acppanel {

namespace {

    using editor::acp::Json;

    std::string StringOf(const Json& object, const char* key) {
        return object.is_object() && object.contains(key) && object[key].is_string() ? object[key].get<std::string>() : std::string();
    }

    // `oneOf`/`anyOf` titled options, else a bare `enum` of values.
    std::vector<ElicitationForm::Option> OptionsOf(const Json& schema, const char* titledKey) {
        std::vector<ElicitationForm::Option> options;
        if (schema.contains(titledKey) && schema[titledKey].is_array()) {
            for (const Json& option : schema[titledKey]) {
                const std::string value = StringOf(option, "const");
                if (!value.empty()) {
                    const std::string title = StringOf(option, "title");
                    options.push_back({.value = value, .title = title.empty() ? value : title, .description = StringOf(option, "description")});
                }
            }
        }
        else if (schema.contains("enum") && schema["enum"].is_array()) {
            for (const Json& value : schema["enum"]) {
                if (value.is_string()) {
                    options.push_back({.value = value.get<std::string>(), .title = value.get<std::string>()});
                }
            }
        }
        return options;
    }

    std::string FieldAnswer(const ElicitationForm::Field& field) {
        switch (field.kind) {
            case ElicitationForm::FieldKind::Choice:
                return field.chosen ? field.options[*field.chosen].title : std::string();
            case ElicitationForm::FieldKind::MultiChoice: {
                std::string answer;
                for (std::size_t i = 0; i < field.options.size(); ++i) {
                    if (field.checked[i]) {
                        answer += (answer.empty() ? "" : ", ") + field.options[i].title;
                    }
                }
                return answer;
            }
            case ElicitationForm::FieldKind::Boolean:
                return field.flag ? "yes" : "no";
            case ElicitationForm::FieldKind::Text:
            case ElicitationForm::FieldKind::Number:
            case ElicitationForm::FieldKind::Integer:
                break;
        }
        return field.text;
    }

    bool IsPlain(const editor::KeyChord& chord) {
        return !chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint != 0;
    }

} // namespace

ElicitationForm::ElicitationForm(std::string message, const Json& schema, const std::vector<std::string>& order) : message_(std::move(message)) {
    std::vector<std::string> required;
    if (schema.contains("required") && schema["required"].is_array()) {
        for (const Json& key : schema["required"]) {
            if (key.is_string()) {
                required.push_back(key.get<std::string>());
            }
        }
    }
    if (!schema.contains("properties") || !schema["properties"].is_object()) {
        return;
    }
    for (const auto& [key, property] : schema["properties"].items()) {
        if (!property.is_object()) {
            continue;
        }
        Field      field{.key = key, .title = StringOf(property, "title"), .description = StringOf(property, "description")};
        const auto type = StringOf(property, "type");
        field.required  = std::find(required.begin(), required.end(), key) != required.end();
        if (field.title.empty()) {
            field.title = key;
        }
        if (type == "array") {
            field.kind    = FieldKind::MultiChoice;
            field.options = property.contains("items") ? OptionsOf(property["items"], "anyOf") : std::vector<Option>{};
            field.checked.assign(field.options.size(), false);
            if (property.contains("default") && property["default"].is_array()) {
                for (const Json& value : property["default"]) {
                    for (std::size_t i = 0; i < field.options.size(); ++i) {
                        field.checked[i] = field.checked[i] || (value.is_string() && value.get<std::string>() == field.options[i].value);
                    }
                }
            }
        }
        else if (type == "boolean") {
            field.kind = FieldKind::Boolean;
            field.flag = property.contains("default") && property["default"].is_boolean() && property["default"].get<bool>();
        }
        else if (type == "number" || type == "integer") {
            field.kind = type == "number" ? FieldKind::Number : FieldKind::Integer;
            if (property.contains("default") && property["default"].is_number()) {
                field.text = property["default"].dump();
            }
        }
        else if (type == "string") {
            field.options              = OptionsOf(property, "oneOf");
            field.kind                 = field.options.empty() ? FieldKind::Text : FieldKind::Choice;
            const std::string fallback = StringOf(property, "default");
            for (std::size_t i = 0; i < field.options.size(); ++i) {
                if (field.options[i].value == fallback) {
                    field.chosen    = i;
                    field.highlight = i;
                }
            }
            if (field.kind == FieldKind::Text) {
                field.text = fallback;
            }
        }
        else {
            continue; // a type ACP doesn't define
        }
        fields_.push_back(std::move(field));
    }
    auto rank = [&order](const Field& field) {
        return static_cast<std::size_t>(std::find(order.begin(), order.end(), field.key) - order.begin());
    };
    std::stable_sort(fields_.begin(), fields_.end(), [&rank](const Field& a, const Field& b) { return rank(a) < rank(b); });
}

ElicitationForm ElicitationForm::ForUrl(std::string message, std::string url) {
    ElicitationForm form;
    form.message_ = std::move(message);
    form.url_     = std::move(url);
    return form;
}

ElicitationForm::KeyResult ElicitationForm::Advance() {
    if (current_ + 1 >= fields_.size()) {
        return KeyResult::Submit;
    }
    ++current_;
    return KeyResult::Handled;
}

ElicitationForm::KeyResult ElicitationForm::HandleKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Escape) {
        return KeyResult::Decline;
    }
    if (chord.Control && chord.Codepoint == U'g') {
        return KeyResult::Cancel;
    }
    // C-RET sends from anywhere; plain Enter moves on, sending from the last field.
    if (chord.Special == editor::SpecialKey::Enter && chord.Control) {
        return KeyResult::Submit;
    }
    if (fields_.empty()) {
        return chord.Special == editor::SpecialKey::Enter ? KeyResult::Submit : KeyResult::Handled;
    }
    Field&     field    = fields_[current_];
    const bool listed   = field.kind == FieldKind::Choice || field.kind == FieldKind::MultiChoice;
    const bool typeable = field.kind == FieldKind::Text || field.kind == FieldKind::Number || field.kind == FieldKind::Integer;

    if (chord.Special == editor::SpecialKey::Tab) {
        if (chord.Shift) {
            current_ = current_ == 0 ? 0 : current_ - 1;
            return KeyResult::Handled;
        }
        current_ = std::min(current_ + 1, fields_.size() - 1);
        return KeyResult::Handled;
    }
    if (chord.Special == editor::SpecialKey::Up || chord.Special == editor::SpecialKey::Down) {
        const bool down = chord.Special == editor::SpecialKey::Down;
        // Inside a list first; off either end moves between fields.
        if (listed && (down ? field.highlight + 1 < field.options.size() : field.highlight > 0)) {
            field.highlight = down ? field.highlight + 1 : field.highlight - 1;
        }
        else if (down && current_ + 1 < fields_.size()) {
            ++current_;
        }
        else if (!down && current_ > 0) {
            --current_;
        }
        return KeyResult::Handled;
    }
    if (chord.Special == editor::SpecialKey::Enter) {
        if (field.kind == FieldKind::Choice && !field.options.empty()) {
            field.chosen = field.highlight;
        }
        return Advance();
    }
    if (listed && IsPlain(chord) && chord.Codepoint >= U'1' && chord.Codepoint <= U'9') {
        const std::size_t index = chord.Codepoint - U'1';
        if (index < field.options.size()) {
            field.highlight = index;
            if (field.kind == FieldKind::Choice) {
                field.chosen = index;
                return Advance();
            }
            field.checked[index] = !field.checked[index];
        }
        return KeyResult::Handled;
    }
    if (IsPlain(chord) && chord.Codepoint == U' ' && !typeable) {
        if (field.kind == FieldKind::Choice && !field.options.empty()) {
            field.chosen = field.highlight;
        }
        else if (field.kind == FieldKind::MultiChoice && !field.options.empty()) {
            field.checked[field.highlight] = !field.checked[field.highlight];
        }
        else if (field.kind == FieldKind::Boolean) {
            field.flag = !field.flag;
        }
        return KeyResult::Handled;
    }
    if (field.kind == FieldKind::Boolean && IsPlain(chord) && (chord.Codepoint == U'y' || chord.Codepoint == U'n')) {
        field.flag = chord.Codepoint == U'y';
        return KeyResult::Handled;
    }
    if (typeable && chord.Special == editor::SpecialKey::Backspace) {
        if (!field.text.empty()) {
            field.text.resize(text::PreviousCodepointBoundary(field.text, field.text.size()));
        }
        return KeyResult::Handled;
    }
    if (typeable && IsPlain(chord)) {
        field.text += text::EncodeCodepointUtf8(chord.Codepoint);
        return KeyResult::Handled;
    }
    return KeyResult::Handled;
}

std::optional<Json> ElicitationForm::Content(std::string& problem) {
    Json content = Json::object();
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        const Field& field   = fields_[i];
        auto         missing = [&] {
            problem  = "Answer \"" + field.title + "\" first.";
            current_ = i;
            return std::nullopt;
        };
        switch (field.kind) {
            case FieldKind::Choice:
                if (field.chosen) {
                    content[field.key] = field.options[*field.chosen].value;
                }
                else if (field.required) {
                    return missing();
                }
                break;
            case FieldKind::MultiChoice: {
                Json values = Json::array();
                for (std::size_t option = 0; option < field.options.size(); ++option) {
                    if (field.checked[option]) {
                        values.push_back(field.options[option].value);
                    }
                }
                if (values.empty() && field.required) {
                    return missing();
                }
                if (!values.empty()) {
                    content[field.key] = std::move(values);
                }
                break;
            }
            case FieldKind::Boolean:
                content[field.key] = field.flag;
                break;
            case FieldKind::Text:
                if (!field.text.empty()) {
                    content[field.key] = field.text;
                }
                else if (field.required) {
                    return missing();
                }
                break;
            case FieldKind::Number:
            case FieldKind::Integer: {
                if (field.text.empty()) {
                    if (field.required) {
                        return missing();
                    }
                    break;
                }
                const char* first = field.text.data();
                const char* last  = first + field.text.size();
                if (field.kind == FieldKind::Integer) {
                    long long value      = 0;
                    const auto [end, ec] = std::from_chars(first, last, value);
                    if (ec != std::errc() || end != last) {
                        problem  = "\"" + field.title + "\" needs a whole number.";
                        current_ = i;
                        return std::nullopt;
                    }
                    content[field.key] = value;
                }
                else {
                    double value         = 0;
                    const auto [end, ec] = std::from_chars(first, last, value);
                    if (ec != std::errc() || end != last) {
                        problem  = "\"" + field.title + "\" needs a number.";
                        current_ = i;
                        return std::nullopt;
                    }
                    content[field.key] = value;
                }
                break;
            }
        }
    }
    return content;
}

std::string ElicitationForm::Summary() const {
    std::string summary;
    for (const Field& field : fields_) {
        const std::string answer = FieldAnswer(field);
        if (!answer.empty() && (field.kind != FieldKind::Boolean || fields_.size() == 1)) {
            summary += (summary.empty() ? "" : "; ") + answer;
        }
    }
    return summary;
}

std::vector<DisplayLine> ElicitationForm::Format(int maxRows) const {
    std::vector<DisplayLine> body;
    std::size_t              focus = 0; // the body line the cursor is on
    if (!url_.empty()) {
        body.push_back({.text = "  " + url_, .style = DisplayStyle::Accent});
    }
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        const Field& field   = fields_[i];
        const bool   current = i == current_;
        std::string  header  = std::string(current ? "▸ " : "  ") + field.title + (field.required ? " *" : "");
        if (!field.description.empty() && field.description != message_) {
            header += " — " + field.description;
        }
        if (current) {
            focus = body.size();
        }
        body.push_back({.text = std::move(header), .style = current ? DisplayStyle::Accent : DisplayStyle::Plain});
        switch (field.kind) {
            case FieldKind::Choice:
            case FieldKind::MultiChoice:
                for (std::size_t option = 0; option < field.options.size(); ++option) {
                    const bool  under = current && option == field.highlight;
                    const bool  on    = field.kind == FieldKind::Choice ? field.chosen == option : field.checked[option];
                    const char* mark  = field.kind == FieldKind::Choice ? (on ? "●" : "○") : (on ? "☑" : "☐");
                    std::string text  = std::string(under ? "  > " : "    ") + (option < 9 ? std::to_string(option + 1) : " ") + " " + mark + " " +
                                        field.options[option].title;
                    if (!field.options[option].description.empty()) {
                        text += "  " + field.options[option].description;
                    }
                    if (under) {
                        focus = body.size();
                    }
                    body.push_back({.text = std::move(text), .style = under ? DisplayStyle::Accent : DisplayStyle::Plain});
                }
                break;
            case FieldKind::Boolean:
                body.push_back({.text = std::string("    ") + (field.flag ? "☑ yes" : "☐ no"), .style = DisplayStyle::Plain});
                break;
            case FieldKind::Text:
            case FieldKind::Number:
            case FieldKind::Integer:
                body.push_back({.text  = "    " + (field.text.empty() && !current ? std::string("—") : field.text) + (current ? "▏" : ""),
                                .style = current ? DisplayStyle::Accent : DisplayStyle::Plain});
                break;
        }
    }

    std::vector<DisplayLine> lines;
    lines.push_back({.text = "? " + message_, .style = DisplayStyle::Warning});
    const std::string footer = !url_.empty() ? "  Enter opens it · Esc declines · C-g cancels"
                                             : "  Tab/↑↓ move · Enter picks · C-RET sends · Esc skips · C-g cancels";
    // The message and the key line always show; the fields scroll to keep
    // the cursor in view.
    const std::size_t room  = static_cast<std::size_t>(std::max(1, maxRows - 2));
    const std::size_t first = focus >= room ? focus - room + 1 : 0;
    for (std::size_t i = first; i < body.size() && i < first + room; ++i) {
        lines.push_back(body[i]);
    }
    lines.push_back({.text = footer, .style = DisplayStyle::Dim});
    return lines;
}

} // namespace ned::ui::acppanel
