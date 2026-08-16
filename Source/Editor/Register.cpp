#include "Register.h"

#include <utility>

namespace ned::editor {

void RegisterTable::SetPoint(char32_t name, std::string bufferName, std::size_t byteOffset) {
    registers_[name] = PointRegisterValue{std::move(bufferName), byteOffset};
}

void RegisterTable::SetText(char32_t name, std::string text) {
    registers_[name] = std::move(text);
}

const PointRegisterValue* RegisterTable::Point(char32_t name) const {
    const auto it = registers_.find(name);
    if (it == registers_.end()) {
        return nullptr;
    }
    return std::get_if<PointRegisterValue>(&it->second);
}

const std::string* RegisterTable::Text(char32_t name) const {
    const auto it = registers_.find(name);
    if (it == registers_.end()) {
        return nullptr;
    }
    return std::get_if<std::string>(&it->second);
}

} // namespace ned::editor
