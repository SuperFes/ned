#include "Parser.h"

namespace ned::editor::grammar {

Language::Language(const TSLanguage* language) noexcept : language_(language) {
}

const TSLanguage* Language::Raw() const noexcept {
    return language_;
}

Parser::Parser(const Language& language) : engine_(std::make_unique<parse::Engine>(language.Raw())) {
}

Parser::~Parser() = default;

Parser::Parser(Parser&& other) noexcept = default;

Parser& Parser::operator=(Parser&& other) noexcept = default;

Tree Parser::Parse(std::string_view text) const {
    return Tree(engine_->Parse(text));
}

Tree Parser::Parse(std::string_view text, const Tree& oldTree) const {
    return Tree(engine_->Parse(text, oldTree.Green()));
}

} // namespace ned::editor::grammar
