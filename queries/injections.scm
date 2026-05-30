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
