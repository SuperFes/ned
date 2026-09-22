// The caddy external scanner, ported from https://github.com/caddyserver/tree-sitter-caddyfile (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::caddy {

using namespace ned::editor::parse::scanner;

enum TokenType {
	HEREDOC_START,
	HEREDOC_BODY,
	HEREDOC_END,
};

typedef struct {
	char delimiter[32];
	int delimiter_len;
	bool has_heredoc;
} Scanner;

static void advance(Lexer *lexer) { lexer->advance(lexer, false); }

static bool is_valid_heredoc_marker_char(int32_t c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		   (c >= '0' && c <= '9') || c == '_';
}

static unsigned serialize(Scanner *scanner, char *buffer)
{
	if (scanner->has_heredoc) {
		buffer[0] = 1;
		memcpy(&buffer[1], scanner->delimiter, scanner->delimiter_len);
		return 1 + scanner->delimiter_len;
	} else {
		buffer[0] = 0;
		return 1;
	}
}

static void deserialize(Scanner *scanner, const char *buffer, unsigned length)
{
	if (length > 0) {
		scanner->has_heredoc = buffer[0] == 1;
		if (scanner->has_heredoc && length > 1) {
			scanner->delimiter_len = length - 1;
			memcpy(scanner->delimiter, &buffer[1], scanner->delimiter_len);
		} else {
			scanner->delimiter_len = 0;
		}
	}
}

static bool scan_heredoc_start(Scanner *scanner, Lexer *lexer)
{
	// No spaces allowed at start of marker
	if (lexer->lookahead == ' ' || lexer->lookahead == '\t')
		return false;

	// Read the delimiter
	int delimiter_len = 0;
	char delimiter[32] = {0};

	// We should capture at least one character for the delimiter
	if (!is_valid_heredoc_marker_char(lexer->lookahead))
		return false;

	while (is_valid_heredoc_marker_char(lexer->lookahead) &&
		   delimiter_len < 31) {
		delimiter[delimiter_len++] = lexer->lookahead;
		advance(lexer);
	}

	// Must have a valid delimiter
	if (delimiter_len == 0)
		return false;

	// Store the delimiter for later matching
	memcpy(scanner->delimiter, delimiter, delimiter_len);
	scanner->delimiter_len = delimiter_len;
	scanner->has_heredoc = true;

	// Make sure we have a newline after the delimiter
	if (lexer->lookahead != '\n' && lexer->lookahead != '\r')
		return false;

	// Mark the end and set the result symbol
	lexer->markEnd(lexer);
	lexer->resultSymbol = HEREDOC_START;
	return true;
}

static bool scan_heredoc_body(Scanner *scanner, Lexer *lexer)
{
	if (!scanner->has_heredoc)
		return false;

	// Check if we're at the potential end delimiter
	bool is_start_of_line = lexer->getColumn(lexer) == 0;
	if (is_start_of_line) {
		// Skip leading whitespace to check for indented end marker
		int32_t saved_lookahead = lexer->lookahead;
		int whitespace_count = 0;
		while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
			advance(lexer);
			whitespace_count++;
		}

		// Try to match the delimiter after whitespace
		bool is_delimiter = true;
		int i;

		for (i = 0; i < scanner->delimiter_len; i++) {
			if (lexer->lookahead != scanner->delimiter[i]) {
				is_delimiter = false;
				break;
			}

			// Peek at the next character
			advance(lexer);
		}

		// Check if this is the end delimiter (must be followed by newline or
		// EOF)
		if (is_delimiter &&
			(lexer->lookahead == '\n' || lexer->lookahead == '\r' ||
			 lexer->lookahead == 0)) {
			// This is the end delimiter - we should handle this in
			// scan_heredoc_end. Mark end and return false so the scanner
			// can try heredoc_end on the next iteration.
			lexer->markEnd(lexer);
			return false;
		}

		// If we advanced but it's not an end delimiter, we need to include
		// those characters in our heredoc_body
		if (i > 0) {
			lexer->markEnd(lexer);
			lexer->resultSymbol = HEREDOC_BODY;
			return true;
		}
	}

	// Consume characters until newline or EOF
	bool found_content = false;
	while (lexer->lookahead != 0) {
		found_content = true;
		advance(lexer);

		if (lexer->lookahead == '\n') {
			advance(lexer);
			break;
		}
	}

	if (!found_content && lexer->lookahead == 0)
		return false;

	lexer->markEnd(lexer);
	lexer->resultSymbol = HEREDOC_BODY;
	return true;
}

static bool scan_heredoc_end(Scanner *scanner, Lexer *lexer)
{
	if (!scanner->has_heredoc)
		return false;

	// Must be at the start of a line
	if (lexer->getColumn(lexer) != 0)
		return false;

	// Skip leading whitespace (supports indented end markers).
	while (lexer->lookahead == ' ' || lexer->lookahead == '\t')
		advance(lexer);

	// Check if this line matches the delimiter.
	for (int i = 0; i < scanner->delimiter_len; i++) {
		if (lexer->lookahead != scanner->delimiter[i])
			return false;

		advance(lexer);
	}

	// Mark the end right after the delimiter, any content after the delimiter
	// like a status code for example will remain for the grammar to handle.
	lexer->markEnd(lexer);

	// Only consume newline if it's immediately after the delimiter.
	// If there's other content, leave it for the grammar to handle.
	if (lexer->lookahead == '\n' || lexer->lookahead == '\r' ||
		lexer->lookahead == 0) {
		if (lexer->lookahead == '\n' || lexer->lookahead == '\r')
			advance(lexer);
	}

	scanner->has_heredoc = false;
	lexer->resultSymbol = HEREDOC_END;
	return true;
}

static bool scan(Scanner *scanner, Lexer *lexer, const bool *valid_symbols)
{
	// Check for heredoc end first
	if (valid_symbols[HEREDOC_END] && scan_heredoc_end(scanner, lexer)) {
		return true;
	}

	// Then check for heredoc body
	if (valid_symbols[HEREDOC_BODY] && scan_heredoc_body(scanner, lexer)) {
		return true;
	}

	// Finally check for heredoc start
	if (valid_symbols[HEREDOC_START] && scan_heredoc_start(scanner, lexer)) {
		return true;
	}

	return false;
}

void *Create()
{
	Scanner *scanner = scanner_calloc(1, sizeof(Scanner));
	return scanner;
}

static void Destroy(void *payload_)
{
    VoidPtr payload{payload_};
	Scanner *scanner = (Scanner *)payload;
	free(scanner);
}

static bool Scan(void *payload_, Lexer *lexer,
												 const bool *valid_symbols)
{
    VoidPtr payload{payload_};
	Scanner *scanner = (Scanner *)payload;
	return scan(scanner, lexer, valid_symbols);
}

static unsigned Serialize(void *payload_,
														  char *buffer)
{
    VoidPtr payload{payload_};
	Scanner *scanner = (Scanner *)payload;
	return serialize(scanner, buffer);
}

static void Deserialize(void *payload_,
														const char *buffer,
														unsigned length)
{
    VoidPtr payload{payload_};
	Scanner *scanner = (Scanner *)payload;
	deserialize(scanner, buffer, length);
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::caddy

NED_SCANNER_LIBRARY_EXPORT(caddy, ned::editor::languages::scanners::caddy::kScanner)
