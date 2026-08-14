#include "Parser.h"

#include <stdexcept>
#include <utility>

namespace ned::editor::treesitter {

Language::Language(const TSLanguage* language) noexcept : language_(language) {
}

const TSLanguage* Language::Raw() const noexcept {
    return language_;
}

Parser::Parser(const Language& language) : parser_(ts_parser_new()) {
    if (!ts_parser_set_language(parser_, language.Raw())) {
        ts_parser_delete(parser_);
        throw std::runtime_error("ned: tree-sitter language is incompatible with this parser's ABI version");
    }
}

Parser::~Parser() {
    ts_parser_delete(parser_); // ts_parser_delete(nullptr) is a documented no-op
}

Parser::Parser(Parser&& other) noexcept : parser_(std::exchange(other.parser_, nullptr)) {
}

Parser& Parser::operator=(Parser&& other) noexcept {
    if (this != &other) {
        ts_parser_delete(parser_);
        parser_ = std::exchange(other.parser_, nullptr);
    }
    return *this;
}

Tree Parser::Parse(std::string_view text) const {
    return Tree(ts_parser_parse_string(parser_, nullptr, text.data(), static_cast<uint32_t>(text.size())));
}

} // namespace ned::editor::treesitter
