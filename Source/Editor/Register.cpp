#include "Register.h"

#include <utility>

namespace ned::editor {

namespace {

    std::string JoinPieces(const std::vector<std::string>& pieces) {
        std::string joined;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if (i > 0) {
                joined += '\n';
            }
            joined += pieces[i];
        }
        return joined;
    }

} // namespace

void RegisterTable::SetPoint(char32_t name, std::string bufferName, std::vector<std::size_t> byteOffsets) {
    if (byteOffsets.empty()) {
        byteOffsets.push_back(0); // defensive; every real caller passes at least the primary's point
    }
    registers_[name] = PointRegisterValue{std::move(bufferName), std::move(byteOffsets)};
}

void RegisterTable::SetText(char32_t name, std::string text) {
    SetTextPieces(name, {std::move(text)});
}

void RegisterTable::SetTextPieces(char32_t name, std::vector<std::string> pieces) {
    if (pieces.empty()) {
        pieces.emplace_back(); // defensive; every real caller passes at least one piece
    }
    std::string joined = JoinPieces(pieces);
    registers_[name]   = TextValue{std::move(pieces), std::move(joined)};
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
    const TextValue* value = std::get_if<TextValue>(&it->second);
    return value ? &value->joined : nullptr;
}

const std::vector<std::string>* RegisterTable::TextPieces(char32_t name) const {
    const auto it = registers_.find(name);
    if (it == registers_.end()) {
        return nullptr;
    }
    const TextValue* value = std::get_if<TextValue>(&it->second);
    return value ? &value->pieces : nullptr;
}

} // namespace ned::editor
