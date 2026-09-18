//
// Reading a language's imprint out of its grammar.
//
// The vocabulary this produces -- `DelimitedBody`, `DelimiterKind`,
// `FoldPolicy` -- lives in `Editor/Imprint.h` and knows nothing about
// grammars. This file is the one half that does: it walks the rule tree of a
// `grammar.janet` (Grammar/Compile/GrammarFile.h) and reports the visible
// rules whose productions are delimited bodies.
//
// Measured against the 59 fold nodes once hand-written across
// queries/*-folds.scm: this reproduced 59/59 with nothing authored per
// language, and those eleven files are now deleted rather than maintained.
// `Tests/ImprintTest.cpp` keeps their node list as a pin, so a grammar bump
// that breaks inference fails the build rather than being noticed later.
//
// This reads *grain* -- structure legible from a construct's shape. It does
// not, and cannot, read a burl: see `Editor/Imprint.h` for what that means
// and why the Lisp cases are the acceptance test for everything above it.
//
// Pure: a function over a parsed grammar, no Parser, Buffer or Screen, so it
// is unit-testable against crafted grammars as well as real ones.
//

#ifndef NED_EDITOR_GRAMMAR_GRAMMARIMPRINT_H
#define NED_EDITOR_GRAMMAR_GRAMMARIMPRINT_H

#include <map>
#include <string>

#include "Editor/Grammar/Compile/GrammarFile.h"
#include "Editor/Imprint.h"

namespace ned::editor::grammar {

// Every visible rule in grammar that is a delimited body, mapped to the
// structural facts about it. Hidden (_-prefixed) rules are never reported:
// the parser inlines them rather than making them nodes, so they can never
// be a node a consumer folds or selects.
//
// Throws nothing -- a shape this doesn't understand yields fewer entries
// rather than an error.
[[nodiscard]] std::map<std::string, imprint::DelimitedBody> InferDelimitedBodies(const compile::GrammarFile& grammar);

} // namespace ned::editor::grammar

#endif // NED_EDITOR_GRAMMAR_GRAMMARIMPRINT_H
