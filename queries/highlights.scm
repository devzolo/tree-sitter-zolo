; Tree-sitter highlights for Zolo
; Captures follow the standard nvim-treesitter / Helix capture conventions.

; -- Comments ---------------------------------------------------------------
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
(triple_string_literal) @string
(fenced_string_literal) @string
(bytes_literal) @string
(regex_literal) @string.regex
(tagged_string_literal
  tag: (identifier) @function.macro)
(tagged_raw_string_literal) @string
(tagged_raw_string_literal
  tag: (identifier) @function.macro)
(string_interpolation
  "{" @punctuation.special
  "}" @punctuation.special)
(escape_sequence) @string.escape
(format_spec) @string.special

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

(type_parameter name: (identifier) @type.parameter)
(const_item name: (identifier) @constant)
(associated_type name: (identifier) @type)
(directive_name (identifier) @constant)

; Type-level identifiers
(primitive_type) @type.builtin
(type_path (identifier) @type)
(generic_type name: (identifier) @type)
(function_type "fn" @keyword.function)
(optional_type "?" @operator)

; -- Calls ------------------------------------------------------------------
(call_expression
  function: (identifier) @function.call)

(call_expression
  function: (path_expression
    (identifier) @function.call .))

(method_call_expression
  method: (identifier) @function.method.call)

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
