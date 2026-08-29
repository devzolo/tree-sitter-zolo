; Tree-sitter highlights for Zolo
; Captures follow the standard nvim-treesitter / Helix capture conventions.

; -- Comments ---------------------------------------------------------------
(shebang) @comment
(line_comment) @comment
(block_comment) @comment
(doc_comment) @comment.documentation
(module_doc_comment) @comment.documentation

; -- Keywords ---------------------------------------------------------------
[
  "let"
  "mut"
  "const"
  "const_assert"
  "type"
  "newtype"
  "use"
  "mod"
  "pub"
  "where"
  "var"
  "override"
  "with"
  "using"
  "state"
  "initial"
  "comptime"
  "enable"
  "requires"
] @keyword

[
  "fn"
  "macro"
  "macro_rules"
] @keyword.function

[
  "struct"
  "enum"
  "trait"
  "impl"
  "schema"
  "machine"
  "effect"
] @keyword.type

[
  "if"
  "else"
  "match"
  "for"
  "in"
  "while"
  "loop"
  "break"
  "continue"
  "return"
  "handle"
  "perform"
  "select"
  "default"
] @keyword.control

; `parallel` (concurrency block, specs/shell-scripting.html §11) is
; deliberately NOT listed here, unlike `scope`/`spawn`: it is not a reserved
; word (std::effect declares a fn named `parallel`, so `parallel(fs)` must
; keep parsing as an ordinary call). grammar.js resolves the ambiguity by
; lexing `parallel {` as one token (mirroring `handler {` /
; `_handler_open`), and — like `_handler_open` — that hidden merged token
; produces no tree node at all (verified via `tree-sitter parse -x`), so
; there is nothing here to capture without also colorizing the following
; `{`/whitespace. Same reasoning applies to `handler` above.
[
  "async"
  "await"
  "yield"
  "spawn"
  "every"
  "after"
  "timeout"
  "sleep"
  "scope"
] @keyword.coroutine

[
  "try"
  "catch"
  "finally"
  "defer"
  "defer_ok"
  "defer_err"
  "guard"
] @keyword.exception

[
  "as"
  "is"
  ; `not` is an anonymous token only inside the two `is not` type-check
  ; rules (never a reserved word), so this is contextual by construction —
  ; a variable/function named `not` parses as identifier and stays unstyled.
  "not"
  "within"
  "relative"
  "ulps"
  "absolute"
] @keyword.operator

[
  "on"
  "shutdown"
  "panic"
  "signal"
  "boot"
  "worker"
] @keyword

"self" @variable.builtin

; -- Literals ---------------------------------------------------------------
(integer_literal) @number
(float_literal) @number.float
(decimal_literal) @number.float
(bigint_literal) @number
(duration_literal) @number
(bool_literal) @boolean
(nil_literal) @constant.builtin
(char_literal) @character

(string_literal) @string
(raw_string_literal) @string
(raw_triple_string_literal) @string
(triple_string_literal) @string
(fenced_string_literal) @string
(bytes_literal) @string
(regex_literal) @string.regex
; Generic tagged template `tag"...{expr}..."` (any identifier tag — sql, sh,
; json, html, ... all share this one grammar rule). Whole-node @string first
; so the body/quotes get a base color (mirrors `tagged_raw_string_literal`
; below); the `tag` field capture is a later pattern, so it wins and
; overrides just that sub-range per the "later patterns win" convention.
(tagged_string_literal) @string
(tagged_string_literal
  tag: (identifier) @function.macro)
(tagged_raw_string_literal) @string
(tagged_raw_string_literal
  tag: (identifier) @function.macro)
(string_interpolation
  "{" @punctuation.special
  "}" @punctuation.special)
(escape_sequence) @string.escape
(triple_brace_escape) @string.escape
(format_spec) @string.special

; Structured, token-backed quasiquotes. The raw generated source remains
; opaque; interpolation expressions keep their normal Zolo captures.
(quote_expression
  "quote" @keyword)
(quote_expression
  category: [
    "items" "item" "member" "param" "stmt" "expr" "type" "pattern" "ident" "match_arm"
  ] @type.builtin)
(quote_hole
  "${" @punctuation.special
  "}" @punctuation.special)

; -- Operators --------------------------------------------------------------
[
  "+" "-" "*" "/" "%" "**"
  "==" "!=" "<" ">" "<=" ">="
  "&&" "||" "!"
  "&" "|" "^" "~" "<<" ">>"
  "=" "+=" "-=" "*=" "/=" "%=" "??="
  "|>" "?>" "&." "->" "=>" "::"
  ".." "..=" "..."
  "?." "!." "??" "?" ".*"
  "~=" "!~=" ":=" "<-"
  "~/" "~/="
] @operator

; -- Punctuation ------------------------------------------------------------
[ "(" ")" "[" "]" "{" "}" "#{" ] @punctuation.bracket
[ "," ";" ":" "." "@" "$" ] @punctuation.delimiter

; -- Decorators -------------------------------------------------------------
(decorator
  "@" @attribute
  name: (identifier) @attribute)

; -- Items ------------------------------------------------------------------
(function_item name: (identifier) @function)
(macro_item name: (identifier) @function.macro)
(macro_rules_item name: (identifier) @function.macro)
(macro_fragment "$" @punctuation.special (identifier) @variable.parameter)
(trait_method name: (identifier) @function.method)

(struct_item name: (identifier) @type)
(enum_item name: (identifier) @type)
(trait_item name: (identifier) @type)
(type_alias name: (identifier) @type)
(newtype_item name: (identifier) @type)
(storage_class (identifier) @keyword.modifier)
(override_declaration name: (identifier) @variable)
(on_declaration hook: (identifier) @function.method)
(effect_item name: (identifier) @type)
(schema_item name: (identifier) @type)
(machine_item name: (identifier) @type)
(effect_signature name: (identifier) @function.method)
(machine_state_decl name: (identifier) @constant)
(machine_initial state: (identifier) @constant)
(machine_transition
  from: (identifier) @constant
  to: (identifier) @constant
  event: (identifier) @property)
(select_guard
  binding: (identifier) @variable)
(enum_variant name: (identifier) @constructor)
(field_declaration name: (identifier) @property)
(field_getter "get" @keyword.function)
(field_setter
  "set" @keyword.function
  parameter: (identifier) @variable.parameter)
(field_setter "set" @keyword.function)
(field_accessor_visibility "pub" @keyword)
(field_accessor_visibility ["crate" "super" "mod" "in"] @keyword)

(type_parameter name: (identifier) @type.parameter)
(const_item name: (identifier) @constant)
(associated_type name: (identifier) @type)
(directive_name (identifier) @constant)

; Type-level identifiers
(primitive_type) @type.builtin
(type_path (identifier) @type)
(generic_type name: (identifier) @type)
(function_type "fn" @keyword.function)
(function_type_parameter name: (identifier) @variable.parameter)
(optional_type "?" @operator)

; -- Calls ------------------------------------------------------------------
(call_expression
  function: (identifier) @function.call)

(call_expression
  function: (path_expression
    (identifier) @function.call .))

(optional_call_expression
  function: (identifier) @function.call)

(optional_call_expression
  function: (path_expression
    (identifier) @function.call .))

(method_call_expression
  method: (identifier) @function.method.call)

; Trailing lambda (C3): `f(a) { |x| … }` / `recv.m { |x| … }` — the
; `trailing_lambda` node's own braces/pipes fall through to the generic
; punctuation/operator token rules below, and its `parameter` children to the
; `(parameter name: (identifier) @variable.parameter)` rule further down; no
; dedicated capture is needed here, matching `lambda_expression` (which also
; has no bespoke rule of its own).

(macro_invocation
  macro: (identifier) @function.macro
  "!" @function.macro)

; `resume(v)` / `abort(e)` — reserved pseudo-calls in effect-handler arms.
; Later patterns win, so this overrides the generic @function.call above.
((call_expression
  function: (identifier) @keyword.control)
  (#any-of? @keyword.control "resume" "abort"))

; Named-argument labels: `f(name: v)` / `@test(timeout: 5s)`.
(call_argument
  name: (identifier) @variable.parameter)

; -- Fields & paths ---------------------------------------------------------
(field_expression
  field: (identifier) @property)
(optional_chain_expression
  field: (identifier) @property)
(force_chain_expression
  field: (identifier) @property)
(struct_expression_field
  name: (identifier) @property)
(struct_expression_field
  spread: (identifier) @variable)
(map_entry
  key: (identifier) @property)
(field_pattern
  name: (identifier) @property)
(struct_pattern
  rest: (identifier) @variable)
(anon_struct_pattern
  rest: (identifier) @variable)
(enum_pattern
  rest: (identifier) @variable)
(binding_pattern
  name: (identifier) @variable)
(record_type
  name: (identifier) @property)

(path_expression
  (identifier) @namespace
  (identifier) @constructor .)

; -- Use / Mod paths --------------------------------------------------------
(use_path (identifier) @namespace)
(use_item name: (identifier) @namespace)
(use_item alias: (identifier) @namespace)
(mod_path (identifier) @namespace)

; -- Parameters / Variables -------------------------------------------------
(parameter name: (identifier) @variable.parameter)
(variadic_parameter name: (identifier) @variable.parameter)
(self_parameter) @variable.builtin

(let_declaration
  pattern: (identifier) @variable)

(macro_param "$" @punctuation.special
  (identifier) @variable.parameter)

; -- Identifiers (fallback) -------------------------------------------------
(identifier) @variable

; Stored accessor identifiers are contextual and must override the generic
; identifier fallback above (later query patterns win). These shapes cover the
; canonical getter/setter bodies; deeper expressions remain normal variables
; rather than globally reserving the otherwise legal name `field`.
(field_setter parameter: (identifier) @variable.parameter)
((field_getter
  body: (block
    (return_statement
      (identifier) @variable.builtin)))
  (#eq? @variable.builtin "field"))
((field_setter
  body: (block
    (assignment_statement
      target: (identifier) @variable.builtin)))
  (#eq? @variable.builtin "field"))

; Loop labels are contextual. These patterns intentionally come after the
; generic identifier fallback because later patterns win in highlights.scm.
; The contextual colon scanner exposes this node only before loop forms, so
; type annotations, named arguments, and map entries remain untouched.
(loop_label
  ":" @punctuation.special
  name: (identifier) @label)

; -- Markup (Verniz V4b) ----------------------------------------------------
; Last in the file on purpose: tag and attribute names are `identifier`
; nodes, and the `(identifier) @variable` fallback above would otherwise
; claim them. Later patterns win.
(markup_open_tag name: (identifier) @tag)
(markup_close_tag name: (identifier) @tag)
(markup_self_closing_tag name: (identifier) @tag)
(markup_qualified_name
  module: (identifier) @namespace
  member: (identifier) @tag)

(markup_attribute name: (markup_attribute_name) @tag.attribute)
(markup_shorthand_attribute name: (identifier) @variable)
(markup_spread_attribute "..." @operator)

; Anchored inside the markup nodes so they never restyle the comparison
; operators `<` and `>`, which an unanchored list would.
(markup_open_tag ["<" ">"] @tag.delimiter)
(markup_close_tag ["</" ">"] @tag.delimiter)
(markup_self_closing_tag ["<" "/>"] @tag.delimiter)
(markup_fragment_open ["<" ">"] @tag.delimiter)
(markup_fragment_close ["</" ">"] @tag.delimiter)
(markup_attribute "=" @operator)

; `{ … }` is the escape back into Zolo — the braces are punctuation, and
; what is inside is ordinary code highlighted by every rule above.
(markup_interpolation ["{" "}"] @punctuation.special)
(markup_named_child_block ["{" "}"] @punctuation.special)
(markup_attribute_expression ["{" "}"] @punctuation.special)
(markup_shorthand_attribute ["{" "}"] @punctuation.special)
(markup_spread_attribute ["{" "}"] @punctuation.special)

; `@(expr)` — the CSS interpolation escape inside a `<style>` body
; (specs/verniz-css.html §6.4, grammar.js `css_interpolation`). Same
; "later pattern wins" override as `markup_interpolation` above: without
; this, the generic `@punctuation.delimiter`/`@punctuation.bracket` rules
; near the top of the file would claim the `@`/`(`/`)` instead. `expr`
; itself needs no rule of its own — every capture above already applies to
; it, the same way it does inside `markup_interpolation`.
(css_interpolation ["@" "(" ")"] @punctuation.special)

; `<!-- … -->` is the comment form in the CHILDREN position: `//` there is
; indistinguishable from a URL, so it stays text. Inside a TAG the opposite
; holds — `//` and `/* */` are ordinary Zolo comments there (they ride the
; `extras`, so they are real `line_comment`/`block_comment` nodes between the
; attributes) and the captures at the top of this file already paint them.
(markup_comment) @comment

; Literal text between tags carries no highlight of its own.
(markup_text) @none

; `<style>`/`<script>` body: CSS/JS, verbatim. Editors that resolve
; `injections.scm` render this as the injected language instead; this scope
; is the fallback for the ones that don't.
(markup_raw_text) @string
