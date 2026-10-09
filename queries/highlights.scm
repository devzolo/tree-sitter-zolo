; Tree-sitter highlights for Zolo
; Captures follow the standard nvim-treesitter / Helix capture conventions.

; -- Comments ---------------------------------------------------------------
(shebang) @comment
(line_comment) @comment
(block_comment) @comment
(doc_comment) @comment.documentation
(module_doc_comment) @comment.documentation

; Generic identifiers precede contextual captures: later patterns win.
(identifier) @variable

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
  "extern"
  "from"
] @keyword

(extern_declaration provider: (identifier) @module)
(extern_expression provider: (identifier) @module)
(extern_function name: (identifier) @function)
; Expression function bodies use the ordinary expression captures.
(function_expression_body "=" @operator)
(extern_module name: (identifier) @module)
(extern_dependencies "deps" @keyword)
(extern_dependency package: (identifier) @property)
(extern_dependency_options option: _ @property)

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
  "as?"
  "unsafe"
  "bitcast"
  "is"
  ; `not` is an anonymous token only inside the two `is not` type-check
  ; rules (never a reserved word), so this is contextual by construction —
  ; a variable/function named `not` parses as identifier and stays unstyled.
  "not"
  "within"
  "relative"
  "ulps"
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
(numeric_base_prefix) @punctuation.special
(numeric_type_suffix) @type.builtin
(unit_magnitude) @number
(unit_suffix) @type
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
(tagged_triple_string_literal) @string
(tagged_triple_string_literal
  tag: (identifier) @function.macro)
((tagged_triple_string_literal
  tag: (identifier) @function.builtin)
 (#eq? @function.builtin "tw"))
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

; Relational chains reuse these operator captures on every link.
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

; `{expr=}`: the `=` of a self-documenting interpolation.
(self_documenting) @operator

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
(newtype_item "transparent" @keyword.modifier)
(newtype_deriving_clause "deriving" @keyword)
(newtype_deriving_clause (identifier) @type)
(storage_class (identifier) @keyword.modifier)
; `sink` before a parameter or argument name (linear types).
(convention) @keyword.modifier
(override_declaration name: (identifier) @variable)
(on_declaration hook: (identifier) @function.method)
(effect_item name: (identifier) @type)
(schema_item name: (identifier) @type)
(machine_item name: (identifier) @type)
(effect_signature name: (identifier) @function.method)
(machine_state_decl name: (identifier) @constant)
(machine_event_decl name: (identifier) @constructor)
(machine_state_decl "state" @keyword)
(machine_event_decl "event" @keyword)
(machine_initial "initial" @keyword)
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

; Explicit source-module roots. `crate` is special only as the first segment;
; every consecutive leading `super` segment walks one lexical parent.
((use_declaration
  path: (use_path
    . (identifier) @keyword.import))
  (#any-of? @keyword.import "crate" "super"))
((use_declaration
  path: (use_path
    (identifier) @_previous-import-root
    . (identifier) @keyword.import))
  (#eq? @_previous-import-root "super")
  (#eq? @keyword.import "super"))

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
(required_named_marker) @operator
(optional_type "?" @operator)

; Qualified references precede the more specific callee captures.
(path_expression
  (identifier) @namespace
  (identifier) @constructor .)

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
  variant: (identifier) @constructor)
(enum_pattern
  rest: (identifier) @variable)
(binding_pattern
  name: (identifier) @variable)
(record_type
  name: (identifier) @property)

; -- Use / Mod paths --------------------------------------------------------
(use_path (identifier) @namespace)
(use_item name: (identifier) @namespace)
(use_item alias: (identifier) @namespace)
(mod_path (identifier) @namespace)

; -- Parameters / Variables -------------------------------------------------
(parameter name: (identifier) @variable.parameter)
(variadic_parameter name: (identifier) @variable.parameter)
(self_parameter "self" @variable.builtin)

(let_declaration
  pattern: (identifier) @variable)

(macro_param "$" @punctuation.special
  (identifier) @variable.parameter)

; Shorthand labels are also value references in the caller's lexical scope.
; Keep the capture after the identifier fallback; explicit `name: value`
; continues to use the call_argument label's @variable.parameter capture.
(named_argument_shorthand name: (identifier) @variable)

; Explicit labels remain formal parameter names. This must follow the
; generic identifier fallback (later query patterns win).
(call_argument name: (identifier) @variable.parameter)

; The `_` receiver in a short-lambda projection is the implicit parameter.
; Keep these contextual captures after the generic fallback: a standalone
; wildcard/discard and underscore-prefixed names remain ordinary identifiers.
; Fields/methods keep their existing property/call captures.
((field_expression object: (identifier) @variable.parameter)
  (#eq? @variable.parameter "_"))
((method_call_expression receiver: (identifier) @variable.parameter)
  (#eq? @variable.parameter "_"))

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
; Tag and attribute names override the generic identifier fallback.
(markup_open_tag name: (identifier) @tag)
(markup_close_tag name: (identifier) @tag)
(markup_self_closing_tag name: (identifier) @tag)
(markup_open_tag name: (markup_custom_element_name) @tag)
(markup_close_tag name: (markup_custom_element_name) @tag)
(markup_self_closing_tag name: (markup_custom_element_name) @tag)
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

; Typed decorator values reuse normal expressions; capitalize type spellings
; without hardcoding any derive namespace or option name.
((decorator_arguments
  (call_argument value: (identifier) @type))
  (#match? @type "^[A-Z]"))
((decorator_arguments
  (call_argument value: (field_expression object: (identifier) @type)))
  (#match? @type "^[A-Z]"))
(enum_shorthand_expression variant: (identifier) @constructor)

; Attribute labels and referenced fields override the generic fallback.
(decorator_arguments (call_argument name: (identifier) @variable.parameter))
(decorator_arguments
  (call_argument value: (field_expression field: (identifier) @property)))

; Typed references can occur at arbitrary collection depth. Match leaf shapes
; rather than a fixed decorator -> map -> array path. As for direct decorator
; values, capitalization is a syntax-only heuristic; semantic tokens refine it.
((field_expression object: (identifier) @type)
  (#match? @type "^[A-Z]"))
(field_expression field: (identifier) @property)
((array_expression (identifier) @type)
  (#match? @type "^[A-Z]"))
((map_entry value: (identifier) @type)
  (#match? @type "^[A-Z]"))
; Map labels remain data even when a label looks like a type.
(map_entry key: (identifier) @property)

; Provider-qualified dependency entries override the generic identifier
; fallback. Ordinary variables/functions/constructors named `deps` keep it.
(dependencies_declaration "deps" @keyword)
(qualified_dependency provider: (identifier) @module)
(dependency_group provider: (identifier) @module)
(qualified_dependency dependency: (extern_dependency package: (identifier) @property))
(dependency_group (extern_dependency package: (identifier) @property))
(extern_dependency_options option: _ @property)

; Clause-local bindings use the same pattern scopes as standalone if-let.
; The separating commas remain ordinary punctuation, never argument labels.
(let_condition pattern: (identifier_pattern (identifier) @variable))
(let_condition "=" @operator)

; Immutable update keys are field references at every authored path segment.
(record_update_path (identifier) @property)

; Array-entry binders are authored lexical names, separate from iterable/value.
(array_for_entry binding: (identifier) @variable)
(array_if_entry "=>" @operator)
(array_for_entry "=>" @operator)

; Only the local Result form owns a control keyword.
(attempt_expression "attempt" @keyword.control)

; Contextual construction has no authored type identifier.
(inferred_struct_expression "." @punctuation.special)

; Exact authored labels of ordinary named callback blocks.
(named_trailing_lambda name: (identifier) @variable.parameter)

; Public labels and lexical parameter binders have separate authored tokens.
; These contextual captures follow the generic identifier fallback.
(external_parameter label: (identifier) @variable.parameter)
(external_parameter name: (identifier) @variable.parameter)

; Named parallel results are fields, not assignments to locals.
(parallel_result_entry name: (identifier) @property)

; Public labels on refutable clause patterns retain their own authored identity.
(function_clause_parameter label: (identifier) @variable.parameter)

; Typed capture binders are scoped pattern variables; types use normal type rules.
(string_capture_pattern "pat\"" @keyword)
(pattern_string_content) @string
(pattern_brace_escape) @string.escape
(string_pattern_capture name: (identifier) @variable.parameter)
(string_pattern_capture ["{" "}"] @punctuation.special)

; First-class capture names are public record slots; old patterns retain binders.
(capture_pattern_literal "pat\"" @keyword)
(capture_pattern_slot name: (property_identifier) @property)
(capture_pattern_slot ["{" "}"] @punctuation.special)

; Prefix handlers reuse the existing with keyword and real block delimiters.
(lexical_handle_expression "with" @keyword)

; Field is contextual; root type and fixed member identities have separate scopes.
(field_path_expression "field" @keyword)
(field_path_expression segment: (identifier) @property)

; Scalar and identity underscore expressions share the implicit-parameter
; presentation. Semantic services prove unary callback ownership; these
; expression contexts do not capture wildcard patterns or member names.
((call_argument (identifier) @variable.parameter)
  (#eq? @variable.parameter "_"))
((binary_expression left: (identifier) @variable.parameter)
  (#eq? @variable.parameter "_"))
((binary_expression right: (identifier) @variable.parameter)
  (#eq? @variable.parameter "_"))
((parenthesized_expression (identifier) @variable.parameter)
  (#eq? @variable.parameter "_"))

(recover_expression "recover" @keyword.control)
(recovery_arm pattern: (enum_pattern variant: (identifier) @constructor))

; Resource bindings retain their authored lexical identity.
(resource_scope_expression name: (identifier) @variable)

(map_for_entry binding: (identifier) @variable)
(map_if_entry "=>" @operator)
(map_for_entry "=>" @operator)
(map_builder_pair key: (identifier) @property)

; Pattern tests share authored is/operator and enum/pattern captures.
(is_pattern_expression "is" @keyword.operator)
(is_pattern_expression pattern: (enum_pattern variant: (identifier) @constructor))

; Filtering marker is contextual; ordinary case bindings remain identifiers.
(for_expression "case" @keyword.control)

; Projection names designate physical properties, never local puns.
(record_projection_body field: (property_identifier) @property)
(record_projection_body field: (property_identifier (identifier) @property))

; Whole-schema clauses retain ordinary callable/lambda scopes.
(schema_invariant_clause "where" @keyword)

; Typed handler override source scopes.
; Qualified patch selectors and formals retain their authored roles.
(handler_override_expression "with" @keyword)
(handler_override_effect namespace: (identifier) @namespace)
(handler_override_effect name: (identifier) @type)
(handler_override_selector operation: (identifier) @function.method)
(handler_override_parameter name: (identifier) @variable.parameter)
(handler_override_parameter pattern: (struct_pattern name: (identifier) @type))
(handler_override_parameter pattern: (tuple_pattern (identifier_pattern (identifier) @variable.parameter)))
(handler_override_parameter pattern: (array_pattern (identifier_pattern (identifier) @variable.parameter)))
(handler_override_parameter pattern: (array_pattern rest: (identifier) @variable.parameter))
(handler_override_parameter pattern: (struct_pattern (field_pattern name: (identifier) @variable.parameter !pattern)))
(handler_override_parameter pattern: (anon_struct_pattern (field_pattern name: (identifier) @variable.parameter !pattern)))
(handler_override_parameter pattern: (struct_pattern (field_pattern pattern: (identifier_pattern (identifier) @variable.parameter))))
(handler_override_parameter pattern: (anon_struct_pattern (field_pattern pattern: (identifier_pattern (identifier) @variable.parameter))))

; Only @unit type-reference/formula positions supply these lexical roles.
; Physical unit ownership and deeper formula types are supplied by semantic tokens.
((decorator
  name: (identifier) @attribute
  arguments: (decorator_arguments
    (call_argument
      name: (identifier) @variable.parameter
      value: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression left: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression left: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
] right: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
])
(parenthesized_expression [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
])
(unary_expression operand: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
])
] right: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression left: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
] right: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
])
(parenthesized_expression [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
])
(unary_expression operand: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
])
])
(parenthesized_expression [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression left: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
] right: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
])
(parenthesized_expression [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
])
(unary_expression operand: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
])
])
(unary_expression operand: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression left: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
] right: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
])
(parenthesized_expression [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
])
(unary_expression operand: [
(identifier) @type
(path_expression (identifier) @type .)
(integer_literal)
(float_literal)
(decimal_literal)
(bigint_literal)
(call_expression)
(method_call_expression)
(binary_expression)
(parenthesized_expression)
(unary_expression)
])
])
])))
  (#eq? @attribute "unit")
  (#any-of? @variable.parameter "of" "dimension"))

; Await deadlines install no names. Authored children retain existing captures.
; Contextual captures are last so they refine the generic keyword list.
(await_deadline_expression
  "await" @keyword.coroutine
  "within" @keyword.operator)

; Yield delegates only when its immediate value is Spread.
; Nested array/call Spread stays a scalar yield value with ordinary captures.
; Put this after generic patterns so captures remain contextual and stable.
(yield_expression
  "yield" @keyword.coroutine
  value: (spread_expression "..." @operator))

; Struct/enum suffix requests select derivers, never lexical variable reads.
; Legacy newtype_deriving_clause captures remain in the existing source query.
(deriving_clause "deriving" @keyword)
(deriving_clause (identifier) @type)

; Existing type-path captures color every segment as type. Refine declaration
; bound prefixes after them; final segment remains the actual type reference.
(type_parameter bound: (type_path (identifier) @namespace (identifier) @type .))
(where_predicate bounds: (type_path (identifier) @namespace (identifier) @type .))

; Authored titled test headers: titles are text, never function bindings.
(titled_test_declaration "test" @keyword.function)
; Literal aliases inherit the existing string/escape captures above.
; Do not append a whole-title capture after @string.escape.

; Dotted generic state markers are enum-state references, never variables.
; This fragment follows generic identifier/type captures (later patterns win).
(state_index_argument name: (identifier) @constant)
(state_index_argument "." @punctuation.special)
(syntax_category_type name: (identifier) @type)
(syntax_category_type name: (type_path) @type)
(syntax_category_argument name: (identifier) @type)
(syntax_category_argument "." @punctuation.special)
