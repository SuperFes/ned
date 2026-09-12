//
// Reading a language's imprint out of a tree-sitter grammar.json.
//
// The vocabulary this produces -- `DelimitedBody`, `DelimiterKind`,
// `FoldPolicy` -- lives in `Editor/Imprint.h` and knows nothing about
// tree-sitter. This file is the one half that does: grammar.json is a
// tree-sitter artifact, so parsing it belongs here rather than there. That
// split is the Phase 4 seam (Docs/ParsingEngine.md): replacing the engine
// replaces this file and leaves the vocabulary untouched.
//
// Measured against the 55 fold nodes currently hand-written across
// queries/*-folds.scm: this reproduces 55/55 with nothing authored per
// language. `Tests/ImprintTest.cpp` enforces the number, so a grammar bump
// that breaks inference fails the build rather than being noticed later.
//
// This reads *grain* -- structure legible from a construct's shape. It does
// not, and cannot, read a burl: see `Editor/Imprint.h` for what that means
// and why the Lisp cases are the acceptance test for everything above it.
//
// Pure: a function over parsed JSON, no Parser, Buffer or Screen, so it is
// unit-testable against crafted grammars as well as real ones.
//

#ifndef NED_EDITOR_TREESITTER_GRAMMARIMPRINT_H
#define NED_EDITOR_TREESITTER_GRAMMARIMPRINT_H

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "Editor/Imprint.h"

namespace ned::editor::treesitter {

// Every visible rule in grammar that is a delimited body, mapped to the
// structural facts about it. Hidden (_-prefixed) rules are never reported:
// tree-sitter inlines them rather than making them nodes, so they can never
// be a node a consumer folds or selects.
//
// Throws nothing -- a grammar missing "rules", or carrying a shape this
// doesn't understand, yields fewer entries rather than an error. An
// unparseable grammar is the caller's problem at json::parse time.
[[nodiscard]] std::map<std::string, imprint::DelimitedBody> InferDelimitedBodies(const nlohmann::json& grammar);

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_GRAMMARIMPRINT_H
