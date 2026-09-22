// The wgsl external scanner, ported from https://github.com/tree-sitter-grammars/tree-sitter-wgsl-bevy (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::wgsl {

using namespace ned::editor::parse::scanner;

enum TokenType {
	BLOCK_COMMENT
};

void *Create() {
	return NULL;
}
static void Destroy(void *p_) {
    VoidPtr p{p_};}
void tree_sitter_wgsl_bevy_external_scanner_reset(void *p) {}
unsigned int Serialize(void *p, char *buffer) {
	return 0;
}
static void Deserialize(void *p_, const char *b, unsigned n) {
    VoidPtr p{p_};}

static void advance(Lexer *lexer) {
	lexer->advance(lexer, false);
}

static bool at_eof(Lexer *lexer) {
	return lexer->eof(lexer);
}

// based on https://github.com/tree-sitter/tree-sitter-rust/blob/f7fb205c424b0962de59b26b931fe484e1262b35/src/scanner.c
static bool Scan(
	void *payload_,
	Lexer *lexer,
	const bool *valid_symbols
) {
    VoidPtr payload{payload_};
	while (iswspace(lexer->lookahead)) {
		lexer->advance(lexer, true);
	}

	if (lexer->lookahead != '/') {
		return false;
	}
	advance(lexer);

	if (lexer->lookahead != '*') {
		return false;
	}
	advance(lexer);

	unsigned int comment_depth = 1;
	while (true) {
		if (lexer->lookahead == '/') {
			advance(lexer);

			if (lexer->lookahead == '*') {
				advance(lexer);
				comment_depth += 1;
			}
		} else if (lexer->lookahead == '*') {
			advance(lexer);

			if (lexer->lookahead == '/') {
				advance(lexer);
				comment_depth -= 1;
				
				if (comment_depth == 0) {
					lexer->resultSymbol = BLOCK_COMMENT;
					return true;
				}
			}
		} else if (at_eof(lexer)) {
			return false;
		} else {
			advance(lexer);
		}
	}
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::wgsl

NED_SCANNER_LIBRARY_EXPORT(wgsl, ned::editor::languages::scanners::wgsl::kScanner)
