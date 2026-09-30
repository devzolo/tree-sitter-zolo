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
  ERROR_SENTINEL,
};

// Imported provider names stay ordinary identifiers. Track only their lexical
// dialect; loading a native plugin is neither needed nor allowed by a parser.
// The bounded table fits tree-sitter's 1024-byte scanner-state buffer.
#define MAX_FOREIGN_BINDINGS 90
typedef struct {
  uint64_t name;
  uint16_t scope;
  bool python;
} ForeignBinding;
typedef struct {
  ForeignBinding bindings[MAX_FOREIGN_BINDINGS];
  uint8_t count;
  uint16_t scope;
  bool python;
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
  buffer[length++] = scanner->python;
  buffer[length++] = scanner->count;
  for (unsigned i = 0; i < scanner->count; i++) {
    ForeignBinding *binding = &scanner->bindings[i];
    buffer[length++] = binding->scope & 0xff;
    buffer[length++] = binding->scope >> 8;
    buffer[length++] = binding->python;
    for (unsigned byte = 0; byte < 8; byte++) {
      buffer[length++] = (binding->name >> (byte * 8)) & 0xff;
    }
  }
  return length;
}

void tree_sitter_zolo_external_scanner_deserialize(void *payload,
                                                   const char *buffer,
                                                   unsigned length) {
  Scanner *scanner = payload;
  memset(scanner, 0, sizeof(Scanner));
  if (length < 4) return;
  unsigned offset = 0;
  scanner->scope = (uint8_t)buffer[offset++];
  scanner->scope |= (uint16_t)(uint8_t)buffer[offset++] << 8;
  scanner->python = buffer[offset++];
  unsigned count = (uint8_t)buffer[offset++];
  if (count > MAX_FOREIGN_BINDINGS) return;
  for (unsigned i = 0; i < count; i++) {
    if (offset + 11 > length) return;
    ForeignBinding *binding = &scanner->bindings[i];
    binding->scope = (uint8_t)buffer[offset++];
    binding->scope |= (uint16_t)(uint8_t)buffer[offset++] << 8;
    binding->python = buffer[offset++];
    for (unsigned byte = 0; byte < 8; byte++) {
      binding->name |= (uint64_t)(uint8_t)buffer[offset++] << (byte * 8);
    }
    scanner->count++;
  }
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
  bool python = plugin == foreign_name_hash("python") && export == foreign_name_hash("python");
  bool shadows_python = false;
  for (unsigned i = scanner->count; i > 0; i--) {
    ForeignBinding *binding = &scanner->bindings[i - 1];
    if (binding->name == name) {
      shadows_python = true;
      if (binding->scope == scanner->scope) {
        binding->python = python;
        return;
      }
    }
  }
  // Other plugin exports need no entry unless they shadow a Python binding.
  if ((!python && !shadows_python) || scanner->count == MAX_FOREIGN_BINDINGS) return;
  ForeignBinding *binding = &scanner->bindings[scanner->count++];
  binding->name = name;
  binding->scope = scanner->scope;
  binding->python = python;
}

// This hidden token consumes no authored bytes. Its grammar position is before
// a `use` declaration; lookahead records provider imports while the ordinary
// grammar still parses every import/path/list node with its original spans.
static bool scan_foreign_import(Scanner *scanner, TSLexer *lexer) {
  skip_ascii_ws(lexer);
  lexer->mark_end(lexer);
  if (!scan_word(lexer, "use") || !skip_foreign_group_trivia(lexer) ||
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

bool tree_sitter_zolo_external_scanner_scan(void *payload, TSLexer *lexer,
                                            const bool *valid_symbols) {
  Scanner *scanner = payload;

  // See the comment on `ERROR_SENTINEL`: this is true ONLY while tree-sitter
  // is in error recovery, probing every external token regardless of
  // whether the grammar actually expects it here. Bailing before either
  // raw-text scan is the fix — without it, `scan_raw_text` ran at any error
  // position and consumed to the next `</style`/`</script` or EOF.
  if (valid_symbols[ERROR_SENTINEL]) {
    return false;
  }

  if (valid_symbols[FOREIGN_IMPORT] && scan_foreign_import(scanner, lexer)) {
    lexer->result_symbol = FOREIGN_IMPORT;
    return true;
  }

  if (valid_symbols[FOREIGN_PROVIDER]) {
    skip_ascii_ws(lexer);
    uint64_t provider;
    if (!read_foreign_name(lexer, &provider)) return false;
    scanner->python = false;
    for (unsigned i = scanner->count; i > 0; i--) {
      if (scanner->bindings[i - 1].name == provider) {
        scanner->python = scanner->bindings[i - 1].python;
        break;
      }
    }
    lexer->mark_end(lexer);
    lexer->result_symbol = FOREIGN_PROVIDER;
    return true;
  }

  if (valid_symbols[FOREIGN_SCOPE_OPEN] || valid_symbols[FOREIGN_SCOPE_CLOSE]) {
    skip_ascii_ws(lexer);
    if (valid_symbols[FOREIGN_SCOPE_OPEN] && lexer->lookahead == '{') {
      lexer->advance(lexer, false);
      if (scanner->scope != UINT16_MAX) scanner->scope++;
      lexer->mark_end(lexer);
      lexer->result_symbol = FOREIGN_SCOPE_OPEN;
      return true;
    }
    if (valid_symbols[FOREIGN_SCOPE_CLOSE] && lexer->lookahead == '}') {
      lexer->advance(lexer, false);
      while (scanner->count && scanner->bindings[scanner->count - 1].scope == scanner->scope) scanner->count--;
      if (scanner->scope) scanner->scope--;
      lexer->mark_end(lexer);
      lexer->result_symbol = FOREIGN_SCOPE_CLOSE;
      return true;
    }
    return false;
  }

  if (valid_symbols[FOREIGN_GROUP_OPEN]) {
    skip_ascii_ws(lexer);
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

  if ((valid_symbols[FOREIGN_BODY] || valid_symbols[PYTHON_FOREIGN_BODY]) &&
      scan_foreign_body(lexer, scanner->python)) {
    lexer->result_symbol = scanner->python ? PYTHON_FOREIGN_BODY : FOREIGN_BODY;
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
  if (!wants_label && !wants_markup && !wants_convention) {
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
  bool saw_newline = false;
  for (;;) {
    int32_t c = lexer->lookahead;
    if (c == '\n') {
      saw_newline = true;
    } else if (c != ' ' && c != '\t' && c != '\r') {
      break;
    }
    lexer->advance(lexer, true);
  }

  // Labelled loops and markup expressions can both begin an expression. Test
  // the first non-extra byte once, then dispatch without starving markup when
  // the label token is also valid in the same parser state.
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
