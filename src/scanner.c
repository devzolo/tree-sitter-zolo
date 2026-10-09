// External scanner for Zolo markup/raw-text boundaries plus two contextual
// tokens: the colon that starts a loop-label declaration
// (`:name loop|while|for`) and the `sink` convention before a parameter or
// argument name (see `scan_convention`).
//
// `_markup_lt` mirrors `markup_starts_here` in crates/zolo-lexer/src/lexer.rs,
// which is where the real compiler answers the same question: does this `<`
// open a markup element, or is it a comparison?
//
// The compiler answers it in the LEXER, and so must we. `<` is spent on
// comparison, on generics (`Foo<T>`) and on storage classes (`var<signal>`),
// so a plain `<` cannot be shared: inside a block, `let x = ""` followed by a
// `<div>` on the next line is a genuine shift/reduce tie between continuing
// the comparison and reducing the statement, and tree-sitter resolves it at
// GENERATION time using the comparison operator's precedence. That decision
// is not reachable from `conflicts` or `prec.dynamic` — both were tried, and
// the generator reported the conflicts as "unnecessary" because the shift had
// already won. The whole file then parsed as `"" < div`.
//
// Emitting a DISTINCT token removes the tie instead of trying to break it:
// where `_markup_lt` is produced, only markup can follow.
//
// The rule implemented is the first clause of `markup_starts_here`: a `<` on
// a NEW LINE opens markup. The clause's other half — an allowlist of
// preceding tokens (`{`, `(`, `[`, `,`, `:`, `=`, `=>`, `return`, `;`) — needs
// no scanner support, because in every one of those positions a comparison is
// not grammatical, so the ordinary `<` already resolves to markup on its own.
//
// `_style_raw_text` and `_script_raw_text` are a second, unrelated problem
// with the same shape: `<style>`/`<script>` are HTML raw text elements — the
// body is CSS/JS, not Zolo, so `{` must not open `markup_interpolation`
// there. Which of the two to scan for needs no state in this file: the
// grammar gives `<style>`/`<script>` their OWN tag-name token
// (`_style_tag_name`/`_script_tag_name` in grammar.js, not the generic
// `_markup_tag_name`), so the parser state — and therefore `valid_symbols` —
// genuinely differs per tag. That is what lets `scan_raw_text` below stay a
// single, tag-agnostic function for `SCRIPT_RAW_TEXT`: whichever of
// `STYLE_RAW_TEXT`/`SCRIPT_RAW_TEXT` is valid is the one the grammar is
// actually asking for. See grammar.js for why the tag-name token had to be
// dedicated (reusing the generic open tag made `valid_symbols[STYLE_RAW_TEXT]`
// true for every element, not just `<style>`, and this scanner swallowed the
// rest of the file looking for a `</style` that never came).
//
// `STYLE_RAW_TEXT` alone gets a SECOND function, `scan_style_raw_text`
// (specs/verniz-css.html §6.4): a live `@(` in a `<style>` body opens a Zolo
// expression (grammar.js `css_interpolation`), so that scan must ALSO stop
// early there — but only when the `@(` sits outside a CSS string or
// comment, which is why it tracks CSS quote/comment state that
// `scan_raw_text` never needed. `<script>` gets no equivalent: JS has its
// own `@decorator` syntax, so `SCRIPT_RAW_TEXT` keeps using the original,
// simpler `scan_raw_text`. Both functions must keep agreeing with
// `next_raw_text_token` in crates/zolo-lexer/src/lexer.rs, which is the
// oracle for all of this (the close-tag boundary check AND, for style, the
// quote/comment-tracking chunk scan and the `is_style && b == '@' &&
// peek2() == '(' ` one-byte lookahead).

#include "tree_sitter/parser.h"

#include <string.h>
#include <stdlib.h>
#include <wctype.h>

enum TokenType {
  MARKUP_LT,
  STYLE_RAW_TEXT,
  SCRIPT_RAW_TEXT,
  LOOP_LABEL_DECL_COLON,
  CONVENTION,
  FOREIGN_BODY,
  FOREIGN_GROUP_OPEN,
  PYTHON_FOREIGN_BODY,
  FOREIGN_PROVIDER,
  FOREIGN_IMPORT,
  FOREIGN_SCOPE_OPEN,
  FOREIGN_SCOPE_CLOSE,
  JAVASCRIPT_FOREIGN_BODY,
  TYPESCRIPT_FOREIGN_BODY,
  JAVA_FOREIGN_BODY,
  KOTLIN_FOREIGN_BODY,
  C_FOREIGN_BODY,
  CPP_FOREIGN_BODY,
  RUST_FOREIGN_BODY,
  GO_FOREIGN_BODY,
  DEPENDENCIES_KEYWORD,
  RECORD_UPDATE_WITH,
  HANDLE_SEPARATOR,
  NAMED_CALLBACK_START,
  TRAILING_CALLBACK_PIPE,
  CALLBACK_END,
  RECOVER_KEYWORD,
  NON_DOT_EXPRESSION_START,
  TITLED_RAW_TITLE,
  TITLED_RAW_TRIPLE_TITLE,
  // MUST stay last. During error recovery tree-sitter calls this scanner
  // with EVERY entry of `valid_symbols` set to true, regardless of what the
  // grammar actually expects at that position — that is how error recovery
  // probes for a token that lets it resynchronize. Without a way to detect
  // that mode, `scan_raw_text` ran at ANY error position (a stray `)`, a
  // missing `}`, ...), matched `valid_symbols[STYLE_RAW_TEXT]` /
  // `[SCRIPT_RAW_TEXT]` unconditionally, and consumed everything up to the
  // next `</style`/`</script` or EOF as a single `markup_raw_text` token —
  // observed turning a 3-byte `ERROR` node into one spanning the rest of the
  // file. `ERROR_SENTINEL` is never a real grammar symbol (nothing in
  // grammar.js references it), so it is false during ordinary parsing and
  // true ONLY during this recovery probe; bailing on it keeps the scanner
  // silent exactly when it must not guess.
  CONTEXTUAL_SUFFIX_START,
  YIELD_EMPTY_END,
  YIELD_PAYLOAD_START,
  RANGE_OPERATOR_START,
  IS_TYPE_START,
  TEMPORAL_CALL_END,
  AWAIT_NAMED_CALLBACK_START,
  AWAIT_WITHIN_CALL_END,
  NOMINAL_NTL_START,
  NOMINAL_RESOURCE_START,
  EVERY_INTERVAL_START,
  PRATT_END0,
  PRATT_END1,
  PRATT_END2,
  PRATT_END10,
  PRATT_END20,
  PRATT_END27,
  NOMINAL_UNRESTRICTED_START,
  PRATT_BRACED_PAYLOAD_START,
  PRATT_LOW_TAIL_START,
  SCOPE_CLEAR_NHA_ENTER0,
  SCOPE_ARGS_END,
  SCOPE_CLEAR_NHA_RESTORE0,
  SCOPE_CLEAR_NHA_ENTER1,
  SCOPE_CLEAR_NHA_RESTORE1,
  SCOPE_CLEAR_NHA_ENTER2,
  SCOPE_CLEAR_NHA_RESTORE2,
  SCOPE_CLEAR_NHA_ENTER3,
  SCOPE_CLEAR_NHA_RESTORE3,
  SCOPE_CLEAR_NHA_ENTER4,
  SCOPE_CLEAR_NHA_RESTORE4,
  SCOPE_CLEAR_NHA_ENTER5,
  SCOPE_CLEAR_NHA_RESTORE5,
  SCOPE_CLEAR_NHA_ENTER6,
  SCOPE_CLEAR_NHA_RESTORE6,
  SCOPE_CLEAR_NHA_ENTER7,
  SCOPE_CLEAR_NHA_RESTORE7,
  SCOPE_SET_N_ENTER0,
  SCOPE_SET_N_RESTORE0,
  SCOPE_SET_N_ENTER1,
  SCOPE_SET_N_RESTORE1,
  SCOPE_PAREN_ENTER0,
  SCOPE_DELIMITED_END,
  SCOPE_PAREN_RESTORE0,
  SCOPE_PAREN_ENTER1,
  SCOPE_PAREN_RESTORE1,
  SCOPE_PAREN_ENTER2,
  SCOPE_PAREN_RESTORE2,
  SCOPE_PAREN_ENTER3,
  SCOPE_PAREN_RESTORE3,
  SCOPE_PAREN_ENTER4,
  SCOPE_PAREN_RESTORE4,
  SCOPE_PAREN_ENTER5,
  SCOPE_PAREN_RESTORE5,
  SCOPE_PAREN_ENTER6,
  SCOPE_PAREN_RESTORE6,
  SCOPE_PAREN_ENTER7,
  SCOPE_PAREN_RESTORE7,
  HYBRID_NOMINAL_START,
  SCOPE_CLEAR_N_ENTER0,
  SCOPE_CLEAR_N_RESTORE0,
  SCOPE_CLEAR_N_ENTER1,
  SCOPE_CLEAR_N_RESTORE1,
  SCOPE_SET_NH_ENTER0,
  SCOPE_SET_NH_RESTORE0,
  SCOPE_SET_NH_ENTER1,
  SCOPE_SET_NH_RESTORE1,
  SCOPE_SET_NH_ENTER2,
  SCOPE_SET_NH_RESTORE2,
  SCOPE_SET_NH_ENTER3,
  SCOPE_SET_NH_RESTORE3,
  SCOPE_SET_N_R_CLEAR_H_ENTER0,
  SCOPE_SET_N_R_CLEAR_H_RESTORE0,
  SCOPE_SET_N_R_CLEAR_H_ENTER1,
  SCOPE_SET_N_R_CLEAR_H_RESTORE1,
  SCOPE_SET_N_R_CLEAR_H_ENTER2,
  SCOPE_SET_N_R_CLEAR_H_RESTORE2,
  SCOPE_SET_N_R_CLEAR_H_ENTER3,
  SCOPE_SET_N_R_CLEAR_H_RESTORE3,
  SCOPE_SET_N_R_CLEAR_H_ENTER8,
  SCOPE_SET_N_R_CLEAR_H_RESTORE8,
  SCOPE_SET_N_R_CLEAR_H_ENTER9,
  SCOPE_SET_N_R_CLEAR_H_RESTORE9,
  SCOPE_SET_N_R_CLEAR_H_ENTER10,
  SCOPE_SET_N_R_CLEAR_H_RESTORE10,
  SCOPE_SET_N_R_CLEAR_H_ENTER11,
  SCOPE_SET_N_R_CLEAR_H_RESTORE11,
  SCOPE_SET_A_ENTER0,
  SCOPE_SET_A_RESTORE0,
  SCOPE_SET_A_ENTER4,
  SCOPE_SET_A_RESTORE4,
  NUMERIC_DURATION_START,
  NUMERIC_INTEGER_START,
  NUMERIC_FLOAT_START,
  NUMERIC_DECIMAL_START,
  NUMERIC_BIGINT_START,
  NUMERIC_UNIT_START,
  NUMERIC_LITERAL_END,
  CONDITION_CHAIN_COMMA,
  APPROX_MODE_START,
  ATTEMPT_NOMINAL_START,
  BLOCK_COMMENT,
  IF_ELSE_KEYWORD,
  ATTEMPT_BLOCK_START,
  MAP_IF_CONDITION_START,
  ERROR_SENTINEL,
};

// Imported provider names stay ordinary identifiers. Track only their lexical
// dialect; loading a native plugin is neither needed nor allowed by a parser.
// The bounded table fits tree-sitter's 1024-byte scanner-state buffer.
#define MAX_FOREIGN_BINDINGS 90
enum ForeignDialect { FOREIGN_BRACED, FOREIGN_PYTHON, FOREIGN_JAVASCRIPT, FOREIGN_TYPESCRIPT, FOREIGN_JAVA, FOREIGN_KOTLIN, FOREIGN_C, FOREIGN_CPP, FOREIGN_RUST, FOREIGN_GO };
typedef struct {
  uint64_t name;
  uint16_t scope;
  uint8_t dialect;
} ForeignBinding;
typedef struct {
  ForeignBinding bindings[MAX_FOREIGN_BINDINGS];
  uint8_t count;
  uint16_t scope;
  uint8_t dialect;
  uint8_t expr_flags, scope_heartbeat, comment_callback_newline;
} Scanner;

void *tree_sitter_zolo_external_scanner_create(void) {
  return calloc(1, sizeof(Scanner));
}

void tree_sitter_zolo_external_scanner_destroy(void *payload) { free(payload); }

unsigned tree_sitter_zolo_external_scanner_serialize(void *payload,
                                                     char *buffer) {
  Scanner *scanner = payload;
  unsigned length = 0;
  buffer[length++] = scanner->scope & 0xff;
  buffer[length++] = scanner->scope >> 8;
  buffer[length++] = scanner->dialect;
  buffer[length++] = scanner->count;
  for (unsigned i = 0; i < scanner->count; i++) {
    ForeignBinding *binding = &scanner->bindings[i];
    buffer[length++] = binding->scope & 0xff;
    buffer[length++] = binding->scope >> 8;
    buffer[length++] = binding->dialect;
    for (unsigned byte = 0; byte < 8; byte++) {
      buffer[length++] = (binding->name >> (byte * 8)) & 0xff;
    }
  }
  buffer[length++] = (char)(scanner->expr_flags | (scanner->scope_heartbeat << 4) | (scanner->comment_callback_newline << 6));
  return length;
}

void tree_sitter_zolo_external_scanner_deserialize(void *payload,
                                                   const char *buffer,
                                                   unsigned length) {
  Scanner *scanner = payload;
  memset(scanner, 0, sizeof(Scanner));
  if (length < 5) return;
  unsigned offset = 0;
  scanner->scope = (uint8_t)buffer[offset++];
  scanner->scope |= (uint16_t)(uint8_t)buffer[offset++] << 8;
  scanner->dialect = buffer[offset++];
  unsigned count = (uint8_t)buffer[offset++];
  if (count > MAX_FOREIGN_BINDINGS) return;
  for (unsigned i = 0; i < count; i++) {
    if (offset + 11 > length - 1) return;
    ForeignBinding *binding = &scanner->bindings[i];
    binding->scope = (uint8_t)buffer[offset++];
    binding->scope |= (uint16_t)(uint8_t)buffer[offset++] << 8;
    binding->dialect = buffer[offset++];
    for (unsigned byte = 0; byte < 8; byte++) {
      binding->name |= (uint64_t)(uint8_t)buffer[offset++] << (byte * 8);
    }
    scanner->count++;
  }
  if (offset < length) { scanner->expr_flags = (uint8_t)buffer[offset] & 15; scanner->scope_heartbeat = ((uint8_t)buffer[offset] >> 4) & 3; scanner->comment_callback_newline=((uint8_t)buffer[offset]>>6)&1; }
}

/// True for the characters that can follow the `<` of an OPENING tag: a tag
/// name, or `>` for the fragment `<>`.
///
/// Narrower than the lexer's `at_tag_open`, on purpose. That function answers
/// "is this `<` markup at all", so it also accepts `/` (a closing tag) and
/// `!-` (a comment). Neither belongs here: `</` and `<!--` are unambiguous
/// tokens that no comparison could ever claim, so the grammar lexes them
/// directly. Accepting `/` here made the scanner swallow the `<` of every
/// `</ul>` that followed a newline, leaving the close tag unparsable.
static inline bool opens_a_tag(int32_t c) {
  return iswalpha(c) || c == '_' || c == '>';
}

/// True for the ASCII whitespace bytes `at_raw_close` accepts via
/// `u8::is_ascii_whitespace` (space, tab, LF, FF, CR).
static inline bool is_ascii_ws(int32_t c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static inline bool is_label_start(int32_t c) {
  // Keep this identical to the compiler lexer and grammar.js identifier
  // contract. Using the locale-sensitive wide-character predicates here
  // made the external scanner accept a declaration prefix that the following
  // `identifier` node could never consume.
  return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static inline bool is_label_continue(int32_t c) {
  return is_label_start(c) || (c >= '0' && c <= '9');
}

static void skip_ascii_ws(TSLexer *lexer) {
  while (is_ascii_ws(lexer->lookahead)) {
    lexer->advance(lexer, true);
  }
}

static bool scan_word(TSLexer *lexer, const char *word) {
  for (const char *cursor = word; *cursor != '\0'; cursor++) {
    if (lexer->lookahead != (int32_t)*cursor) {
      return false;
    }
    lexer->advance(lexer, false);
  }
  return !is_label_continue(lexer->lookahead);
}

/// Scan only the `:` that starts a loop-label declaration, while validating
/// the complete `:name loop|for|while` prefix through lookahead. The token's
/// marked end remains after the colon, so the identifier is still a normal
/// named syntax node and can participate in highlight/local queries. Looking
/// for the following loop keyword distinguishes a new `:search loop`
/// expression from a type annotation such as `let value: Type`.
static bool scan_loop_label_decl_colon(TSLexer *lexer) {
  if (lexer->lookahead != ':') {
    return false;
  }
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  if (!is_label_start(lexer->lookahead)) {
    return false;
  }
  do {
    lexer->advance(lexer, false);
  } while (is_label_continue(lexer->lookahead));

  skip_ascii_ws(lexer);
  switch (lexer->lookahead) {
  case 'l':
    return scan_word(lexer, "loop");
  case 'f':
    return scan_word(lexer, "for");
  case 'w':
    return scan_word(lexer, "while");
  default:
    return false;
  }
}

/// The words `lookup_keyword` in crates/zolo-lexer/src/keywords.rs turns into
/// keyword tokens, minus `self`. Keep the two lists identical.
static const char *const RESERVED_WORDS[] = {
    "let",      "mut",      "var",       "const",   "const_assert", "override",
    "enable",   "requires", "fn",        "return",  "if",           "else",
    "for",      "while",    "loop",      "break",   "continue",     "match",
    "enum",     "struct",   "impl",      "trait",   "mod",          "use",
    "pub",      "in",       "as",        "is",      "where",        "nil",
    "true",     "false",    "type",      "newtype", "comptime",     "async",
    "await",    "yield",    "spawn",     "scope",   "select",       "every",
    "after",    "timeout",  "sleep",     "try",     "catch",        "finally",
    "defer",    "defer_ok", "defer_err", "guard",   "macro",        "on",
    "schema",   "machine",  "effect",    "handle",  "perform",      "with",
    "using",    "extern",
};

static bool is_reserved_word(const char *word) {
  for (size_t i = 0; i < sizeof(RESERVED_WORDS) / sizeof(RESERVED_WORDS[0]); i++) {
    if (strcmp(word, RESERVED_WORDS[i]) == 0) {
      return true;
    }
  }
  return false;
}

/// `sink`, the parameter convention (specs/linear-affine-types-innovations.html
/// §0.3), before a parameter name or `self` (`fn commit(sink self)`) or a call
/// argument (`close(sink f)`). The rule is `eat_param_convention` in
/// crates/zolo-parser/src/parser.rs: a keyword only when the NEXT token is an
/// identifier or `self`, so `let sink = 3`, `fn f(sink: Sink)`, `f(sink)`,
/// `f(sink + 1)` and `f(sink as int)` keep `sink` a name. A grammar keyword
/// cannot do that: with `word: $ => $.identifier`, every state that accepts
/// the keyword would lex `sink` as it. The token is `sink` alone — the name
/// after it is a peek past `mark_end`, so it stays its own node.
static bool scan_convention(TSLexer *lexer) {
  if (!scan_word(lexer, "sink")) {
    return false;
  }
  lexer->mark_end(lexer);
  if (!is_ascii_ws(lexer->lookahead)) {
    return false;
  }
  // Peek, not skip: `advance(_, true)` after the token's own bytes moves the
  // token START past them, leaving a zero-width `convention` node.
  while (is_ascii_ws(lexer->lookahead)) {
    lexer->advance(lexer, false);
  }
  if (!is_label_start(lexer->lookahead)) {
    return false;
  }
  char word[16];
  size_t length = 0;
  do {
    if (length + 1 < sizeof(word)) {
      word[length] = (char)lexer->lookahead;
    }
    length++;
    lexer->advance(lexer, false);
  } while (is_label_continue(lexer->lookahead));
  // A string prefix (`f"…"`, `sh"…"`) is a literal, not a name.
  if (lexer->lookahead == '"' || lexer->lookahead == '\'') {
    return false;
  }
  if (length + 1 > sizeof(word)) {
    // Longer than every reserved word: a name.
    return true;
  }
  word[length] = '\0';
  return strcmp(word, "self") == 0 || !is_reserved_word(word);
}

/// True when `lexer` sits right after a candidate close name (`/style`,
/// `/script`) at a byte that can legally follow a tag name: `>` (`</style>`),
/// `/` (a stray `</style/>`, never valid Zolo but not this function's job to
/// reject), ASCII whitespace (`</style >`), or EOF.
///
/// This is the oracle: `at_raw_close` in
/// `crates/zolo-lexer/src/lexer.rs`. The two must agree, on pain of
/// repeating the exact bug `raw_text_close_tag_requires_name_boundary`
/// (same file) already regression-tests for the compiler — matching only the
/// `n` bytes of `close` and declaring victory is not enough, because
/// `</script` is also a syntactic PREFIX of `</scripts>`. Without this check
/// `<script>console.log(1);</scripts>;</script>` — a real Zolo program,
/// where `</scripts>` is just a string a script happens to contain — ends
/// the raw-text body at the wrong `<`, and the real `</script>` a few bytes
/// later is orphaned.
static inline bool at_close_boundary(TSLexer *lexer) {
  return lexer->eof(lexer) || lexer->lookahead == '>' ||
         lexer->lookahead == '/' || is_ascii_ws(lexer->lookahead);
}

/// The body of a raw text element: runs until an EXACT, case-sensitive
/// `</style` or `</script` — matching `is_raw_text_tag` in
/// `crates/zolo-lexer/src/lexer.rs`, which is exact too. A Zolo tag is an
/// identifier that resolves to a function, not an HTML element name, so
/// `<STYLE>` and `<Style>` are NOT raw text here any more than they are in
/// the compiler; only literal lowercase `<style>`/`<script>` reach this
/// scanner at all (`_style_tag_name`/`_script_tag_name` in grammar.js are
/// exact-match tokens too). `close` is always given here in lowercase.
///
/// Which of the two to look for needs no state in the scanner — the grammar
/// has one rule per raw-text tag, so `valid_symbols` already carries the
/// distinction: whichever branch the parser is actually pursuing is the one
/// asking for its own token. That is what saves the serialize/deserialize
/// tree-sitter-html needs to remember which tag it is inside.
static bool scan_raw_text(TSLexer *lexer, const char *close) {
  size_t n = strlen(close);
  bool any = false;
  for (;;) {
    if (lexer->eof(lexer)) {
      break;
    }
    if (lexer->lookahead == '<') {
      lexer->mark_end(lexer);
      lexer->advance(lexer, false);
      size_t i = 0;
      while (i < n && lexer->lookahead == (int32_t)close[i]) {
        lexer->advance(lexer, false);
        i++;
      }
      if (i == n && at_close_boundary(lexer)) {
        // `mark_end` landed BEFORE the `<`, so the token stops here and the
        // `</style>`/`</script>` is tokenized through the ordinary grammar
        // path (`markup_close_tag`).
        return any;
      }
      // Either a partial match (i < n), or a full match on the name that
      // is not actually followed by a tag boundary (`</scripts>`,
      // `</style-ish>`) — in both cases this was not the close tag, so
      // fall through and keep scanning as ordinary raw text.
      any = true;
      continue;
    }
    lexer->advance(lexer, false);
    any = true;
    lexer->mark_end(lexer);
  }
  return any;
}

/// `<style>`-only variant of `scan_raw_text`: same close-tag scan, PLUS a
/// stop at a live `@(` (the CSS interpolation escape, grammar.js
/// `css_interpolation`) and CSS quote/comment tracking so a `@(` written
/// inside a string or a comment does NOT stop the scan there — the oracle
/// is the chunk-scan loop in `next_raw_text_token`
/// (crates/zolo-lexer/src/lexer.rs), which this mirrors condition for
/// condition:
///
///   - The close-tag check runs FIRST, unconditionally — even mid-string or
///     mid-comment, exactly like the lexer's `while ... &&
///     !self.at_raw_close(name)` loop condition, which is checked before the
///     `in_comment`/`quote` branches. A literal `</style` always wins.
///   - Inside a `/* … */` comment, everything is skipped verbatim until the
///     matching `*/` — including a `@(` — mirroring the lexer's `in_comment`
///     branch.
///   - Inside a `'…'`/`"…"` CSS string, `\` escapes the next byte, and the
///     string ends at a matching quote OR an unescaped newline (real CSS
///     strings cannot span lines) — mirroring the lexer's `quote` branch,
///     including a `@(` written inside the string.
///   - Outside both, a `@` immediately followed by `(` (no whitespace
///     tolerated) ends the chunk right before the `@`, mirroring
///     `is_style && b == '@' && self.peek2() == b'(' ` in the lexer. A
///     zero-width result here (nothing consumed before the `@(`) is
///     expected and correct — e.g. `<style>@(x)</style>` has no CSS before
///     the interpolation — `grammar.js`'s `repeat($._style_body_part)`
///     is what lets `css_interpolation` follow immediately with no raw-text
///     node in between (see `scan_raw_text` above for the same zero-width
///     shape at an immediate `</style`).
static bool scan_style_raw_text(TSLexer *lexer, const char *close) {
  size_t n = strlen(close);
  bool any = false;
  bool in_comment = false;
  int32_t quote = 0; // 0 = not in a CSS string, else the quote byte ('\'' or '"')

  for (;;) {
    if (lexer->eof(lexer)) {
      break;
    }

    // Close tag: always checked first, even mid-string/mid-comment — see
    // the function doc comment.
    if (lexer->lookahead == '<') {
      lexer->mark_end(lexer);
      lexer->advance(lexer, false);
      size_t i = 0;
      while (i < n && lexer->lookahead == (int32_t)close[i]) {
        lexer->advance(lexer, false);
        i++;
      }
      if (i == n && at_close_boundary(lexer)) {
        return any;
      }
      any = true;
      continue;
    }

    if (in_comment) {
      if (lexer->lookahead == '*') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '/') {
          lexer->advance(lexer, false);
          in_comment = false;
        }
      } else {
        lexer->advance(lexer, false);
      }
      any = true;
      lexer->mark_end(lexer);
      continue;
    }

    if (quote != 0) {
      if (lexer->lookahead == '\\') {
        lexer->advance(lexer, false); // the backslash; the escaped byte falls through below
        if (!lexer->eof(lexer)) {
          lexer->advance(lexer, false);
        }
      } else {
        if (lexer->lookahead == quote || lexer->lookahead == '\n') {
          quote = 0;
        }
        lexer->advance(lexer, false);
      }
      any = true;
      lexer->mark_end(lexer);
      continue;
    }

    // Live `@(` outside a string/comment: stop here (possibly zero-width)
    // and let grammar.js's `css_interpolation` take over.
    if (lexer->lookahead == '@') {
      lexer->mark_end(lexer);
      lexer->advance(lexer, false);
      if (lexer->lookahead == '(') {
        return any;
      }
      // Not `@(` (e.g. an at-rule like `@media`) — ordinary content.
      any = true;
      lexer->mark_end(lexer);
      continue;
    }

    if (lexer->lookahead == '/') {
      lexer->advance(lexer, false); // the `/`; a following `*` falls through below
      if (lexer->lookahead == '*') {
        lexer->advance(lexer, false);
        in_comment = true;
      }
      any = true;
      lexer->mark_end(lexer);
      continue;
    }

    if (lexer->lookahead == '"' || lexer->lookahead == '\'') {
      quote = lexer->lookahead;
      lexer->advance(lexer, false);
      any = true;
      lexer->mark_end(lexer);
      continue;
    }

    lexer->advance(lexer, false);
    any = true;
    lexer->mark_end(lexer);
  }
  return any;
}

// Foreign source uses balanced braces, but braces inside comments and strings
// do not delimit the Zolo construct. Match the compiler's opaque-body scanner;
// no C#/Java/JS tokens are ever handed to the surrounding Zolo grammar.
// Mirror zolo-lexer::foreign::scan_ecmascript's structural context. Native
// providers validate syntax; this scanner only keeps authored literals opaque.
enum EcmaBrace { ECMA_BLOCK, ECMA_OBJECT, ECMA_CALLABLE_DECL, ECMA_CALLABLE_EXPR, ECMA_CLASS_DECL, ECMA_CLASS_EXPR };
enum EcmaParen { ECMA_NORMAL, ECMA_CONTROL, ECMA_FUNCTION_DECL, ECMA_FUNCTION_EXPR };
#define MAX_ECMA_DEPTH 256
static bool scan_ecma_code(TSLexer *lexer, bool interpolation, bool statement, unsigned recursion);

static bool ecma_identifier_start(int32_t c) {
  return is_label_start(c) || c == '$' || c >= 128;
}
static bool ecma_identifier_part(int32_t c) {
  return ecma_identifier_start(c) || (c >= '0' && c <= '9');
}
static void scan_ecma_regex(TSLexer *lexer) {
  bool character_class = false;
  while (!lexer->eof(lexer)) {
    int32_t c = lexer->lookahead;
    if (c == '\n' || c == '\r') return;
    lexer->advance(lexer, false);
    if (c == '\\') {
      if (!lexer->eof(lexer)) lexer->advance(lexer, false);
    } else if (c == '[') {
      character_class = true;
    } else if (c == ']') {
      character_class = false;
    } else if (c == '/' && !character_class) {
      while (ecma_identifier_part(lexer->lookahead)) lexer->advance(lexer, false);
      return;
    }
  }
}
static void scan_ecma_string(TSLexer *lexer, int32_t quote, unsigned recursion) {
  while (!lexer->eof(lexer)) {
    int32_t c = lexer->lookahead;
    lexer->advance(lexer, false);
    if (c == '\\') {
      if (!lexer->eof(lexer)) lexer->advance(lexer, false);
    } else if (c == quote) {
      return;
    } else if (quote == '`' && c == '$' && lexer->lookahead == '{') {
      lexer->advance(lexer, false);
      scan_ecma_code(lexer, true, false, recursion + 1);
    }
  }
}
static bool scan_ecma_code(TSLexer *lexer, bool interpolation, bool statement, unsigned recursion) {
  if (recursion > 64) return false;
  bool expression_expected = true, block_expected = statement, control_pending = false;
  bool member_pending = false, arrow_pending = false, any = false;
  // Pending function/class/callable: 0 none, 1 declaration, 2 expression.
  uint8_t function_pending = 0, class_pending = 0, callable_pending = 0;
  unsigned class_braces = 0, class_parens = 0, brace_count = 0, paren_count = 0;
  uint8_t braces[MAX_ECMA_DEPTH], parentheses[MAX_ECMA_DEPTH];
  while (!lexer->eof(lexer)) {
    int32_t c = lexer->lookahead;
    if (c == '}' && !brace_count) {
      if (interpolation) lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      return any;
    }
    lexer->advance(lexer, false); any = true;
    if (is_ascii_ws(c)) { lexer->mark_end(lexer); continue; }
    if (c == '/' && lexer->lookahead == '/') {
      while (!lexer->eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '\r') lexer->advance(lexer, false);
      lexer->mark_end(lexer); continue;
    }
    if (c == '/' && lexer->lookahead == '*') {
      lexer->advance(lexer, false);
      while (!lexer->eof(lexer)) {
        int32_t comment = lexer->lookahead; lexer->advance(lexer, false);
        if (comment == '*' && lexer->lookahead == '/') { lexer->advance(lexer, false); break; }
      }
      lexer->mark_end(lexer); continue;
    }
    if (c != '{' && arrow_pending) { callable_pending = 0; arrow_pending = false; }
    if (c == '\'' || c == '"' || c == '`') {
      scan_ecma_string(lexer, c, recursion);
      expression_expected = block_expected = member_pending = false;
      lexer->mark_end(lexer); continue;
    }
    if (c == '/') {
      if (expression_expected) { scan_ecma_regex(lexer); expression_expected = false; }
      else { if (lexer->lookahead == '=') lexer->advance(lexer, false); expression_expected = true; }
      block_expected = member_pending = false;
      lexer->mark_end(lexer); continue;
    }
    if (ecma_identifier_start(c)) {
      char word[48]; unsigned length = 0; word[length++] = c < 128 ? c : '?';
      while (ecma_identifier_part(lexer->lookahead)) {
        if (length < sizeof(word) - 1) word[length++] = lexer->lookahead < 128 ? lexer->lookahead : '?';
        lexer->advance(lexer, false);
      }
      word[length] = 0;
      while (is_ascii_ws(lexer->lookahead)) lexer->advance(lexer, false);
      bool property_key = brace_count && (braces[brace_count - 1] == ECMA_OBJECT ||
        braces[brace_count - 1] == ECMA_CLASS_DECL || braces[brace_count - 1] == ECMA_CLASS_EXPR) && lexer->lookahead == ':';
      if (member_pending || property_key) expression_expected = block_expected = false;
      else if (!strcmp(word, "function")) {
        function_pending = block_expected ? 1 : 2; expression_expected = true; block_expected = false;
      } else if (!strcmp(word, "class")) {
        class_pending = block_expected ? 1 : 2; class_braces = brace_count; class_parens = paren_count;
        expression_expected = true; block_expected = false;
      } else if (block_expected && (!strcmp(word, "async") || !strcmp(word, "export") || !strcmp(word, "default"))) {
        expression_expected = true;
      } else if (!strcmp(word, "if") || !strcmp(word, "while") || !strcmp(word, "for") || !strcmp(word, "with") || !strcmp(word, "switch") || !strcmp(word, "catch")) {
        control_pending = true; expression_expected = true; block_expected = !strcmp(word, "catch");
      } else if (!strcmp(word, "else") || !strcmp(word, "do") || !strcmp(word, "try") || !strcmp(word, "finally")) {
        expression_expected = block_expected = true;
      } else if (!strcmp(word, "return") || !strcmp(word, "throw") || !strcmp(word, "case") || !strcmp(word, "delete") || !strcmp(word, "void") || !strcmp(word, "typeof") || !strcmp(word, "new") || !strcmp(word, "in") || !strcmp(word, "instanceof") || !strcmp(word, "of") || !strcmp(word, "yield") || !strcmp(word, "await")) {
        expression_expected = true; block_expected = false;
      } else expression_expected = block_expected = false;
      member_pending = false; lexer->mark_end(lexer); continue;
    }
    if (c >= '0' && c <= '9') {
      while (ecma_identifier_part(lexer->lookahead) || lexer->lookahead == '.') lexer->advance(lexer, false);
      expression_expected = block_expected = member_pending = false;
      lexer->mark_end(lexer); continue;
    }
    switch (c) {
    case '(':
      if (paren_count == MAX_ECMA_DEPTH) return false;
      parentheses[paren_count++] = control_pending ? ECMA_CONTROL : function_pending == 1 ? ECMA_FUNCTION_DECL : function_pending == 2 ? ECMA_FUNCTION_EXPR : ECMA_NORMAL;
      if (!control_pending) function_pending = 0;
      control_pending = false; expression_expected = true; block_expected = false;
      break;
    case ')': {
      uint8_t parenthesis = paren_count ? parentheses[--paren_count] : ECMA_NORMAL;
      expression_expected = parenthesis == ECMA_CONTROL;
      if (parenthesis == ECMA_FUNCTION_DECL || parenthesis == ECMA_FUNCTION_EXPR) callable_pending = parenthesis == ECMA_FUNCTION_DECL ? 1 : 2;
      block_expected = true; break;
    }
    case '{': {
      if (brace_count == MAX_ECMA_DEPTH) return false;
      uint8_t brace;
      if (callable_pending) { brace = callable_pending == 1 ? ECMA_CALLABLE_DECL : ECMA_CALLABLE_EXPR; callable_pending = 0; arrow_pending = false; }
      else if (class_pending && class_braces == brace_count && class_parens == paren_count) { brace = class_pending == 1 ? ECMA_CLASS_DECL : ECMA_CLASS_EXPR; class_pending = 0; }
      else brace = block_expected || !expression_expected ? ECMA_BLOCK : ECMA_OBJECT;
      braces[brace_count++] = brace; expression_expected = block_expected = true; break;
    }
    case '}': {
      uint8_t brace = braces[--brace_count];
      expression_expected = brace == ECMA_BLOCK || brace == ECMA_CALLABLE_DECL || brace == ECMA_CLASS_DECL;
      block_expected = expression_expected; break;
    }
    case ']': expression_expected = block_expected = false; break;
    case ';':
      expression_expected = block_expected = true; control_pending = false;
      function_pending = class_pending = callable_pending = 0; arrow_pending = false; break;
    case '.':
      if (lexer->lookahead == '.') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '.') lexer->advance(lexer, false);
        expression_expected = true; member_pending = false;
      } else { expression_expected = false; member_pending = true; }
      block_expected = false; lexer->mark_end(lexer); continue;
    case '#': member_pending = true; expression_expected = block_expected = false; lexer->mark_end(lexer); continue;
    case '+': case '-':
      if (lexer->lookahead == c) { lexer->advance(lexer, false); block_expected = member_pending = false; lexer->mark_end(lexer); continue; }
      expression_expected = true; block_expected = false; break;
    case '!':
      if (lexer->lookahead == '=') expression_expected = true;
      block_expected = false; break;
    case '=':
      expression_expected = true;
      if (lexer->lookahead == '>') { lexer->advance(lexer, false); block_expected = true; callable_pending = 2; arrow_pending = true; }
      else block_expected = false;
      break;
    default: expression_expected = true; block_expected = false;
    }
    member_pending = false; lexer->mark_end(lexer);
  }
  return any;
}

static bool scan_foreign_body(TSLexer *lexer, bool python) {
  unsigned depth = 0;
  bool any = false;
  while (!lexer->eof(lexer)) {
    int32_t c = lexer->lookahead;
    if (c == '}' && depth == 0) {
      lexer->mark_end(lexer);
      return any;
    }
    lexer->advance(lexer, false);
    any = true;
    if (c == '{') {
      depth++;
    } else if (c == '}') {
      depth--;
    } else if (c == '#' && python) {
      while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
        lexer->advance(lexer, false);
      }
    } else if (c == '/' && !python) {
      if (lexer->lookahead == '/') {
        while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
          lexer->advance(lexer, false);
        }
      } else if (lexer->lookahead == '*') {
        lexer->advance(lexer, false);
        unsigned comments = 1;
        while (comments && !lexer->eof(lexer)) {
          int32_t comment = lexer->lookahead;
          lexer->advance(lexer, false);
          if (comment == '/' && lexer->lookahead == '*') {
            lexer->advance(lexer, false);
            comments++;
          } else if (comment == '*' && lexer->lookahead == '/') {
            lexer->advance(lexer, false);
            comments--;
          }
        }
      }
    } else {
      bool verbatim = !python && c == '@' && lexer->lookahead == '"';
      if (verbatim) {
        c = lexer->lookahead;
        lexer->advance(lexer, false);
      }
      if (c == '"' || c == '\'' || c == '`') {
        unsigned quotes = 1;
        if ((c == '"' || (c == '\'' && python)) && !verbatim) {
          while (lexer->lookahead == c && (!python || quotes < 3)) {
            quotes++;
            lexer->advance(lexer, false);
          }
        }
        // Two opening quotes form an empty ordinary string. Three or more
        // are a C#/Java/Python raw string delimiter with an exact quote run.
        if (quotes != 2) {
          unsigned closing = 0;
          while (!lexer->eof(lexer)) {
            int32_t character = lexer->lookahead;
            lexer->advance(lexer, false);
            if (character == '\\' && (quotes == 1 || python) && !verbatim) {
              if (!lexer->eof(lexer)) lexer->advance(lexer, false);
              continue;
            }
            if (character == c) {
              if (verbatim && lexer->lookahead == c) {
                lexer->advance(lexer, false);
                continue;
              }
              if (++closing == quotes) break;
            } else {
              closing = 0;
            }
          }
        }
      }
    }
    lexer->mark_end(lexer);
  }
  return any;
}

// Mirror zolo-lexer's Java Unicode translation while preserving authored bytes.
// mark_end is only advanced after a unit belongs to the body, so a raw outer
// closing brace remains the grammar's delimiter even after lookahead consumes it.
static int java_hex(int32_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int32_t java_unit(TSLexer *lexer, unsigned *slashes, bool *escaped) {
  int32_t c = lexer->lookahead;
  lexer->advance(lexer, false);
  *escaped = false;
  if (c == '\\' && *slashes % 2 == 0 && lexer->lookahead == 'u') {
    while (lexer->lookahead == 'u') lexer->advance(lexer, false);
    int32_t value = 0;
    for (unsigned i = 0; i < 4; i++) {
      int digit = java_hex(lexer->lookahead);
      if (digit < 0) { *slashes = 0; return '\\'; }
      value = value * 16 + digit;
      lexer->advance(lexer, false);
    }
    *escaped = true;
    *slashes = 0;
    return value;
  }
  *slashes = c == '\\' ? *slashes + 1 : 0;
  return c;
}

static bool scan_java_body(TSLexer *lexer) {
  enum { CODE, SLASH, LINE, BLOCK, OPEN_STRING, STRING, CHARACTER, TEXT } state = CODE;
  unsigned depth = 0, slashes = 0, quotes = 0;
  bool any = false, escape = false, star = false;
  lexer->mark_end(lexer);
  while (!lexer->eof(lexer)) {
    bool translated;
    int32_t c = java_unit(lexer, &slashes, &translated);
    bool again = true;
    while (again) {
      again = false;
      switch (state) {
      case CODE:
        if (c == '}' && depth == 0 && !translated) return any;
        if (c == '{') depth++;
        else if (c == '}' && depth) depth--;
        else if (c == '/') state = SLASH;
        else if (c == '"') { state = OPEN_STRING; quotes = 1; }
        else if (c == '\'') { state = CHARACTER; escape = false; }
        break;
      case SLASH:
        if (c == '/') state = LINE;
        else if (c == '*') { state = BLOCK; star = false; }
        else { state = CODE; again = true; }
        break;
      case LINE:
        if (c == '\r' || c == '\n') state = CODE;
        break;
      case BLOCK:
        if (star && c == '/') state = CODE;
        star = c == '*';
        break;
      case OPEN_STRING:
        if (c == '"') {
          if (++quotes == 3) { state = TEXT; quotes = 0; escape = false; }
        } else { state = quotes == 2 ? CODE : STRING; escape = false; again = true; }
        break;
      case STRING: case CHARACTER:
        if (escape) escape = false;
        else if (c == '\\') escape = true;
        else if (c == (state == STRING ? '"' : '\'')) state = CODE;
        break;
      case TEXT:
        if (escape) { escape = false; quotes = 0; }
        else if (c == '\\') { escape = true; quotes = 0; }
        else if (c == '"') { if (++quotes == 3) state = CODE; }
        else quotes = 0;
        break;
      }
    }
    any = true;
    lexer->mark_end(lexer);
  }
  return any;
}

// Matches scan_native/native_literal_end in the compiler lexer. Prefixes and
// comments are language-specific; an imported alias retains the same dialect.
static bool scan_native_code(TSLexer *, uint8_t, bool, unsigned);
static bool native_name_start(int32_t c) {return is_label_start(c) || c >= 0x80;}
static bool native_name_continue(int32_t c) {return native_name_start(c) || (c >= '0' && c <= '9');}
static void scan_native_string(TSLexer *lexer, uint8_t dialect,
                               int32_t quote, unsigned dollars, unsigned nesting) {
  lexer->advance(lexer, false); // opening quote
  unsigned width = 1;
  if (dialect == FOREIGN_KOTLIN && quote == '"' && lexer->lookahead == '"') {
    lexer->advance(lexer, false);
    if (lexer->lookahead != '"') return; // empty string
    lexer->advance(lexer, false);
    width = 3;
  }
  unsigned quotes = 0;
  while (!lexer->eof(lexer)) {
    int32_t c = lexer->lookahead;
    if (c == quote) {
      lexer->advance(lexer, false);
      if (++quotes == width) return;
      continue;
    }
    quotes = 0;
    if (c == '\\' && width == 1 && quote != '`') {
      lexer->advance(lexer, false);
      if (!lexer->eof(lexer)) lexer->advance(lexer, false);
    } else if (c == '$' && dialect == FOREIGN_KOTLIN && quote == '"') {
      unsigned count = 0;
      do { lexer->advance(lexer, false); count++; } while (lexer->lookahead == '$');
      if (count >= (dollars ? dollars : 1) && lexer->lookahead == '{' && nesting < 64) {
        lexer->advance(lexer, false);
        scan_native_code(lexer, dialect, true, nesting + 1);
      }
    } else lexer->advance(lexer, false);
  }
}
static void scan_rust_raw(TSLexer *lexer, unsigned hashes) {
  lexer->advance(lexer, false); // quote after r###
  while (!lexer->eof(lexer)) {
    if (lexer->lookahead == '"') {
      lexer->advance(lexer, false);
      unsigned count = 0;
      while (count < hashes && lexer->lookahead == '#') {lexer->advance(lexer, false);count++;}
      if (count == hashes) return;
    } else lexer->advance(lexer, false);
  }
}
static void scan_cpp_raw(TSLexer *lexer) {
  char delimiter[17];unsigned size = 0;
  lexer->advance(lexer, false); // quote after R
  while (!lexer->eof(lexer) && lexer->lookahead != '(') {
    if (size == 16 || lexer->lookahead == ')' || lexer->lookahead == '\\' || is_ascii_ws(lexer->lookahead)) return;
    delimiter[size++] = (char)lexer->lookahead;
    lexer->advance(lexer, false);
  }
  if (lexer->lookahead != '(') return;
  lexer->advance(lexer, false);
  while (!lexer->eof(lexer)) {
    if (lexer->lookahead == ')') {
      lexer->advance(lexer, false);unsigned i = 0;
      while (i < size && lexer->lookahead == delimiter[i]) {lexer->advance(lexer, false);i++;}
      if (i == size && lexer->lookahead == '"') {lexer->advance(lexer, false);return;}
    } else lexer->advance(lexer, false);
  }
}
static bool scan_native_code(TSLexer *lexer, uint8_t dialect,
                             bool consume_close, unsigned nesting) {
  unsigned depth = 0;bool any = false;bool line_start = true;
  const bool cpp = dialect == FOREIGN_C || dialect == FOREIGN_CPP;
  lexer->mark_end(lexer);
  while (!lexer->eof(lexer)) {
    int32_t c = lexer->lookahead;
    if (c == '}' && depth == 0) {
      if (consume_close) lexer->advance(lexer, false);
      return any;
    }
    if (c == '\n' || c == '\r') {line_start = true;lexer->advance(lexer, false);}
    else if (line_start && (c == ' ' || c == '\t')) lexer->advance(lexer, false);
    else if (cpp && line_start && c == '#') {
      // Macro braces belong to the directive, including continued lines.
      while (!lexer->eof(lexer)) {
        c = lexer->lookahead;lexer->advance(lexer, false);
        if (c == '\\' && (lexer->lookahead == '\r' || lexer->lookahead == '\n')) {
          if (lexer->lookahead == '\r') lexer->advance(lexer, false);
          if (lexer->lookahead == '\n') lexer->advance(lexer, false);
        } else if (c == '\n' || c == '\r') break;
      }
      line_start = true;
    } else if (c == '/') {
      lexer->advance(lexer, false);
      if (lexer->lookahead == '/') {
        while (!lexer->eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
          c = lexer->lookahead;lexer->advance(lexer, false);
          if (cpp && c == '\\' && (lexer->lookahead == '\r' || lexer->lookahead == '\n')) {
            if (lexer->lookahead == '\r') lexer->advance(lexer, false);
            if (lexer->lookahead == '\n') lexer->advance(lexer, false);
          }
        }
      } else if (lexer->lookahead == '*') {
        lexer->advance(lexer, false);unsigned comments = 1;
        while (comments && !lexer->eof(lexer)) {
          c = lexer->lookahead;lexer->advance(lexer, false);
          if (c == '*' && lexer->lookahead == '/') {lexer->advance(lexer, false);comments--;}
          else if ((dialect == FOREIGN_RUST || dialect == FOREIGN_KOTLIN) && c == '/' && lexer->lookahead == '*') {lexer->advance(lexer, false);comments++;}
        }
      }
    } else if (dialect == FOREIGN_RUST && c == '\'') {
      lexer->advance(lexer, false);
      if (native_name_start(lexer->lookahead)) {
        // A lifetime or label ends at the identifier, a character at its quote.
        while (native_name_continue(lexer->lookahead)) lexer->advance(lexer, false);
        if (lexer->lookahead == '\'') lexer->advance(lexer, false);
      } else {
        while (!lexer->eof(lexer)) {c=lexer->lookahead;lexer->advance(lexer,false);if(c=='\\'&&!lexer->eof(lexer))lexer->advance(lexer,false);else if(c=='\'')break;}
      }
      line_start = false;
    } else if (native_name_start(c)) {
      char prefix[8];unsigned length = 0;
      while (native_name_continue(lexer->lookahead)) {if(length<sizeof(prefix)-1)prefix[length]=(char)lexer->lookahead;length++;lexer->advance(lexer,false);}
      prefix[length<sizeof(prefix)-1?length:sizeof(prefix)-1] = 0;
      if (dialect == FOREIGN_RUST && (!strcmp(prefix,"r") || !strcmp(prefix,"br") || !strcmp(prefix,"cr"))) {
        unsigned hashes = 0;while(lexer->lookahead=='#'){hashes++;lexer->advance(lexer,false);}
        if(lexer->lookahead=='"')scan_rust_raw(lexer,hashes);
      } else if (dialect == FOREIGN_CPP && (!strcmp(prefix,"R") || !strcmp(prefix,"u8R") || !strcmp(prefix,"uR") || !strcmp(prefix,"UR") || !strcmp(prefix,"LR")) && lexer->lookahead == '"') scan_cpp_raw(lexer);
      line_start = false;
    } else if (dialect == FOREIGN_KOTLIN && c == '$') {
      unsigned dollars = 0;while(lexer->lookahead=='$'){dollars++;lexer->advance(lexer,false);}
      if(lexer->lookahead=='"')scan_native_string(lexer,dialect,'"',dollars,nesting);
      line_start = false;
    } else if (c == '"' || c == '\'' || (c == '`' && (dialect == FOREIGN_GO || dialect == FOREIGN_KOTLIN))) {
      scan_native_string(lexer,dialect,c,1,nesting);line_start=false;
    } else {
      lexer->advance(lexer,false);if(c=='{')depth++;else if(c=='}')depth--;line_start=false;
    }
    any = true;lexer->mark_end(lexer);
  }
  return any;
}

// Group headers are Zolo declarations, so comments before the first `fn`
// must not make the group look like one opaque foreign expression.
static bool skip_foreign_group_trivia(TSLexer *lexer) {
  for (;;) {
    while (is_ascii_ws(lexer->lookahead)) lexer->advance(lexer, false);
    if (lexer->lookahead != '/') return true;
    lexer->advance(lexer, false);
    if (lexer->lookahead == '/') {
      while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
        lexer->advance(lexer, false);
      }
    } else if (lexer->lookahead == '*') {
      lexer->advance(lexer, false);
      unsigned depth = 1;
      while (depth && !lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;
        lexer->advance(lexer, false);
        if (c == '/' && lexer->lookahead == '*') {
          lexer->advance(lexer, false);
          depth++;
        } else if (c == '*' && lexer->lookahead == '/') {
          lexer->advance(lexer, false);
          depth--;
        }
      }
      if (depth) return false;
    } else {
      return false;
    }
  }
}

// The normal Rust lexer treats a regular block comment reaching EOF as trivia.
// Keep that yield-only decision separate from provider peeks requiring closure.
static bool skip_yield_trivia(TSLexer *lexer) {
  for (;;) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
           lexer->lookahead == '\r' || lexer->lookahead == '\n' ||
           lexer->lookahead == 0x00a0) lexer->advance(lexer, false);
    if (lexer->lookahead != '/') return true;
    lexer->advance(lexer, false);
    if (lexer->lookahead == '/') {
      lexer->advance(lexer, false);
      if (lexer->lookahead == '/' || lexer->lookahead == '!') return false; // doc token
      while (!lexer->eof(lexer) && lexer->lookahead != '\n') lexer->advance(lexer, false);
    } else if (lexer->lookahead == '*') {
      lexer->advance(lexer, false);
      if (lexer->lookahead == '*' || lexer->lookahead == '!') return false; // doc token
      unsigned depth = 1;
      while (depth && !lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;
        lexer->advance(lexer, false);
        if (c == '/' && lexer->lookahead == '*') {
          lexer->advance(lexer, false); depth++;
        } else if (c == '*' && lexer->lookahead == '/') {
          lexer->advance(lexer, false); depth--;
        }
      }
      if (lexer->eof(lexer)) return true;
    } else {
      return false; // a real slash is an operand attempt, even before EOF
    }
  }
}

// A required positive marker may fail so normal extras consume comments/FF/VT
// and the parser reconsults it. Match G's exact immediate whitespace window.
// mark_end stays at the original position; the expression lexes all bytes.
static bool scan_non_dot_expression_start(TSLexer *lexer, bool prefix_newline) {
  lexer->mark_end(lexer);
  bool saw_newline = prefix_newline;
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
         lexer->lookahead == '\r' || lexer->lookahead == '\n') {
    if (lexer->lookahead == '\n') saw_newline = true;
    lexer->advance(lexer, false);
  }
  if (lexer->eof(lexer)) return false;
  int32_t c = lexer->lookahead;
  if (is_label_start(c) || (c >= '0' && c <= '9')) return true;
  switch (c) {
  case '"': case '\'': case '$': case '(': case '[': case '{':
    return true;
  case '.':
    lexer->advance(lexer, false);
    if (lexer->lookahead != '.') return false;
    lexer->advance(lexer, false);
    return lexer->lookahead != '.'; // .. and ..=; not ., ... or .*
  case '!':
    lexer->advance(lexer, false);
    if (lexer->lookahead == '=') return false;
    if (lexer->lookahead == '~') {
      lexer->advance(lexer, false);
      if (lexer->lookahead == '=') return false; // !~= infix, but !~x two prefixes
    }
    return true;
  case '-':
    lexer->advance(lexer, false);
    return lexer->lookahead != '=' && lexer->lookahead != '>';
  case '~':
    lexer->advance(lexer, false);
    return lexer->lookahead != '=' && lexer->lookahead != '/';
  case '^':
    lexer->advance(lexer, false);
    return lexer->lookahead != '=';
  case '|':
    lexer->advance(lexer, false);
    return lexer->lookahead != '=' && lexer->lookahead != '>'; // | and || lambda
  case '#':
    lexer->advance(lexer, false);
    return lexer->lookahead == '{';
  case 96:
    lexer->advance(lexer, false);
    if (lexer->lookahead != 96) return false;
    lexer->advance(lexer, false);
    return lexer->lookahead == 96;
  case ':':
    lexer->advance(lexer, false);
    if (!is_label_start(lexer->lookahead)) return false;
    do { lexer->advance(lexer, false); } while (is_label_continue(lexer->lookahead));
    while (is_ascii_ws(lexer->lookahead)) lexer->advance(lexer, false);
    if (lexer->lookahead == 'l') return scan_word(lexer, "loop");
    if (lexer->lookahead == 'f') return scan_word(lexer, "for");
    if (lexer->lookahead == 'w') return scan_word(lexer, "while");
    return false;
  case '<':
    if (!saw_newline) return false;
    lexer->advance(lexer, false);
    return opens_a_tag(lexer->lookahead);
  default:
    return false;
  }
}

// A file-scoped dependency declaration starts only when the first entry is
// provider-qualified or opens a provider group. Mark only `deps`: braces,
// comments, providers and packages remain ordinary grammar nodes.
// `deps()`, `deps {}`, and multiline constructors with unqualified fields.
static bool scan_dependencies_keyword(TSLexer *lexer) {
  if (!scan_word(lexer, "deps")) return false;
  lexer->mark_end(lexer);
  if (!skip_foreign_group_trivia(lexer) || lexer->lookahead != '{') return false;
  lexer->advance(lexer, false);
  if (!skip_foreign_group_trivia(lexer)) return false;
  while (lexer->lookahead == ',' || lexer->lookahead == ';') {
    lexer->advance(lexer, false);
    if (!skip_foreign_group_trivia(lexer)) return false;
  }
  if (!is_label_start(lexer->lookahead)) return false;
  do {
    lexer->advance(lexer, false);
  } while (is_label_continue(lexer->lookahead));
  if (!skip_foreign_group_trivia(lexer)) return false;
  if (lexer->lookahead == '{') return true;
  if (lexer->lookahead != ':') return false;
  lexer->advance(lexer, false);
  return lexer->lookahead == ':';
}

// Declaration keywords still have keyword TokenKinds, so the compiler's
// named callback lookahead accepts only actual identifiers. Keep this list
// aligned with zolo-lexer/src/keywords.rs, not contextual member-name rules.
static bool callback_keyword(const char *word) {
  static const char *const keywords[] = {"let", "mut", "var", "const", "const_assert", "override", "enable", "requires", "fn", "return", "if", "else", "for", "while", "loop", "break", "continue", "match", "enum", "struct", "impl", "trait", "mod", "use", "pub", "in", "as", "is", "where", "nil", "true", "false", "self", "type", "newtype", "comptime", "async", "await", "yield", "spawn", "scope", "select", "every", "after", "timeout", "sleep", "try", "catch", "finally", "defer", "defer_ok", "defer_err", "guard", "macro", "on", "schema", "machine", "effect", "handle", "perform", "with", "using"};
  for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
    if (!strcmp(word, keywords[i])) return true;
  }
  return false;
}

// Leading comments must stay on the closer's line. This is peek-only: the
// zero-width token leaves comments to ordinary extras and their own nodes.
static bool skip_callback_trivia(TSLexer *lexer) {
  for (;;) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r' || lexer->lookahead == '\f') lexer->advance(lexer, false);
    if (lexer->lookahead != '/') return lexer->lookahead != '\n';
    lexer->advance(lexer, false);
    if (lexer->lookahead != '*') return false;
    lexer->advance(lexer, false);
    unsigned depth = 1;
    while (depth && !lexer->eof(lexer)) {
      int32_t c = lexer->lookahead;
      if (c == '\n') return false;
      lexer->advance(lexer, false);
      if (c == '/' && lexer->lookahead == '*') { lexer->advance(lexer, false); depth++; }
      else if (c == '*' && lexer->lookahead == '/') { lexer->advance(lexer, false); depth--; }
    }
    if (depth) return false;
  }
}

// Called before any external whitespace scan. The accepted marker consumes
// zero bytes at the authored closer: names, comments, braces and whitespace
// remain normal syntax. A negative marker commits the call's end, so extras
// cannot discard a newline and then retry attaching a next-line constructor.
static bool scan_callback_boundary(TSLexer *lexer, bool named, bool pipe, bool await_named, bool await_end, bool operand_a, bool prefix_newline) {
  lexer->mark_end(lexer);
  if (prefix_newline || !skip_callback_trivia(lexer)) return false;
  if (pipe && lexer->lookahead == '{') {
    lexer->advance(lexer, false);
    if (!skip_foreign_group_trivia(lexer) || lexer->lookahead != '|') return false;
    lexer->result_symbol = TRAILING_CALLBACK_PIPE;
    return true;
  }
  if ((!named && !await_named && !await_end) || !is_label_start(lexer->lookahead)) return false;
  char word[80];
  unsigned length = 0;
  do {
    if (length < sizeof(word) - 1) word[length] = (char)lexer->lookahead;
    length++;
    lexer->advance(lexer, false);
  } while (is_label_continue(lexer->lookahead));
  word[length < sizeof(word) - 1 ? length : sizeof(word) - 1] = 0;
  if (length < sizeof(word) && (!strcmp(word, "recover") || callback_keyword(word))) return false;
  if (!skip_foreign_group_trivia(lexer) || lexer->lookahead != '{') return false;
  if ((await_named || await_end) && length == 6 && !strcmp(word, "within")) {
    if (!await_end) return false;
    lexer->result_symbol = AWAIT_WITHIN_CALL_END;
    return true;
  }
  if (operand_a && length == 6 && !strcmp(word,"within")) return false;
  if (await_named) lexer->result_symbol = AWAIT_NAMED_CALLBACK_START;
  else if (named) lexer->result_symbol = NAMED_CALLBACK_START;
  else return false;
  return true;
}

static uint64_t foreign_name_hash(const char *name) {
  uint64_t hash = UINT64_C(14695981039346656037);
  while (*name) hash = (hash ^ (unsigned char)*name++) * UINT64_C(1099511628211);
  return hash;
}

static bool read_foreign_name(TSLexer *lexer, uint64_t *name) {
  uint64_t hash = UINT64_C(14695981039346656037);
  if (!(lexer->lookahead == '_' ||
        (lexer->lookahead >= 'a' && lexer->lookahead <= 'z') ||
        (lexer->lookahead >= 'A' && lexer->lookahead <= 'Z'))) return false;
  do {
    hash = (hash ^ (unsigned char)lexer->lookahead) * UINT64_C(1099511628211);
    lexer->advance(lexer, false);
  } while (lexer->lookahead == '_' ||
           (lexer->lookahead >= 'a' && lexer->lookahead <= 'z') ||
           (lexer->lookahead >= 'A' && lexer->lookahead <= 'Z') ||
           (lexer->lookahead >= '0' && lexer->lookahead <= '9'));
  *name = hash;
  return true;
}

static void register_foreign_binding(Scanner *scanner, uint64_t plugin,
                                    uint64_t export, uint64_t name) {
  // Same built-in lexical contract as foreign_provider_dialect(plugin, export)
  // in zolo-lexer. No guessed aliases and no universal Python comment rule.
  uint8_t dialect = FOREIGN_BRACED;
  if (plugin == foreign_name_hash("python") && export == foreign_name_hash("python")) dialect = FOREIGN_PYTHON;
  if (plugin == foreign_name_hash("node") && export == foreign_name_hash("javascript")) dialect = FOREIGN_JAVASCRIPT;
  if (plugin == foreign_name_hash("node") && export == foreign_name_hash("typescript")) dialect = FOREIGN_TYPESCRIPT;
  if (plugin == foreign_name_hash("jvm") && export == foreign_name_hash("java")) dialect = FOREIGN_JAVA;
  if (plugin == foreign_name_hash("jvm") && export == foreign_name_hash("kotlin")) dialect = FOREIGN_KOTLIN;
  if (plugin == foreign_name_hash("native") && export == foreign_name_hash("c")) dialect = FOREIGN_C;
  if (plugin == foreign_name_hash("native") && export == foreign_name_hash("cpp")) dialect = FOREIGN_CPP;
  if (plugin == foreign_name_hash("rust") && export == foreign_name_hash("rust")) dialect = FOREIGN_RUST;
  if (plugin == foreign_name_hash("go") && export == foreign_name_hash("go")) dialect = FOREIGN_GO;
  bool shadows_provider = false;
  for (unsigned i = scanner->count; i > 0; i--) {
    ForeignBinding *binding = &scanner->bindings[i - 1];
    if (binding->name == name) {
      shadows_provider = true;
      if (binding->scope == scanner->scope) {
        binding->dialect = dialect;
        return;
      }
    }
  }
  // Other plugin exports need no entry unless they shadow a known binding.
  if ((!dialect && !shadows_provider) || scanner->count == MAX_FOREIGN_BINDINGS) return;
  ForeignBinding *binding = &scanner->bindings[scanner->count++];
  binding->name = name;
  binding->scope = scanner->scope;
  binding->dialect = dialect;
}

// This hidden token consumes no authored bytes. Its grammar position is before
// a `use` declaration; lookahead records provider imports while the ordinary
// grammar still parses every import/path/list node with its original spans.
static bool scan_foreign_import_after_use(Scanner *scanner, TSLexer *lexer) {
  // Shared dispatcher already peeked use and marked its zero-width start.
  if (!skip_foreign_group_trivia(lexer) ||
      !scan_word(lexer, "plugin") || !skip_foreign_group_trivia(lexer)) return false;
  uint64_t plugin;
  if (!read_foreign_name(lexer, &plugin)) return false;
  Scanner next = *scanner;
  uint64_t export = plugin;
  if (!skip_foreign_group_trivia(lexer)) return false;
  while (lexer->lookahead == ':') {
    lexer->advance(lexer, false);
    if (lexer->lookahead != ':') return false;
    lexer->advance(lexer, false);
    if (!skip_foreign_group_trivia(lexer)) return false;
    if (lexer->lookahead == '*') return false; // explicit binding required
    if (lexer->lookahead == '{') {
      lexer->advance(lexer, false);
      for (;;) {
        if (!skip_foreign_group_trivia(lexer)) return false;
        if (lexer->lookahead == '}') break;
        uint64_t name;
        if (!read_foreign_name(lexer, &export)) return false;
        name = export;
        if (!skip_foreign_group_trivia(lexer)) return false;
        if (lexer->lookahead == 'a') {
          if (!scan_word(lexer, "as") || !skip_foreign_group_trivia(lexer) ||
              !read_foreign_name(lexer, &name)) return false;
        }
        register_foreign_binding(&next, plugin, export, name);
        if (!skip_foreign_group_trivia(lexer)) return false;
        if (lexer->lookahead == '}') break;
        if (lexer->lookahead != ',') return false;
        lexer->advance(lexer, false);
      }
      *scanner = next;
      return true;
    }
    if (!read_foreign_name(lexer, &export) || !skip_foreign_group_trivia(lexer)) return false;
  }
  uint64_t name = export;
  if (lexer->lookahead == 'a') {
    // A following `async` declaration is not an alias clause. The lookahead
    // marker still leaves that authored text to the ordinary grammar.
    if (scan_word(lexer, "as") &&
        (!skip_foreign_group_trivia(lexer) || !read_foreign_name(lexer, &name))) return false;
  }
  register_foreign_binding(&next, plugin, export, name);
  *scanner = next;
  return true;
}

// Whole raw test-title tokens: bounded matching hashes without 256 lexer branches.
// The title context alone enables these symbols. No scanner state is mutated.
static bool scan_titled_raw_title(TSLexer *lexer, bool regular_valid, bool triple_valid) {
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
         lexer->lookahead == '\r' || lexer->lookahead == '\n') {
    lexer->advance(lexer, true);
  }
  if (lexer->lookahead != 'r') return false;
  lexer->advance(lexer, false);
  unsigned hashes = 0;
  while (lexer->lookahead == '#') {
    if (hashes == 255) return false;
    hashes++;
    lexer->advance(lexer, false);
  }
  if (lexer->lookahead != '"') return false;
  lexer->advance(lexer, false);

  // Three opening quotes are a block only when immediately followed by LF/CRLF.
  // Otherwise the extra quotes remain ordinary raw content (or the first closer).
  unsigned extra_quotes = 0;
  while (extra_quotes < 2 && lexer->lookahead == '"') {
    lexer->advance(lexer, false);
    extra_quotes++;
    if (hashes == 0 && extra_quotes == 1) lexer->mark_end(lexer);
  }
  if (extra_quotes == 2 && (lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
    if (!triple_valid) return false;
    if (lexer->lookahead == '\r') {
      lexer->advance(lexer, false);
      if (lexer->lookahead != '\n') {
        if (hashes == 0 && regular_valid) {
          lexer->result_symbol = TITLED_RAW_TITLE;
          return true; // Preserve the marked ordinary r"" closer at byte 3.
        }
        return false;
      }
    }
    lexer->advance(lexer, false);
    while (!lexer->eof(lexer) && lexer->lookahead != '\r' && lexer->lookahead != '\n')
      lexer->advance(lexer, false);
    if (lexer->lookahead == '\r') {
      lexer->advance(lexer, false);
      if (lexer->lookahead != '\n') return false;
    } else if (lexer->lookahead != '\n') return false;
    lexer->advance(lexer, false);
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, false);
    for (unsigned i = 0; i < 3; i++) {
      if (lexer->lookahead != '"') return false;
      lexer->advance(lexer, false);
    }
    for (unsigned i = 0; i < hashes; i++) {
      if (lexer->lookahead != '#') return false;
      lexer->advance(lexer, false);
    }
    lexer->mark_end(lexer);
    lexer->result_symbol = TITLED_RAW_TRIPLE_TITLE;
    return true;
  }
  if (!regular_valid) return false;
  if (extra_quotes > 0) {
    if (hashes == 0) {
      lexer->result_symbol = TITLED_RAW_TITLE;
      return true; // mark_end is still after the first closing quote.
    }
    unsigned found = 0;
    while (found < hashes && lexer->lookahead == '#') {
      found++;
      lexer->advance(lexer, false);
    }
    if (found == hashes) {
      lexer->mark_end(lexer);
      lexer->result_symbol = TITLED_RAW_TITLE;
      return true;
    }
  }
  while (!lexer->eof(lexer) && lexer->lookahead != '\r' && lexer->lookahead != '\n') {
    if (lexer->lookahead != '"') {
      lexer->advance(lexer, false);
      continue;
    }
    lexer->advance(lexer, false);
    unsigned found = 0;
    while (found < hashes && lexer->lookahead == '#') {
      found++;
      lexer->advance(lexer, false);
    }
    if (found == hashes) {
      lexer->mark_end(lexer);
      lexer->result_symbol = TITLED_RAW_TITLE;
      return true;
    }
  }
  return false;
}

static bool scan_restricted_nominal_start(TSLexer *lexer, bool resource_header) {
  lexer->mark_end(lexer);
  if(!skip_yield_trivia(lexer)||lexer->lookahead!='{')return false;
  lexer->advance(lexer,false);
  if(!skip_yield_trivia(lexer))return false;
  bool single=false;
  if(is_label_start(lexer->lookahead)) {
    char name[9]={0};unsigned length=0;bool long_name=false;
    do {if(length<8)name[length++]=(char)lexer->lookahead;else long_name=true;lexer->advance(lexer,false);}while(is_label_continue(lexer->lookahead));
    if(!long_name&&(!strcmp(name,"break")||!strcmp(name,"continue")))return false;
    // A string prefix adjacent to its delimiter is one lexer token, not a field name.
    if(lexer->lookahead=='"'||lexer->lookahead=='#')return false;
    if(!skip_yield_trivia(lexer))return false;
    if(lexer->lookahead==',')return true;
    if(lexer->lookahead==':'){lexer->advance(lexer,false);return lexer->lookahead!=':';}
    single=true;
  } else if(lexer->lookahead=='.') {
    lexer->advance(lexer,false);if(lexer->lookahead!='.')return false;lexer->advance(lexer,false);return lexer->lookahead!='=';
  }
  if(lexer->lookahead!='}')return false;
  lexer->advance(lexer,false);
  if(!skip_yield_trivia(lexer))return false;
  if(single&&lexer->lookahead=='=') {lexer->advance(lexer,false);return lexer->lookahead!='='&&lexer->lookahead!='>';}
  if(!resource_header)return false;
  while(lexer->lookahead=='?') {lexer->advance(lexer,false);if(lexer->lookahead=='?'||lexer->lookahead=='.'||lexer->lookahead=='>')return false;if(!skip_yield_trivia(lexer))return false;}
  return lexer->lookahead=='{';
}


static bool scan_every_interval_start(TSLexer *lexer) {
  // Rust Every dispatches a leading LBrace directly to its body.
  // Peek normal trivia without changing the zero-width token span.
  lexer->mark_end(lexer);
  if (!skip_yield_trivia(lexer)) return true;
  return lexer->lookahead != '{';
}

static bool scan_is_type_start(TSLexer *lexer) {
 lexer->mark_end(lexer);
 if(!skip_yield_trivia(lexer)) return false;
 bool allow_not=true;
 next_type: ;
 if(!is_label_start(lexer->lookahead)) {
  if(lexer->lookahead=='('||lexer->lookahead=='['||lexer->lookahead=='#') return true;
  if(lexer->lookahead!='{') return false;
  lexer->advance(lexer,false);if(!skip_yield_trivia(lexer))return false;
  if(!is_label_start(lexer->lookahead))return true;
  do { lexer->advance(lexer,false); } while(is_label_continue(lexer->lookahead));
  if(!skip_yield_trivia(lexer))return false;
  return lexer->lookahead!=','&&lexer->lookahead!='}'&&lexer->lookahead!='.';
 }
 unsigned length=0;char word[6]={0};bool long_word=false;
 do { if(length<5)word[length++]=(char)lexer->lookahead;else long_word=true;lexer->advance(lexer,false); } while(is_label_continue(lexer->lookahead));
 if(lexer->lookahead=='"')return false; // Any adjacent quote is a lexer string/pattern/tag token, never a type-name token.
 if(!long_word&&length==1&&word[0]=='r'&&lexer->lookahead=='#')return false;
 if(lexer->lookahead=='#'){do{lexer->advance(lexer,false);}while(lexer->lookahead=='#');return lexer->lookahead!='"';}
 if(allow_not&&!long_word&&length==3&&word[0]=='n'&&word[1]=='o'&&word[2]=='t') { allow_not=false;if(!skip_yield_trivia(lexer))return false;goto next_type; }
 if(!long_word&&((length==4&&word[0]=='t'&&word[1]=='r'&&word[2]=='u'&&word[3]=='e')||(length==5&&word[0]=='f'&&word[1]=='a'&&word[2]=='l'&&word[3]=='s'&&word[4]=='e')))return false;
 if(!skip_yield_trivia(lexer)) return false;
 if(lexer->lookahead=='@')return false;
 bool qualified=false;
 while(lexer->lookahead=='.'||lexer->lookahead==':') {
  const int32_t separator=lexer->lookahead;lexer->advance(lexer,false);
  if(separator==':') { if(lexer->lookahead!=':') return true;lexer->advance(lexer,false); }
  if(!skip_yield_trivia(lexer)) return false;
  if(!is_label_start(lexer->lookahead)) return true;
  qualified=true;
  do { lexer->advance(lexer,false); } while(is_label_continue(lexer->lookahead));
  if(!skip_yield_trivia(lexer)) return false;
 }
 return !qualified || lexer->lookahead!='(';
}

typedef struct { unsigned bp; enum TokenType symbol; } StaticPrattEnd;
static const StaticPrattEnd static_pratt_ends[] = {
  {0, PRATT_END0},
  {1, PRATT_END1},
  {2, PRATT_END2},
  {10, PRATT_END10},
  {20, PRATT_END20},
  {27, PRATT_END27},
};
#define STATIC_PRATT_END_COUNT 6

// Stateless BP barriers; the 16 grammar contexts remain in the LR grammar.
// Potential postfixes conservatively preserve the previous G policy. This
// scanner does not claim to implement Rust's missing head-column/lhs guards.
static bool skip_pratt_end_trivia(TSLexer *lexer, bool *newline, bool *markup_newline) {
  bool markup_whitespace = true;
  for (;;) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
           lexer->lookahead == '\r' || lexer->lookahead == '\n' ||
           lexer->lookahead == '\f' || lexer->lookahead == '\v' ||
           lexer->lookahead == 0xa0) {
      if (lexer->lookahead == '\f' || lexer->lookahead == '\v' || lexer->lookahead == 0xa0) {
        markup_whitespace = false; *markup_newline = false;
      }
      if (lexer->lookahead == '\n') { *newline = true; if (markup_whitespace) *markup_newline = true; }
      lexer->advance(lexer, false);
    }
    if (lexer->lookahead != '/') return true;
    lexer->advance(lexer, false);
    if (lexer->lookahead == '/') {
      while (!lexer->eof(lexer) && lexer->lookahead != '\n') lexer->advance(lexer, false);
      *markup_newline = false; markup_whitespace = true;
      continue;
    }
    if (lexer->lookahead != '*') return false;
    lexer->advance(lexer, false);
    unsigned depth = 1;
    while (depth && !lexer->eof(lexer)) {
      const int32_t c = lexer->lookahead;
      if (c == '\n') *newline = true;
      lexer->advance(lexer, false);
      if (c == '/' && lexer->lookahead == '*') { lexer->advance(lexer, false); depth++; }
      else if (c == '*' && lexer->lookahead == '/') { lexer->advance(lexer, false); depth--; }
    }
    *markup_newline = false; markup_whitespace = true;
  }
}

// The caller retains mark_end at the authored boundary. This is literally
// the existing restricted nominal predicate after its initial trivia lookup.
static bool pratt_nominal_body(TSLexer *lexer, bool resource_header, bool restricted) {
  if (lexer->lookahead != '{') return false;
  lexer->advance(lexer, false);
  if (!skip_yield_trivia(lexer)) return false;
  bool single = false;
  if (is_label_start(lexer->lookahead)) {
    char name[9] = {0}; unsigned length = 0; bool long_name = false;
    do { if (length < 8) name[length++] = (char)lexer->lookahead; else long_name = true;
      lexer->advance(lexer, false); } while (is_label_continue(lexer->lookahead));
    if (restricted && !long_name && (!strcmp(name, "break") || !strcmp(name, "continue"))) return false;
    if (lexer->lookahead == '"' || lexer->lookahead == '#') return false;
    if (!skip_yield_trivia(lexer)) return false;
    if (lexer->lookahead == ',') return true;
    if (lexer->lookahead == ':') { lexer->advance(lexer, false); return lexer->lookahead != ':'; }
    single = true;
  } else if (lexer->lookahead == '.') {
    lexer->advance(lexer, false); if (lexer->lookahead != '.') return false;
    lexer->advance(lexer, false); return lexer->lookahead != '=';
  }
  if (lexer->lookahead != '}') return false;
  if (!restricted) return true;
  lexer->advance(lexer, false);
  if (!skip_yield_trivia(lexer)) return false;
  if (single && lexer->lookahead == '=') { lexer->advance(lexer, false); return lexer->lookahead != '=' && lexer->lookahead != '>'; }
  if (!resource_header) return false;
  while (lexer->lookahead == '?') {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '?' || lexer->lookahead == '.' || lexer->lookahead == '>') return false;
    if (!skip_yield_trivia(lexer)) return false;
  }
  return lexer->lookahead == '{';
}


// N/H/A/R scopes use entry and exact-prior-bit restoration tokens.
// Heartbeats distinguish zero-byte no-op scopes.
typedef struct { enum TokenType token; uint8_t mask,value,old; } ExprScopeToken;
static const ExprScopeToken expr_scope_enters[] = {{SCOPE_CLEAR_NHA_ENTER0,7,0,0},{SCOPE_CLEAR_NHA_ENTER1,7,0,1},{SCOPE_CLEAR_NHA_ENTER2,7,0,2},{SCOPE_CLEAR_NHA_ENTER3,7,0,3},{SCOPE_CLEAR_NHA_ENTER4,7,0,4},{SCOPE_CLEAR_NHA_ENTER5,7,0,5},{SCOPE_CLEAR_NHA_ENTER6,7,0,6},{SCOPE_CLEAR_NHA_ENTER7,7,0,7},{SCOPE_SET_N_ENTER0,1,1,0},{SCOPE_SET_N_ENTER1,1,1,1},{SCOPE_PAREN_ENTER0,7,0,0},{SCOPE_PAREN_ENTER1,7,1,1},{SCOPE_PAREN_ENTER2,7,0,2},{SCOPE_PAREN_ENTER3,7,0,3},{SCOPE_PAREN_ENTER4,7,0,4},{SCOPE_PAREN_ENTER5,7,1,5},{SCOPE_PAREN_ENTER6,7,0,6},{SCOPE_PAREN_ENTER7,7,0,7},{SCOPE_CLEAR_N_ENTER0,1,0,0},{SCOPE_CLEAR_N_ENTER1,1,0,1},{SCOPE_SET_NH_ENTER0,3,3,0},{SCOPE_SET_NH_ENTER1,3,3,1},{SCOPE_SET_NH_ENTER2,3,3,2},{SCOPE_SET_NH_ENTER3,3,3,3},{SCOPE_SET_N_R_CLEAR_H_ENTER0,11,9,0},{SCOPE_SET_N_R_CLEAR_H_ENTER1,11,9,1},{SCOPE_SET_N_R_CLEAR_H_ENTER2,11,9,2},{SCOPE_SET_N_R_CLEAR_H_ENTER3,11,9,3},{SCOPE_SET_N_R_CLEAR_H_ENTER8,11,9,8},{SCOPE_SET_N_R_CLEAR_H_ENTER9,11,9,9},{SCOPE_SET_N_R_CLEAR_H_ENTER10,11,9,10},{SCOPE_SET_N_R_CLEAR_H_ENTER11,11,9,11},{SCOPE_SET_A_ENTER0,4,4,0},{SCOPE_SET_A_ENTER4,4,4,4}};
static const ExprScopeToken expr_scope_restores[] = {{SCOPE_CLEAR_NHA_RESTORE0,7,0,0},{SCOPE_CLEAR_NHA_RESTORE1,7,1,0},{SCOPE_CLEAR_NHA_RESTORE2,7,2,0},{SCOPE_CLEAR_NHA_RESTORE3,7,3,0},{SCOPE_CLEAR_NHA_RESTORE4,7,4,0},{SCOPE_CLEAR_NHA_RESTORE5,7,5,0},{SCOPE_CLEAR_NHA_RESTORE6,7,6,0},{SCOPE_CLEAR_NHA_RESTORE7,7,7,0},{SCOPE_SET_N_RESTORE0,1,0,0},{SCOPE_SET_N_RESTORE1,1,1,0},{SCOPE_PAREN_RESTORE0,7,0,0},{SCOPE_PAREN_RESTORE1,7,1,0},{SCOPE_PAREN_RESTORE2,7,2,0},{SCOPE_PAREN_RESTORE3,7,3,0},{SCOPE_PAREN_RESTORE4,7,4,0},{SCOPE_PAREN_RESTORE5,7,5,0},{SCOPE_PAREN_RESTORE6,7,6,0},{SCOPE_PAREN_RESTORE7,7,7,0},{SCOPE_CLEAR_N_RESTORE0,1,0,0},{SCOPE_CLEAR_N_RESTORE1,1,1,0},{SCOPE_SET_NH_RESTORE0,3,0,0},{SCOPE_SET_NH_RESTORE1,3,1,0},{SCOPE_SET_NH_RESTORE2,3,2,0},{SCOPE_SET_NH_RESTORE3,3,3,0},{SCOPE_SET_N_R_CLEAR_H_RESTORE0,11,0,0},{SCOPE_SET_N_R_CLEAR_H_RESTORE1,11,1,0},{SCOPE_SET_N_R_CLEAR_H_RESTORE2,11,2,0},{SCOPE_SET_N_R_CLEAR_H_RESTORE3,11,3,0},{SCOPE_SET_N_R_CLEAR_H_RESTORE8,11,8,0},{SCOPE_SET_N_R_CLEAR_H_RESTORE9,11,9,0},{SCOPE_SET_N_R_CLEAR_H_RESTORE10,11,10,0},{SCOPE_SET_N_R_CLEAR_H_RESTORE11,11,11,0},{SCOPE_SET_A_RESTORE0,4,0,0},{SCOPE_SET_A_RESTORE4,4,4,0}};
static bool emit_expr_scope(Scanner *scanner, TSLexer *lexer, enum TokenType token, uint8_t mask, uint8_t value) {
 lexer->mark_end(lexer);
 scanner->expr_flags = (scanner->expr_flags & ~mask) | value;
 scanner->scope_heartbeat = (scanner->scope_heartbeat + 1) & 3;
 lexer->result_symbol = token; return true;
}
static bool scan_expr_scope(Scanner *scanner, TSLexer *lexer, const bool *valid) {
 for(unsigned i=0;i<sizeof(expr_scope_enters)/sizeof(expr_scope_enters[0]);i++) {
  const ExprScopeToken *t=&expr_scope_enters[i];
  if(valid[t->token]&&(scanner->expr_flags&t->mask)==t->old)return emit_expr_scope(scanner,lexer,t->token,t->mask,t->value);
 }
 if(valid[SCOPE_DELIMITED_END])return emit_expr_scope(scanner,lexer,SCOPE_DELIMITED_END,0,0);
 unsigned count=0,which=0;
 for(unsigned i=0;i<sizeof(expr_scope_restores)/sizeof(expr_scope_restores[0]);i++)if(valid[expr_scope_restores[i].token]){count++;which=i;}
 if(count!=1)return false;
 const ExprScopeToken *t=&expr_scope_restores[which];return emit_expr_scope(scanner,lexer,t->token,t->mask,t->value);
}

// Shared dispatch peeks retain line information for later predicates.
static void skip_dispatch_ws(TSLexer *lexer, bool *newline, bool *markup_newline, bool *markup_whitespace) {
  while(is_ascii_ws(lexer->lookahead)) {
    if(lexer->lookahead=='\f') {*markup_whitespace=false;*markup_newline=false;}
    if(lexer->lookahead=='\n') {*newline=true;if(*markup_whitespace)*markup_newline=true;}
    lexer->advance(lexer,true);
  }
}

// Called only with a cached brace FIRST. A negative peek ends this scanner call.
static bool attempt_nominal_body(TSLexer *lexer) {
  lexer->advance(lexer,false);
  if(!skip_yield_trivia(lexer) || !is_label_start(lexer->lookahead))return false;
  char first_field[32]={0};unsigned first_length=0;bool first_long=false;
  do {if(first_length+1<sizeof(first_field))first_field[first_length++]=(char)lexer->lookahead;else first_long=true;lexer->advance(lexer,false);} while(is_label_continue(lexer->lookahead));
  if(!first_long && (!strcmp(first_field,"let") || !strcmp(first_field,"mut") || !strcmp(first_field,"var") || !strcmp(first_field,"const") || !strcmp(first_field,"const_assert") || !strcmp(first_field,"override") || !strcmp(first_field,"enable") || !strcmp(first_field,"requires") || !strcmp(first_field,"fn") || !strcmp(first_field,"return") || !strcmp(first_field,"if") || !strcmp(first_field,"else") || !strcmp(first_field,"for") || !strcmp(first_field,"while") || !strcmp(first_field,"loop") || !strcmp(first_field,"break") || !strcmp(first_field,"continue") || !strcmp(first_field,"match") || !strcmp(first_field,"enum") || !strcmp(first_field,"struct") || !strcmp(first_field,"impl") || !strcmp(first_field,"trait") || !strcmp(first_field,"mod") || !strcmp(first_field,"use") || !strcmp(first_field,"pub") || !strcmp(first_field,"in") || !strcmp(first_field,"as") || !strcmp(first_field,"is") || !strcmp(first_field,"where") || !strcmp(first_field,"nil") || !strcmp(first_field,"true") || !strcmp(first_field,"false") || !strcmp(first_field,"self") || !strcmp(first_field,"type") || !strcmp(first_field,"newtype") || !strcmp(first_field,"comptime") || !strcmp(first_field,"async") || !strcmp(first_field,"await") || !strcmp(first_field,"yield") || !strcmp(first_field,"spawn") || !strcmp(first_field,"scope") || !strcmp(first_field,"select") || !strcmp(first_field,"every") || !strcmp(first_field,"after") || !strcmp(first_field,"timeout") || !strcmp(first_field,"sleep") || !strcmp(first_field,"try") || !strcmp(first_field,"catch") || !strcmp(first_field,"finally") || !strcmp(first_field,"defer") || !strcmp(first_field,"defer_ok") || !strcmp(first_field,"defer_err") || !strcmp(first_field,"guard") || !strcmp(first_field,"macro") || !strcmp(first_field,"on") || !strcmp(first_field,"schema") || !strcmp(first_field,"machine") || !strcmp(first_field,"effect") || !strcmp(first_field,"handle") || !strcmp(first_field,"perform") || !strcmp(first_field,"with") || !strcmp(first_field,"using")))return false;
  if(!skip_yield_trivia(lexer) || lexer->lookahead!=':')return false;
  lexer->advance(lexer,false);return lexer->lookahead!=':';
}

static bool scan_static_pratt_end(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool prefix_newline, bool prefix_markup_newline, bool prefetched_slash) {
  if(!prefetched_slash)lexer->mark_end(lexer);
  bool newline = prefix_newline, markup_newline = prefix_markup_newline;
  const bool trivia_complete = !prefetched_slash && skip_pratt_end_trivia(lexer, &newline, &markup_newline);
  const int32_t first = trivia_complete ? lexer->lookahead : '/';
  int32_t second = 0, third = 0;
  unsigned lbp = 0;
  bool potential_postfix = false, contextual_zero = false, contextual_suffix_word = false;
  // Required payload FIRST decisions precede optional outer completion.
  // These are the existing specific Every guard and a brace-only counterpart,
  // not a new enumeration of general expression FIRST.
  // The contextual attempt prefix owns its block before an outer header
  // can close. The compiler's identifier-colon exception remains nominal.
  if(trivia_complete && first=='{' && valid_symbols[ATTEMPT_BLOCK_START]) {
    if(attempt_nominal_body(lexer)) {
      if(!valid_symbols[ATTEMPT_NOMINAL_START] || (scanner->expr_flags&2))return false;
      lexer->result_symbol=ATTEMPT_NOMINAL_START;return true;
    }
    lexer->result_symbol=ATTEMPT_BLOCK_START;return true;
  }
  if (trivia_complete && first == '{' && valid_symbols[PRATT_BRACED_PAYLOAD_START]) {
    lexer->result_symbol = PRATT_BRACED_PAYLOAD_START; return true;
  }
  if (trivia_complete && first != '{' && valid_symbols[EVERY_INTERVAL_START]) {
    if(first=='-') { lexer->advance(lexer,false);if(lexer->lookahead=='>')return false; }
    lexer->result_symbol = EVERY_INTERVAL_START; return true;
  }
  if (first == '{') {
    if(valid_symbols[ATTEMPT_NOMINAL_START] && !(scanner->expr_flags&2)) {
      if(!attempt_nominal_body(lexer))return false;
      lexer->result_symbol=ATTEMPT_NOMINAL_START;return true;
    }
    if (valid_symbols[HYBRID_NOMINAL_START]) {
      const bool resource=(scanner->expr_flags & 8) != 0;
      if (!(scanner->expr_flags & 2) && pratt_nominal_body(lexer,resource,(scanner->expr_flags & 1) != 0)) {
        lexer->result_symbol=HYBRID_NOMINAL_START;return true;
      }
    }
    if (valid_symbols[NOMINAL_NTL_START] || valid_symbols[NOMINAL_RESOURCE_START]) {
      const bool resource = valid_symbols[NOMINAL_RESOURCE_START];
      if (pratt_nominal_body(lexer, resource, true)) {
        lexer->result_symbol = resource ? NOMINAL_RESOURCE_START : NOMINAL_NTL_START;
        return true;
      }
      // N=true does not admit callbacks. A failed nominal gate leaves the
      // brace to the caller body, without changing this zero-width marker.
    } else if (valid_symbols[TRAILING_CALLBACK_PIPE] && !newline) {
      lexer->advance(lexer, false);
      if (skip_yield_trivia(lexer) && lexer->lookahead == '|') {
        lexer->result_symbol = TRAILING_CALLBACK_PIPE; return true;
      }
    }
  } else if (is_label_start(first)) {
    char word[80] = {0}; unsigned length = 0;
    do {
      if (length + 1 < sizeof(word)) word[length] = (char)lexer->lookahead;
      length++; lexer->advance(lexer, false);
    } while (is_label_continue(lexer->lookahead));
    const bool bounded = length < sizeof(word);
    if(bounded && !strcmp(word,"else") && valid_symbols[IF_ELSE_KEYWORD]) {lexer->mark_end(lexer);lexer->result_symbol=IF_ELSE_KEYWORD;return true;}
    contextual_suffix_word = bounded && !strcmp(word,"within");

    if (bounded && (!strcmp(word, "as") || !strcmp(word, "is") || !strcmp(word, "in"))) lbp = 11;
    bool after_newline = false, after_markup_newline = false;
    const bool brace = skip_pratt_end_trivia(lexer, &after_newline, &after_markup_newline) && lexer->lookahead == '{';
    contextual_zero = bounded && brace && (!strcmp(word, "with") || !strcmp(word, "recover"));
    if (!newline && brace && (!bounded || (!callback_keyword(word) && strcmp(word, "recover")))) {
      const bool within = bounded && !strcmp(word, "within");
      if (within && valid_symbols[AWAIT_WITHIN_CALL_END]) { lexer->result_symbol = AWAIT_WITHIN_CALL_END; return true; }
      if (within && !(scanner->expr_flags & 4) && valid_symbols[NAMED_CALLBACK_START] && !valid_symbols[AWAIT_NAMED_CALLBACK_START]) { lexer->result_symbol = NAMED_CALLBACK_START; return true; }
      if (!within && valid_symbols[AWAIT_NAMED_CALLBACK_START]) { lexer->result_symbol = AWAIT_NAMED_CALLBACK_START; return true; }
      if (!within && valid_symbols[NAMED_CALLBACK_START]) { lexer->result_symbol = NAMED_CALLBACK_START; return true; }
    }
    if(bounded && valid_symbols[APPROX_MODE_START] && (!strcmp(word,"relative")||!strcmp(word,"ulps"))) {lexer->result_symbol=APPROX_MODE_START;return true;}
    // The low-tail start is zero-width. A later scanner invocation consumes
    // the original with/recover token, preserving its authored word span and
    // all intervening named comment extras.
  } else {
    if (!trivia_complete) second = lexer->lookahead;
    if (trivia_complete && !lexer->eof(lexer)) {
      lexer->advance(lexer, false); second = lexer->lookahead;
      if (!lexer->eof(lexer)) { lexer->advance(lexer, false); third = lexer->lookahead; }
    }
    potential_postfix = first == '(' || first == '[' ||
      (first == '.' && second != '.') || (first == ':' && second == ':') ||
      (first == '?' && second != '?' && second != '>') || (first == '!' && second == '.');
    if (first == '.' && second == '.') lbp = 19;
    else if (first == '?' && second == '?' && third != '=') lbp = 3;
    else if ((first == '?' && second == '>') || (first == '|' && second == '>') || (first == '&' && second == '.')) lbp = 1;
    else if (first == '|' && second == '|') lbp = 5;
    else if (first == '&' && second == '&') lbp = 7;
    else if ((first == '=' && second == '=') || (first == '!' && second == '=') || (first == '~' && second == '=') || (first == '!' && second == '~' && third == '=')) lbp = 9;
    else if ((first == '<' && second == '<') || (first == '>' && second == '>')) lbp = 21;
    else if (first == '<' || first == '>') lbp = 11;
    else if (first == '|' && second != '=') lbp = 13;
    else if (first == '^' && second != '=') lbp = 15;
    else if (first == '&' && second != '=') lbp = 17;
    else if ((first == '+' || first == '-') && second != '=' && !(first == '-' && second == '>')) lbp = 23;
    else if (first == '*' && second == '*') lbp = 28;
    else if ((first == '*' || first == '/' || first == '%') && second != '=') lbp = 25;
    else if (first == '~' && second == '/' && third != '=') lbp = 25;
    // The existing markup scanner owns a new-line '<tag' before ordinary
    // comparison lexing. Close the Pratt scope here, then let that original
    // scanner emit the real '<' token with its original trivia/span policy.
    // Comment-internal LF and FF/VT/NBSP do not manufacture this eligibility.
    if (first == '<' && markup_newline && valid_symbols[MARKUP_LT] && opens_a_tag(second)) lbp = 0;
  }
  // Closing an already parsed call suffix is independent of closing its
  // enclosing Pratt payload. Admitted callback starts were dispatched above.
  if (valid_symbols[CALLBACK_END]) { scanner->comment_callback_newline=0;lexer->result_symbol = CALLBACK_END; return true; }
  // Admitted named callbacks and their call-ending decision retain the old
  // dispatch priority. A remaining real suffix cannot be preempted by END
  // through the optional approximation tail (e.g. yield a ~= b within c).
  if (contextual_suffix_word && valid_symbols[CONTEXTUAL_SUFFIX_START]) {
    lexer->result_symbol = CONTEXTUAL_SUFFIX_START; return true;
  }
  const unsigned bps[] = {0, 1, 2, 10, 20, 27};
  // Select the innermost/lower threshold first. A refused inner barrier must
  // not fall through to an outer threshold on that same continuation.
  for (unsigned b = 0; b < 6; b++) {
    bool active = false;
    for (unsigned i = 0; i < STATIC_PRATT_END_COUNT; i++)
      if (static_pratt_ends[i].bp == bps[b]) active |= valid_symbols[static_pratt_ends[i].symbol];
    if (!active) continue;
    if (contextual_zero && bps[b] == 0 && valid_symbols[PRATT_LOW_TAIL_START]) {
      lexer->result_symbol = PRATT_LOW_TAIL_START; return true;
    }
    if (first == '.' && second == '.' && third != '.' && valid_symbols[RANGE_OPERATOR_START] && lbp >= bps[b]) {
      lexer->result_symbol=RANGE_OPERATOR_START; return true;
    }
    if (potential_postfix || (lbp && lbp >= bps[b]) || (contextual_zero && bps[b] == 0)) return false;
    for (unsigned i = 0; i < STATIC_PRATT_END_COUNT; i++) if (static_pratt_ends[i].bp == bps[b] && valid_symbols[static_pratt_ends[i].symbol]) {
      lexer->result_symbol = static_pratt_ends[i].symbol;
      if (lexer->log) lexer->log(lexer, "static_pratt_end bp:%u shared_predicate:1 serialized_flags:0", bps[b]);
      return true;
    }
  }
  if (first == '.' && second == '.' && third != '.' && valid_symbols[RANGE_OPERATOR_START]) { lexer->result_symbol=RANGE_OPERATOR_START; return true; }
  if (contextual_zero && valid_symbols[PRATT_LOW_TAIL_START]) {
    lexer->result_symbol = PRATT_LOW_TAIL_START; return true;
  }
  return false;
}

// A numeric decision occurs immediately after a magnitude, before trivia.
// Distinct markers preserve all suffix alternatives despite keyword extraction
// and aliases. The plain-number END is mandatory, so a comment cannot create
// a new adjacency to an unrelated identifier.
static bool scan_numeric_suffix(TSLexer *lexer, const bool *valid) {
  bool active=false;
  for (unsigned i=NUMERIC_DURATION_START;i<=NUMERIC_LITERAL_END;i++) active |= valid[i];
  if (!active) return false;
  lexer->mark_end(lexer);
  char word[32]={0}; unsigned length=0; bool long_word=false, micro=false;
  if (lexer->lookahead==0xb5) { micro=true; lexer->advance(lexer,false); }
  else if (!((lexer->lookahead>='a'&&lexer->lookahead<='z') || (lexer->lookahead>='A'&&lexer->lookahead<='Z'))) {
    if (!valid[NUMERIC_LITERAL_END]) return false;
    lexer->result_symbol=NUMERIC_LITERAL_END;return true;
  }
  while ((lexer->lookahead>='a'&&lexer->lookahead<='z') || (lexer->lookahead>='A'&&lexer->lookahead<='Z') || (lexer->lookahead>='0'&&lexer->lookahead<='9') || lexer->lookahead=='_') {
    if (length+1<sizeof(word)) word[length++]=(char)lexer->lookahead;else long_word=true;
    lexer->advance(lexer,false);
  }
  enum TokenType token=NUMERIC_UNIT_START;
  if (!long_word && ((micro&&!strcmp(word,"s")) || (!micro&&(!strcmp(word,"min")||!strcmp(word,"ms")||!strcmp(word,"ns")||!strcmp(word,"us")||!strcmp(word,"s")||!strcmp(word,"h")||!strcmp(word,"w"))))) token=NUMERIC_DURATION_START;
  else if (!micro&&!long_word&&(!strcmp(word,"f")||!strcmp(word,"f32")||!strcmp(word,"f64"))) token=NUMERIC_FLOAT_START;
  else if (!micro&&!long_word&&(!strcmp(word,"i")||!strcmp(word,"i8")||!strcmp(word,"i16")||!strcmp(word,"i32")||!strcmp(word,"i64")||!strcmp(word,"isize")||!strcmp(word,"u")||!strcmp(word,"u8")||!strcmp(word,"u16")||!strcmp(word,"u32")||!strcmp(word,"u64")||!strcmp(word,"usize"))) token=NUMERIC_INTEGER_START;
  else if (!micro&&!long_word&&(!strcmp(word,"d")||!strcmp(word,"bd"))) token=NUMERIC_DECIMAL_START;
  else if (!micro&&!long_word&&!strcmp(word,"n")) token=NUMERIC_BIGINT_START;
  else if (micro) { if(!valid[NUMERIC_LITERAL_END])return false;token=NUMERIC_LITERAL_END; }
  if (!valid[token] && token != NUMERIC_LITERAL_END && valid[NUMERIC_UNIT_START] && !micro) token=NUMERIC_UNIT_START;
  if (!valid[token]) return false;
  lexer->result_symbol=token;return true;
}

bool tree_sitter_zolo_external_scanner_scan(void *payload, TSLexer *lexer,
                                            const bool *valid_symbols) {
  Scanner *scanner = payload;
  const bool callback_phase=valid_symbols[NAMED_CALLBACK_START]||valid_symbols[AWAIT_NAMED_CALLBACK_START]||valid_symbols[AWAIT_WITHIN_CALL_END]||valid_symbols[TRAILING_CALLBACK_PIPE]||valid_symbols[CALLBACK_END];
  if(!callback_phase)scanner->comment_callback_newline=0;
  bool dispatch_newline=callback_phase&&scanner->comment_callback_newline,dispatch_markup_newline=false,dispatch_markup_whitespace=true;

  // See the comment on `ERROR_SENTINEL`: this is true ONLY while tree-sitter
  // is in error recovery, probing every external token regardless of
  // whether the grammar actually expects it here. Bailing before either
  // raw-text scan is the fix — without it, `scan_raw_text` ran at any error
  // position and consumed to the next `</style`/`</script` or EOF.
  if (valid_symbols[ERROR_SENTINEL]) {
    return false;
  }

  // The comma belongs to the condition chain before an alternative header
  // can emit END/restore and commit to a single-clause if/guard.
  if (valid_symbols[CONDITION_CHAIN_COMMA]) {
    skip_dispatch_ws(lexer,&dispatch_newline,&dispatch_markup_newline,&dispatch_markup_whitespace);
    if (lexer->lookahead==',') { lexer->advance(lexer,false);lexer->mark_end(lexer);lexer->result_symbol=CONDITION_CHAIN_COMMA;return true; }
    // Let the internal lexer retain comment extras (or an infix slash).
    if (lexer->lookahead=='/') return false;
  }
  // A map's authored if: key must resolve before a condition's zero-byte
  // scope entry commits the competing conditional-entry branch.
  if(valid_symbols[MAP_IF_CONDITION_START]) {
    lexer->mark_end(lexer);
    if(!skip_yield_trivia(lexer) || lexer->lookahead==':')return false;
    lexer->result_symbol=MAP_IF_CONDITION_START;return true;
  }
  if (scan_expr_scope(scanner,lexer,valid_symbols)) return true;
  bool numeric_decision=false;
  for(unsigned i=NUMERIC_DURATION_START;i<=NUMERIC_LITERAL_END;i++)numeric_decision|=valid_symbols[i];
  if(numeric_decision)return scan_numeric_suffix(lexer,valid_symbols);
  // Closing call arguments must precede callback and optional-range peeks.
  // Skip only ordinary whitespace; comments remain lexer extras.
  if(valid_symbols[SCOPE_ARGS_END]) {
    skip_dispatch_ws(lexer,&dispatch_newline,&dispatch_markup_newline,&dispatch_markup_whitespace);
    if(lexer->lookahead==')')return emit_expr_scope(scanner,lexer,SCOPE_ARGS_END,0,0);
  }
  // These lexical scope delimiters must not be starved by optional callbacks.
  if(valid_symbols[FOREIGN_SCOPE_OPEN] || valid_symbols[FOREIGN_SCOPE_CLOSE]) {
    skip_dispatch_ws(lexer,&dispatch_newline,&dispatch_markup_newline,&dispatch_markup_whitespace);
    if(valid_symbols[FOREIGN_SCOPE_OPEN] && lexer->lookahead=='{') { lexer->advance(lexer,false);if(scanner->scope!=UINT16_MAX)scanner->scope++;lexer->mark_end(lexer);lexer->result_symbol=FOREIGN_SCOPE_OPEN;return true; }
    if(valid_symbols[FOREIGN_SCOPE_CLOSE] && lexer->lookahead=='}') { lexer->advance(lexer,false);while(scanner->count&&scanner->bindings[scanner->count-1].scope==scanner->scope)scanner->count--;if(scanner->scope)scanner->scope--;lexer->mark_end(lexer);lexer->result_symbol=FOREIGN_SCOPE_CLOSE;return true; }
  }
  bool scoped_symbols[ERROR_SENTINEL + 1];
  if (scanner->expr_flags & 1) {
    memcpy(scoped_symbols,valid_symbols,sizeof(scoped_symbols));
    scoped_symbols[NAMED_CALLBACK_START]=false;
    scoped_symbols[AWAIT_NAMED_CALLBACK_START]=false;
    scoped_symbols[TRAILING_CALLBACK_PIPE]=false;
    valid_symbols=scoped_symbols;
  }

  bool wants_pratt_end = false;
  for (unsigned i = 0; i < STATIC_PRATT_END_COUNT; i++) wants_pratt_end |= valid_symbols[static_pratt_ends[i].symbol];
  if(!wants_pratt_end && valid_symbols[ATTEMPT_BLOCK_START]) {
    lexer->mark_end(lexer);
    if(!skip_pratt_end_trivia(lexer,&dispatch_newline,&dispatch_markup_newline))return false;
    if(lexer->lookahead=='{') {
      if(attempt_nominal_body(lexer)) {
        if(!valid_symbols[ATTEMPT_NOMINAL_START] || (scanner->expr_flags&2))return false;
        lexer->result_symbol=ATTEMPT_NOMINAL_START;return true;
      }
      lexer->result_symbol=ATTEMPT_BLOCK_START;return true;
    }
    return false;
  }
  if(!wants_pratt_end && valid_symbols[ATTEMPT_NOMINAL_START] && !(scanner->expr_flags&2)) {
    lexer->mark_end(lexer);
    if(!skip_pratt_end_trivia(lexer,&dispatch_newline,&dispatch_markup_newline))return false;
    if(lexer->lookahead=='{') {
      if(!attempt_nominal_body(lexer))return false;
      lexer->result_symbol=ATTEMPT_NOMINAL_START;return true;
    }
  }
  // Comment tokens are opaque named leaves. Keep their newline decision
  // through the current optional callback phase, including zero-width gates.
  const bool raw_body=valid_symbols[STYLE_RAW_TEXT]||valid_symbols[SCRIPT_RAW_TEXT]||valid_symbols[TITLED_RAW_TITLE]||valid_symbols[TITLED_RAW_TRIPLE_TITLE]||valid_symbols[FOREIGN_BODY]||valid_symbols[PYTHON_FOREIGN_BODY]||valid_symbols[JAVASCRIPT_FOREIGN_BODY]||valid_symbols[TYPESCRIPT_FOREIGN_BODY]||valid_symbols[JAVA_FOREIGN_BODY]||valid_symbols[KOTLIN_FOREIGN_BODY]||valid_symbols[C_FOREIGN_BODY]||valid_symbols[CPP_FOREIGN_BODY]||valid_symbols[RUST_FOREIGN_BODY]||valid_symbols[GO_FOREIGN_BODY];
  if(valid_symbols[BLOCK_COMMENT] && !raw_body) {
    skip_dispatch_ws(lexer,&dispatch_newline,&dispatch_markup_newline,&dispatch_markup_whitespace);
    if(lexer->lookahead=='/') {
      lexer->mark_end(lexer);lexer->advance(lexer,false);
      if(lexer->lookahead=='*') {
        lexer->advance(lexer,false);unsigned depth=1;bool comment_newline=dispatch_newline;
        while(depth&&!lexer->eof(lexer)) {const int32_t c=lexer->lookahead;if(c=='\n')comment_newline=true;lexer->advance(lexer,false);if(c=='/'&&lexer->lookahead=='*'){lexer->advance(lexer,false);depth++;}else if(c=='*'&&lexer->lookahead=='/'){lexer->advance(lexer,false);depth--;}}
        if(depth)return false;
        scanner->comment_callback_newline=callback_phase&&comment_newline;
        lexer->mark_end(lexer);lexer->result_symbol=BLOCK_COMMENT;return true;
      }
      // A division peek must still close a higher-BP operand or a call suffix.
      if(wants_pratt_end)return scan_static_pratt_end(scanner,lexer,valid_symbols,dispatch_newline,dispatch_markup_newline,true);
      if(valid_symbols[CALLBACK_END]){scanner->comment_callback_newline=0;lexer->result_symbol=CALLBACK_END;return true;}
      return false;
    }
  }
  if (!wants_pratt_end && (valid_symbols[EVERY_INTERVAL_START] || valid_symbols[PRATT_BRACED_PAYLOAD_START] || valid_symbols[NOMINAL_UNRESTRICTED_START])) {
    lexer->mark_end(lexer);
    if (!skip_yield_trivia(lexer)) return false;
    const bool brace = lexer->lookahead == '{';
    if (brace && valid_symbols[PRATT_BRACED_PAYLOAD_START]) lexer->result_symbol = PRATT_BRACED_PAYLOAD_START;
    else if (brace && valid_symbols[NOMINAL_UNRESTRICTED_START]) lexer->result_symbol = NOMINAL_UNRESTRICTED_START;
    else if (!brace && valid_symbols[EVERY_INTERVAL_START]) {
      if(lexer->lookahead=='-') { lexer->advance(lexer,false);if(lexer->lookahead=='>')return false; }
      lexer->result_symbol = EVERY_INTERVAL_START;
    }
    else return false;
    return true;
  }

  if (!wants_pratt_end && (valid_symbols[NOMINAL_NTL_START] || valid_symbols[NOMINAL_RESOURCE_START])) {
    const bool resource = valid_symbols[NOMINAL_RESOURCE_START];
    if (!scan_restricted_nominal_start(lexer, resource)) return false;
    lexer->result_symbol = resource ? NOMINAL_RESOURCE_START : NOMINAL_NTL_START;
    return true;
  }

  if (valid_symbols[IS_TYPE_START]) {
    if (!scan_is_type_start(lexer)) return false;
    lexer->result_symbol = IS_TYPE_START;
    return true;
  }

  if (valid_symbols[TEMPORAL_CALL_END]) {
    lexer->mark_end(lexer);
    lexer->result_symbol = TEMPORAL_CALL_END;
    return true;
  }

  if (valid_symbols[TITLED_RAW_TITLE] || valid_symbols[TITLED_RAW_TRIPLE_TITLE]) {
    return scan_titled_raw_title(lexer, valid_symbols[TITLED_RAW_TITLE], valid_symbols[TITLED_RAW_TRIPLE_TITLE]);
  }

  if (valid_symbols[NON_DOT_EXPRESSION_START]) {
    if (!scan_non_dot_expression_start(lexer,dispatch_newline)) return false;
    lexer->result_symbol = NON_DOT_EXPRESSION_START;
    return true;
  }

  // Commit yield's empty/payload decision before its operand is lexed.
  // Preserve the original zero-width end so trivia cannot extend yield.
  if (valid_symbols[YIELD_EMPTY_END] || valid_symbols[YIELD_PAYLOAD_START]) {
    lexer->mark_end(lexer);
    const bool trivia_complete = skip_yield_trivia(lexer);
    const bool empty = trivia_complete && (lexer->eof(lexer) ||
      lexer->lookahead == '}' || lexer->lookahead == ';' ||
      lexer->lookahead == ')' || lexer->lookahead == ',');
    const enum TokenType token = empty ? YIELD_EMPTY_END : YIELD_PAYLOAD_START;
    if (!valid_symbols[token]) return false;
    lexer->result_symbol = token;
    return true;
  }

  if (wants_pratt_end) return scan_static_pratt_end(scanner, lexer, valid_symbols,dispatch_newline,dispatch_markup_newline,false);

  // A pipe-only optional callback cannot own the handle separator.
  if(valid_symbols[HANDLE_SEPARATOR] && valid_symbols[TRAILING_CALLBACK_PIPE] &&
     !valid_symbols[NAMED_CALLBACK_START] && !valid_symbols[AWAIT_NAMED_CALLBACK_START] &&
     !valid_symbols[CALLBACK_END] && !valid_symbols[AWAIT_WITHIN_CALL_END]) {
    skip_dispatch_ws(lexer,&dispatch_newline,&dispatch_markup_newline,&dispatch_markup_whitespace);
    if(lexer->lookahead=='w') {if(!scan_word(lexer,"with"))return false;lexer->mark_end(lexer);lexer->result_symbol=HANDLE_SEPARATOR;return true;}
  }
  const bool only_callback_pipe=valid_symbols[TRAILING_CALLBACK_PIPE] && !valid_symbols[NAMED_CALLBACK_START] && !valid_symbols[AWAIT_NAMED_CALLBACK_START] && !valid_symbols[CALLBACK_END] && !valid_symbols[AWAIT_WITHIN_CALL_END];
  if(only_callback_pipe)skip_dispatch_ws(lexer,&dispatch_newline,&dispatch_markup_newline,&dispatch_markup_whitespace);
  if (!only_callback_pipe || lexer->lookahead=='{') {
  if (valid_symbols[NAMED_CALLBACK_START] || valid_symbols[TRAILING_CALLBACK_PIPE] || valid_symbols[CALLBACK_END] || valid_symbols[AWAIT_NAMED_CALLBACK_START] || valid_symbols[AWAIT_WITHIN_CALL_END]) {
    if (scan_callback_boundary(lexer, valid_symbols[NAMED_CALLBACK_START], valid_symbols[TRAILING_CALLBACK_PIPE], valid_symbols[AWAIT_NAMED_CALLBACK_START], valid_symbols[AWAIT_WITHIN_CALL_END], (scanner->expr_flags & 4) != 0,dispatch_newline)) return true;
    if (valid_symbols[CALLBACK_END]) {
      scanner->comment_callback_newline=0;lexer->result_symbol = CALLBACK_END;
      return true;
    }
    return false;
  }

  } // Pipe-only callbacks leave other FIRST tokens to the common selector.

  if (valid_symbols[FOREIGN_PROVIDER]) {
    skip_dispatch_ws(lexer,&dispatch_newline,&dispatch_markup_newline,&dispatch_markup_whitespace);
    uint64_t provider;
    if (!read_foreign_name(lexer, &provider)) return false;
    scanner->dialect = FOREIGN_BRACED;
    for (unsigned i = scanner->count; i > 0; i--) {
      if (scanner->bindings[i - 1].name == provider) {
        scanner->dialect = scanner->bindings[i - 1].dialect;
        break;
      }
    }
    lexer->mark_end(lexer);
    lexer->result_symbol = FOREIGN_PROVIDER;
    return true;
  }

  if (valid_symbols[FOREIGN_GROUP_OPEN]) {
    skip_dispatch_ws(lexer,&dispatch_newline,&dispatch_markup_newline,&dispatch_markup_whitespace);
    if (lexer->lookahead != '{') return false;
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    if (!skip_foreign_group_trivia(lexer)) return false;
    if (lexer->lookahead == 'a') {
      if (!scan_word(lexer, "async")) return false;
      if (!skip_foreign_group_trivia(lexer)) return false;
    }
    if (!scan_word(lexer, "fn")) return false;
    lexer->result_symbol = FOREIGN_GROUP_OPEN;
    return true;
  }

  if ((valid_symbols[FOREIGN_BODY] || valid_symbols[PYTHON_FOREIGN_BODY] ||
       valid_symbols[JAVASCRIPT_FOREIGN_BODY] || valid_symbols[TYPESCRIPT_FOREIGN_BODY] || valid_symbols[JAVA_FOREIGN_BODY] ||
       valid_symbols[KOTLIN_FOREIGN_BODY] || valid_symbols[C_FOREIGN_BODY] || valid_symbols[CPP_FOREIGN_BODY] || valid_symbols[RUST_FOREIGN_BODY] || valid_symbols[GO_FOREIGN_BODY]) &&
      (scanner->dialect == FOREIGN_JAVA ? scan_java_body(lexer) :
       scanner->dialect == FOREIGN_JAVASCRIPT || scanner->dialect == FOREIGN_TYPESCRIPT ? scan_ecma_code(lexer, false, true, 0) :
       scanner->dialect >= FOREIGN_KOTLIN ? scan_native_code(lexer, scanner->dialect, false, 0) :
       scan_foreign_body(lexer, scanner->dialect == FOREIGN_PYTHON))) {
    lexer->result_symbol = scanner->dialect == FOREIGN_PYTHON ? PYTHON_FOREIGN_BODY :
      scanner->dialect == FOREIGN_JAVASCRIPT ? JAVASCRIPT_FOREIGN_BODY :
      scanner->dialect == FOREIGN_TYPESCRIPT ? TYPESCRIPT_FOREIGN_BODY :
      scanner->dialect == FOREIGN_JAVA ? JAVA_FOREIGN_BODY :
      scanner->dialect == FOREIGN_KOTLIN ? KOTLIN_FOREIGN_BODY :
      scanner->dialect == FOREIGN_C ? C_FOREIGN_BODY :
      scanner->dialect == FOREIGN_CPP ? CPP_FOREIGN_BODY :
      scanner->dialect == FOREIGN_RUST ? RUST_FOREIGN_BODY :
      scanner->dialect == FOREIGN_GO ? GO_FOREIGN_BODY : FOREIGN_BODY;
    return true;
  }

  if (valid_symbols[STYLE_RAW_TEXT] && scan_style_raw_text(lexer, "/style")) {
    lexer->result_symbol = STYLE_RAW_TEXT;
    return true;
  }
  if (valid_symbols[SCRIPT_RAW_TEXT] && scan_raw_text(lexer, "/script")) {
    lexer->result_symbol = SCRIPT_RAW_TEXT;
    return true;
  }

  const bool wants_label = valid_symbols[LOOP_LABEL_DECL_COLON];
  const bool wants_markup = valid_symbols[MARKUP_LT];
  const bool wants_convention = valid_symbols[CONVENTION];
  const bool wants_dependencies = valid_symbols[DEPENDENCIES_KEYWORD];
  const bool wants_update = valid_symbols[RECORD_UPDATE_WITH];
  const bool wants_handle = valid_symbols[HANDLE_SEPARATOR];
  const bool wants_recover = valid_symbols[RECOVER_KEYWORD];
  const bool wants_suffix = valid_symbols[CONTEXTUAL_SUFFIX_START];
  const bool wants_mode = valid_symbols[APPROX_MODE_START];
  const bool wants_else = valid_symbols[IF_ELSE_KEYWORD];
  const bool wants_import = valid_symbols[FOREIGN_IMPORT];
  const bool wants_range = valid_symbols[RANGE_OPERATOR_START];
  const bool wants_nominal = valid_symbols[HYBRID_NOMINAL_START];
  const bool wants_low_tail = valid_symbols[PRATT_LOW_TAIL_START];
  const bool wants_args_end = valid_symbols[SCOPE_ARGS_END];
  if (!wants_label && !wants_markup && !wants_convention && !wants_dependencies && !wants_update && !wants_handle && !wants_recover && !wants_suffix && !wants_import && !wants_range && !wants_nominal && !wants_low_tail && !wants_args_end && !wants_mode && !wants_else) {
    return false;
  }

  // The scanner runs BEFORE extras are skipped, which is the only reason it
  // can see the newline at all — once whitespace has been consumed as an
  // extra, the newline is gone. Skipping with `advance(_, true)` marks these
  // bytes as whitespace so they never land inside a token.
  //
  // Side effect, accepted: when the scan below bails, the whitespace it
  // already skipped is not handed back, so it is tokenized as an extra
  // instead of as part of the following `markup_text`. Whitespace-only text
  // is dropped by the compiler (`normalize_markup_text`) and carries no
  // highlight, so the only visible consequence is that indentation before a
  // tag is sometimes inside the tag's own extent rather than beside it.
  bool saw_newline = dispatch_markup_newline;
  for (;;) {
    int32_t c = lexer->lookahead;
    if (c == '\n') {
      saw_newline = true;
    } else if (c != ' ' && c != '\t' && c != '\r') {
      break;
    }
    lexer->advance(lexer, true);
  }

  if (wants_args_end && lexer->lookahead == ')') return emit_expr_scope(scanner,lexer,SCOPE_ARGS_END,0,0);

  if (wants_nominal && lexer->lookahead == '{') {
    lexer->mark_end(lexer);
    if ((scanner->expr_flags & 2) || !pratt_nominal_body(lexer,(scanner->expr_flags & 8) != 0,(scanner->expr_flags & 1) != 0)) return false;
    lexer->result_symbol = HYBRID_NOMINAL_START; return true;
  }

  if (wants_range && lexer->lookahead == '.') {
    lexer->mark_end(lexer);
    lexer->advance(lexer, false);
    if (lexer->lookahead != '.') return false;
    lexer->advance(lexer, false);
    if (lexer->lookahead == '.') return false;
    lexer->result_symbol = RANGE_OPERATOR_START;
    return true;
  }

  // Labelled loops and markup expressions can both begin an expression. Test
  // the first non-extra byte once, then dispatch without starving markup when
  // the label token is also valid in the same parser state.
  if (wants_dependencies && lexer->lookahead == 'd') {
    if (scan_dependencies_keyword(lexer)) {
      lexer->result_symbol = DEPENDENCIES_KEYWORD;
      return true;
    }
    return false;
  }

  if (wants_label && lexer->lookahead == ':') {
    if (scan_loop_label_decl_colon(lexer)) {
      lexer->result_symbol = LOOP_LABEL_DECL_COLON;
      return true;
    }
    return false;
  }

  if (wants_convention && lexer->lookahead == 's') {
    if (scan_convention(lexer)) {
      lexer->result_symbol = CONVENTION;
      return true;
    }
    return false;
  }

  // Read overlapping contextual words once; failed peeks must not starve
  // either within/relative/ulps or the existing with/recover tokens.
  if ((wants_else && lexer->lookahead=='e') ||
      (wants_low_tail && (lexer->lookahead == 'w' || lexer->lookahead == 'r')) ||
      (wants_suffix && lexer->lookahead == 'w') ||
      (wants_mode && (lexer->lookahead == 'r' || lexer->lookahead == 'u')) ||
      (wants_import && lexer->lookahead == 'u') ||
      (wants_recover && lexer->lookahead == 'r') ||
      ((wants_update || wants_handle) && lexer->lookahead == 'w')) {
    lexer->mark_end(lexer); // suffix marker remains zero-width after whitespace
    char word[10]; unsigned length = 0;
    while (is_label_continue(lexer->lookahead)) {
      if (length == sizeof(word) - 1) return false;
      word[length++] = (char)lexer->lookahead;
      lexer->advance(lexer, false);
    }
    word[length] = '\0';
    if(wants_else && !strcmp(word,"else")){lexer->mark_end(lexer);lexer->result_symbol=IF_ELSE_KEYWORD;return true;}
    if (wants_handle && !strcmp(word,"with")) { lexer->mark_end(lexer);lexer->result_symbol=HANDLE_SEPARATOR;return true; }
    if (wants_low_tail && (!strcmp(word,"with") || !strcmp(word,"recover"))) {
      if (!skip_foreign_group_trivia(lexer) || lexer->lookahead != '{') return false;
      lexer->result_symbol=PRATT_LOW_TAIL_START;return true;
    }
    if(wants_mode && (!strcmp(word,"relative")||!strcmp(word,"ulps"))) {lexer->result_symbol=APPROX_MODE_START;return true;}
    if (wants_suffix && !strcmp(word, "within")) {
      lexer->result_symbol = CONTEXTUAL_SUFFIX_START;
      return true;
    }
    if (wants_import && !strcmp(word, "use")) {
      if (!scan_foreign_import_after_use(scanner, lexer)) return false;
      lexer->result_symbol = FOREIGN_IMPORT;
      return true;
    }
    if (wants_recover && !strcmp(word, "recover")) {
      lexer->mark_end(lexer);
      if (!skip_foreign_group_trivia(lexer) || lexer->lookahead != '{') return false;
      lexer->result_symbol = RECOVER_KEYWORD;
      return true;
    }
    if ((wants_update || wants_handle) && !strcmp(word, "with")) {
      lexer->mark_end(lexer);
      if (wants_handle) { lexer->result_symbol = HANDLE_SEPARATOR; return true; }
      if (!skip_foreign_group_trivia(lexer) || lexer->lookahead != '{') return false;
      lexer->result_symbol = RECORD_UPDATE_WITH;
      return true;
    }
    return false;
  }

  if (!wants_markup || !saw_newline || lexer->lookahead != '<') {
    return false;
  }

  lexer->advance(lexer, false);
  // The token is exactly `<`; everything after this point is a peek.
  lexer->mark_end(lexer);

  if (!opens_a_tag(lexer->lookahead)) {
    return false;
  }

  lexer->result_symbol = MARKUP_LT;
  return true;
}
