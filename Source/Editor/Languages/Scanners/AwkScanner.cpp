// The awk external scanner, ported from https://github.com/Beaglefoot/tree-sitter-awk (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::awk {

using namespace ned::editor::parse::scanner;

enum TokenType
{
  CONCATENATING_SPACE,
  IF_ELSE_SEPARATOR,
  NO_SPACE,
  FUNC_CALL
};

static void tsawk_debug(Lexer *lexer)
{
  if (lexer->lookahead == '\r')
  {
    printf("column: %3" PRIu32 " | sym: '%c' | lookahead: '\\r' | skipped: %s\n",
           lexer->getColumn(lexer),
           lexer->resultSymbol,
           lexer->isAtIncludedRangeStart(lexer) ? "true" : "false");
    return;
  }

  if (lexer->lookahead == '\n')
  {
    printf("column: %3" PRIu32 " | sym: '%c' | lookahead: '\\n' | skipped: %s\n",
           lexer->getColumn(lexer),
           lexer->resultSymbol,
           lexer->isAtIncludedRangeStart(lexer) ? "true" : "false");
    return;
  }

  printf("column: %3" PRIu32 " | sym: '%c' | lookahead:  '%c' | skipped: %s\n",
         lexer->getColumn(lexer),
         lexer->resultSymbol,
         lexer->lookahead,
         lexer->isAtIncludedRangeStart(lexer) ? "true" : "false");
}

static bool tsawk_next_chars_eq(Lexer *lexer, char *word)
{
  for (int i = 0; i < strlen(word); i++)
  {
    if (lexer->lookahead != word[i])
    {
      return false;
    }

    lexer->advance(lexer, true);
  }
  return true;
}

static bool tsawk_is_whitespace(int32_t chr)
{
  return chr == ' ' || chr == '\t';
}

static bool tsawk_is_line_continuation(Lexer *lexer)
{
  if (lexer->lookahead == '\\')
  {
    lexer->advance(lexer, true);

    if (lexer->lookahead == '\r')
      lexer->advance(lexer, true);

    if (lexer->lookahead == '\n')
      return true;
  }

  return false;
}

static bool tsawk_is_statement_terminator(int32_t chr)
{
  return chr == '\n' || chr == ';';
}

static bool tsawk_skip_whitespace(Lexer *lexer, bool skip_newlines, bool capture)
{
  bool skipped = false;

  while (tsawk_is_whitespace(lexer->lookahead) || tsawk_is_line_continuation(lexer) || lexer->lookahead == '\r' || (skip_newlines && lexer->lookahead == '\n'))
  {
    lexer->advance(lexer, !capture);
    skipped = true;
  }

  return skipped;
}

static void tsawk_skip_comment(Lexer *lexer)
{
  if (lexer->lookahead != '#')
  {
    return;
  }

  while (lexer->lookahead != '\n' && !lexer->eof(lexer))
  {
    lexer->advance(lexer, true);
  }

  lexer->advance(lexer, false);

  tsawk_skip_whitespace(lexer, true, false);

  if (lexer->lookahead == '#')
  {
    tsawk_skip_comment(lexer);
  }
}

static bool tsawk_is_if_else_separator(Lexer *lexer)
{
  while (tsawk_is_whitespace(lexer->lookahead) || tsawk_is_statement_terminator(lexer->lookahead) || lexer->lookahead == '\r')
  {
    lexer->advance(lexer, true);
  }

  lexer->markEnd(lexer);

  if (lexer->lookahead == '#')
  {
    tsawk_skip_comment(lexer);
    tsawk_skip_whitespace(lexer, false, false);
  }

  return tsawk_next_chars_eq(lexer, "else");
}

static bool tsawk_is_concatenating_space(Lexer *lexer)
{
  bool had_whitespace = tsawk_skip_whitespace(lexer, false, true);

  lexer->markEnd(lexer);

  switch (lexer->lookahead)
  {
  case '^':
  case '*':
  case '/':
  case '%':
  case '+':
  case '-':
  case '<':
  case '>':
  case '=':
  case '!':
  case '~':
  case '&':
  case '|':
  case ',':
  case '?':
  case ':':
  case ')':
  case '[':
  case ']':
  case '{':
  case '}':
  case '#':
  case ';':
  case '\n':
    return false;
  case 'i':
    lexer->advance(lexer, true);

    if (lexer->lookahead == 'n' || lexer->lookahead == 'f')
    {
      lexer->advance(lexer, true);
      return lexer->lookahead != ' ';
    }
  default:
    return !lexer->eof(lexer);
  }
}

void *Create()
{
  return NULL;
}

static void Destroy(void *payload_)
{
    VoidPtr payload{payload_};
}

static unsigned Serialize(void *payload_, char *buffer)
{
    VoidPtr payload{payload_};
  return 0;
}

static void Deserialize(void *payload_, const char *state, unsigned length)
{
    VoidPtr payload{payload_};
}

static bool Scan(void *payload_, Lexer *lexer,
                                           const bool *valid_symbols)
{
    VoidPtr payload{payload_};
  bool statement_terminator_was_found = false;

  if (valid_symbols[NO_SPACE])
  {
    if (!tsawk_is_whitespace(lexer->lookahead))
    {
      lexer->resultSymbol = NO_SPACE;
      return true;
    }
  }

  if (valid_symbols[FUNC_CALL])
  {
    if (!tsawk_is_whitespace(lexer->lookahead) && lexer->lookahead == '(')
    {
      lexer->resultSymbol = FUNC_CALL;
      return true;
    }
  }

  if (valid_symbols[IF_ELSE_SEPARATOR])
  {
    tsawk_skip_whitespace(lexer, false, false);

    // Comment ends with '\n' which also terminates statement
    if (tsawk_is_statement_terminator(lexer->lookahead) || lexer->lookahead == '#')
    {
      statement_terminator_was_found = true;
    }

    if (tsawk_is_if_else_separator(lexer))
    {
      lexer->resultSymbol = IF_ELSE_SEPARATOR;
      return true;
    }
  }

  if (valid_symbols[CONCATENATING_SPACE] && !statement_terminator_was_found)
  {
    if (tsawk_is_concatenating_space(lexer))
    {
      lexer->resultSymbol = CONCATENATING_SPACE;
      return true;
    }
  }

  return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::awk

NED_SCANNER_LIBRARY_EXPORT(awk, ned::editor::languages::scanners::awk::kScanner)
