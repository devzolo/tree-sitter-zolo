// External scanner for Zolo — three tokens: `_markup_lt`, `_style_raw_text`,
// `_script_raw_text`.
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
#include <wctype.h>

enum TokenType {
  MARKUP_LT,
  STYLE_RAW_TEXT,
  SCRIPT_RAW_TEXT,
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

void *tree_sitter_zolo_external_scanner_create(void) { return NULL; }

void tree_sitter_zolo_external_scanner_destroy(void *payload) { (void)payload; }

unsigned tree_sitter_zolo_external_scanner_serialize(void *payload,
                                                     char *buffer) {
  (void)payload;
  (void)buffer;
  return 0;
}

void tree_sitter_zolo_external_scanner_deserialize(void *payload,
                                                   const char *buffer,
                                                   unsigned length) {
  (void)payload;
  (void)buffer;
  (void)length;
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

bool tree_sitter_zolo_external_scanner_scan(void *payload, TSLexer *lexer,
                                            const bool *valid_symbols) {
  (void)payload;

  // See the comment on `ERROR_SENTINEL`: this is true ONLY while tree-sitter
  // is in error recovery, probing every external token regardless of
  // whether the grammar actually expects it here. Bailing before either
  // raw-text scan is the fix — without it, `scan_raw_text` ran at any error
  // position and consumed to the next `</style`/`</script` or EOF.
  if (valid_symbols[ERROR_SENTINEL]) {
    return false;
  }

  if (valid_symbols[STYLE_RAW_TEXT] && scan_style_raw_text(lexer, "/style")) {
    lexer->result_symbol = STYLE_RAW_TEXT;
    return true;
  }
  if (valid_symbols[SCRIPT_RAW_TEXT] && scan_raw_text(lexer, "/script")) {
    lexer->result_symbol = SCRIPT_RAW_TEXT;
    return true;
  }

  if (!valid_symbols[MARKUP_LT]) {
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

  if (!saw_newline || lexer->lookahead != '<') {
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
