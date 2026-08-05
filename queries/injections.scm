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
; The body of `<style>`/`<script>` is CSS/JS, not Zolo — it is a single
; `markup_raw_text` node, never interpolated (see grammar.js and TE139 in the
; compiler).

((markup_element
   open_tag: (markup_open_tag name: (identifier) @_tag)
   (markup_raw_text) @injection.content)
 (#eq? @_tag "style")
 (#set! injection.language "css"))

((markup_element
   open_tag: (markup_open_tag name: (identifier) @_tag)
   (markup_raw_text) @injection.content)
 (#eq? @_tag "script")
 (#set! injection.language "javascript"))
