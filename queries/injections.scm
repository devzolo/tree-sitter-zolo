; Injections for Zolo
;
; String interpolation: the {expr} part inside a string literal is itself
; Zolo source. tree-sitter already exposes the inner expression through the
; `string_interpolation` rule, so most consumers don't need an injection
; here — we keep one anyway so editors that re-tokenize from raw text get
; correct highlighting.

((string_interpolation) @injection.content
  (#set! injection.language "zolo")
  (#set! injection.include-children))

; -- Tagged template injections ---------------------------------------------
; Common tag names map to embedded languages. Editors that don't support a
; given language will simply fall back to plain string highlighting.

((tagged_string_literal
  tag: (identifier) @_tag) @injection.content
  (#eq? @_tag "sql")
  (#set! injection.language "sql"))

((tagged_string_literal
  tag: (identifier) @_tag) @injection.content
  (#eq? @_tag "html")
  (#set! injection.language "html"))

((tagged_string_literal
  tag: (identifier) @_tag) @injection.content
  (#eq? @_tag "css")
  (#set! injection.language "css"))

((tagged_string_literal
  tag: (identifier) @_tag) @injection.content
  (#eq? @_tag "json")
  (#set! injection.language "json"))

((tagged_string_literal
  tag: (identifier) @_tag) @injection.content
  (#eq? @_tag "regex")
  (#set! injection.language "regex"))

; Regex literal: re"pattern"
((regex_literal) @injection.content
  (#set! injection.language "regex"))

; -- Raw text elements -------------------------------------------------------
; The body of `<script>` is JS, not Zolo — a single `markup_raw_text` node,
; never interpolated (see grammar.js and TE139 in the compiler). `<style>`
; is CSS the same way, EXCEPT for a live `@(expr)` escape
; (specs/verniz-css.html §6.4): its body is `repeat($._style_body_part)`, so
; a style element can hold SEVERAL `markup_raw_text` chunks (one per gap
; between interpolations, possibly zero-width — see `css_interpolation`
; below) rather than the single chunk `<script>` always gets.
; `injection.combined` stitches those chunks into one logical CSS document
; instead of N unrelated ones, so e.g. a rule's `{ … }` that happens to
; straddle an interpolation still reads as balanced CSS.

((markup_element
   open_tag: (markup_open_tag name: (identifier) @_tag)
   (markup_raw_text) @injection.content)
 (#eq? @_tag "style")
 (#set! injection.language "css")
 (#set! injection.combined))

((markup_element
   open_tag: (markup_open_tag name: (identifier) @_tag) @_open
   (markup_raw_text) @injection.content)
 (#eq? @_tag "script")
 (#not-match? @_open "^<script[ \\t\\r\\n]+client(?:[ \\t\\r\\n]*=[ \\t\\r\\n]*true)?[ \\t\\r\\n]+lang[ \\t\\r\\n]*=[ \\t\\r\\n]*[\"'](?:ts|typescript)[\"'][ \\t\\r\\n]*>$")
 (#not-match? @_open "^<script[ \\t\\r\\n]+lang[ \\t\\r\\n]*=[ \\t\\r\\n]*[\"'](?:ts|typescript)[\"'][ \\t\\r\\n]+client(?:[ \\t\\r\\n]*=[ \\t\\r\\n]*true)?[ \\t\\r\\n]*>$")
 (#set! injection.language "javascript"))

((markup_element
   open_tag: (markup_open_tag name: (identifier) @_tag) @_open
   (markup_raw_text) @injection.content)
 (#eq? @_tag "script")
 (#match? @_open "^<script[ \\t\\r\\n]+client(?:[ \\t\\r\\n]*=[ \\t\\r\\n]*true)?[ \\t\\r\\n]+lang[ \\t\\r\\n]*=[ \\t\\r\\n]*[\"'](?:ts|typescript)[\"'][ \\t\\r\\n]*>$")
 (#set! injection.language "typescript"))

((markup_element
   open_tag: (markup_open_tag name: (identifier) @_tag) @_open
   (markup_raw_text) @injection.content)
 (#eq? @_tag "script")
 (#match? @_open "^<script[ \\t\\r\\n]+lang[ \\t\\r\\n]*=[ \\t\\r\\n]*[\"'](?:ts|typescript)[\"'][ \\t\\r\\n]+client(?:[ \\t\\r\\n]*=[ \\t\\r\\n]*true)?[ \\t\\r\\n]*>$")
 (#set! injection.language "typescript"))

; `@(expr)` itself: `expr`, not the whole node — `@(`/`)` are not valid
; standalone Zolo on their own (unlike `{expr}` above, which IS a valid
; `block_expression` when reparsed whole — that is why `string_interpolation`
; injects the full node and this does not).
((css_interpolation
   expression: (_) @injection.content)
 (#set! injection.language "zolo")
 (#set! injection.include-children))
