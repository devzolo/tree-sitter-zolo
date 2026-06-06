/**
 * @file Tree-sitter grammar for the Zolo programming language.
 * @author Zolo Lang authors
 * @license MIT
 *
 * Zolo is a typed, modern language compiling to Lua 5.1 bytecode.
 * Syntax inspired by Rust + Swift. See https://zolo-lang.dev
 *
 * Grammar covers ~80% of common features. Some advanced macros, comptime,
 * and edge-case interpolation paths are stubbed for follow-up.
 */

/* eslint-disable arrow-parens */
/* eslint-disable camelcase */
/* eslint-disable-next-line spaced-comment */
/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// -- Operator precedence --------------------------------------------------
// Mirrors crates/zolo-parser/src/parser.rs `infix_binding_power`.
// Higher number = binds tighter.
const PREC = {
  pipe: 1,           // |>  &.
  null_coalesce: 3,  // ??
  or: 5,             // ||
  and: 7,            // &&
  equality: 9,       // == !=
  in: 11,            // in
  comparison: 11,    // < > <= >=
  bit_or: 13,        // |
  bit_xor: 15,       // ^
  bit_and: 17,       // &
  range: 19,         // .. ..=
  shift: 21,         // << >>
  additive: 23,      // + -
  multiplicative: 25,// * / %
  unary: 27,         // -x !x ~x
  power: 28,         // ** (right-assoc)
  cast: 29,          // as / is
  try: 30,           // postfix ?
  call: 31,          // f(x)  obj.field  obj[i]
  primary: 32,
};

module.exports = grammar({
  name: 'zolo',

  extras: $ => [
    /\s+/,
    $.line_comment,
    $.block_comment,
    $.doc_comment,
    $.module_doc_comment,
  ],

  word: $ => $.identifier,

  conflicts: $ => [
    // Struct literal vs block expression after identifier
    [$._expression, $._pattern],
    // Tuple type vs parenthesized type
    [$.tuple_type, $.parenthesized_type],
    // Struct literal in `if cond { ... }` is ambiguous; we disambiguate via prec
    [$.path_expression, $.type_path],
    // `let x` — prefer plain identifier over identifier_pattern wrapper
    [$.let_declaration, $.identifier_pattern],
    // `let Foo(` — decide between bare path and enum_pattern with args
    [$.enum_pattern],
    // `spawn { ... }` — block as direct arg vs as block_expression
    [$.block_expression, $.spawn_expression],
    // `every { ... }` — block as interval-expr vs as body
    [$.block_expression, $.every_expression],
    // `struct Foo(...)` — tuple-struct vs struct with body
    [$.struct_item],
    // `let (a, ...)` — tuple_pattern_binding vs tuple_pattern via _pattern
    [$.tuple_pattern_binding, $.identifier_pattern],
    // `expr.name(...)` — field access followed by call vs method call
    [$.field_expression, $.method_call_expression],
    // `type X = T | U` — extend union_type vs end of type_alias
    [$.type_alias, $.union_type],
    // `let x: T |` — extend annotated type with union vs end of let
    [$.let_declaration, $.union_type],
    // `|x: T|` — lambda parameter end vs union_type continuation
    [$.parameter, $.union_type],
    // `fn(...) -> T |` — function_type return vs union_type continuation
    [$.function_type, $.union_type],
    // `|| -> T| name` — parameter (untyped) vs type_path
    [$.parameter, $.type_path],
    // `Name { ... }` struct literal vs a control-flow head followed by a block
    // (`match x { ... }`, `if x { ... }`); GLR keeps both and prunes the invalid.
    [$._expression, $.struct_expression],
    // Struct-free scrutinee postfix: field access vs method call (mirrors the
    // `[field_expression, method_call_expression]` conflict above).
    [$._scrutinee_field, $._scrutinee_method_call],
  ],

  inline: $ => [
    $._top_level_item,
  ],

  supertypes: $ => [
    $._expression,
    $._statement,
    $._item,
    $._pattern,
    $._type,
    $._literal,
  ],

  rules: {
    // ---------------------------------------------------------------------
    // Source file
    // ---------------------------------------------------------------------
    source_file: $ => repeat($._top_level_item),

    _top_level_item: $ => choice(
      $._item,
      $._statement,
    ),

    // ---------------------------------------------------------------------
    // Comments
    // ---------------------------------------------------------------------
    line_comment: _ => token(seq('//', /[^\n]*/)),
    block_comment: _ => token(seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/')),
    doc_comment: _ => token(seq('///', /[^\n]*/)),
    module_doc_comment: _ => token(seq('//!', /[^\n]*/)),

    // ---------------------------------------------------------------------
    // Items
    // ---------------------------------------------------------------------
    _item: $ => choice(
      $.function_item,
      $.struct_item,
      $.enum_item,
      $.trait_item,
      $.impl_item,
      $.use_declaration,
      $.mod_declaration,
      $.type_alias,
      $.newtype_item,
      $.const_item,
      $.const_assert_item,
      $.macro_item,
      $.on_declaration,
      $.effect_item,
      $.schema_item,
      $.machine_item,
      $.override_declaration,
      $.macro_rules_item,
    ),

    // -- Decorators -------------------------------------------------------
    decorator: $ => seq(
      '@',
      field('name', $.identifier),
      optional(field('arguments', $.decorator_arguments)),
    ),

    decorator_arguments: $ => seq(
      '(',
      optional(commaSep1($.call_argument)),
      optional(','),
      ')',
    ),

    // -- Function ---------------------------------------------------------
    function_item: $ => seq(
      repeat($.decorator),
      optional('pub'),
      // Function modifiers are collapsed into a single `repeat(choice(...))`
      // rather than a stack of separate `optional(...)`s ON PURPOSE. With four
      // independent optionals here, the older tree-sitter table builder
      // (cli 0.22.x) multiplied LR item sets across the whole recursive,
      // type-heavy tail that follows (`with_clause`, the return type and
      // `where_clause`): parser-table generation ballooned past 15 GB and was
      // OOM-killed. tree-sitter 0.26+ (see package.json) handles either form,
      // but keeping the modifiers in one node holds generation memory in check
      // across CLI versions. Do NOT split these back into individual optionals.
      repeat(choice('override', 'multi', 'async')),
      'fn',
      optional(field('generator', '*')),
      field('name', $.identifier),
      optional(field('type_parameters', $.type_parameters)),
      field('parameters', $.parameter_list),
      optional(field('with_clause', $.with_clause)),
      optional(seq('->', field('return_type', $._type))),
      optional(field('where_clause', $.where_clause)),
      choice(
        field('body', $.block),
        ';',
      ),
    ),

    type_parameters: $ => seq(
      '<',
      commaSep1($.type_parameter),
      optional(','),
      '>',
    ),

    type_parameter: $ => seq(
      field('name', $.identifier),
      optional(seq(':', field('bound', $._type_bound))),
    ),

    _type_bound: $ => sep1($._type, '+'),

    where_clause: $ => seq(
      'where',
      commaSep1($.where_predicate),
    ),

    where_predicate: $ => seq(
      field('type', $._type),
      ':',
      field('bounds', $._type_bound),
    ),

    parameter_list: $ => seq(
      '(',
      optional(seq(
        commaSep1(choice($.parameter, $.self_parameter, $.variadic_parameter)),
        optional(','),
      )),
      ')',
    ),

    self_parameter: _ => 'self',

    parameter: $ => seq(
      repeat($.decorator),
      field('name', $.identifier),
      optional(seq(':', field('type', $._type))),
      optional(seq('=', field('default', $._expression))),
    ),

    variadic_parameter: $ => seq(
      '...',
      field('name', $.identifier),
      optional(seq(':', field('type', $._type))),
    ),

    // -- Struct -----------------------------------------------------------
    struct_item: $ => seq(
      repeat($.decorator),
      optional('pub'),
      'struct',
      field('name', $.identifier),
      optional(field('type_parameters', $.type_parameters)),
      choice(
        // Compact form: struct Foo(...) { methods }
        seq(
          field('compact_fields', $.compact_field_list),
          optional(field('body', $.declaration_body)),
        ),
        // Regular form
        field('body', $.field_declaration_list),
        ';',
      ),
    ),

    field_declaration_list: $ => seq(
      '{',
      optional(seq(
        commaSep1($.field_declaration),
        optional(','),
      )),
      '}',
    ),

    field_declaration: $ => seq(
      repeat($.decorator),
      optional('pub'),
      field('name', $.identifier),
      ':',
      field('type', $._type),
      optional(seq('=', field('default', $._expression))),
      optional(seq('where', field('constraint', $._expression))),
    ),

    compact_field_list: $ => seq(
      '(',
      optional(seq(
        commaSep1($.field_declaration),
        optional(','),
      )),
      ')',
    ),

    // body of items that may contain methods (after compact struct)
    declaration_body: $ => seq(
      '{',
      repeat(choice($.function_item, $.const_item)),
      '}',
    ),

    // -- Enum -------------------------------------------------------------
    enum_item: $ => seq(
      repeat($.decorator),
      optional('pub'),
      'enum',
      field('name', $.identifier),
      optional(field('type_parameters', $.type_parameters)),
      field('body', choice($.enum_variant_list, $.enum_compact_list)),
    ),

    enum_compact_list: $ => seq(
      '(',
      optional(seq(commaSep1($.enum_variant), optional(','))),
      ')',
    ),

    enum_variant_list: $ => seq(
      '{',
      optional(seq(
        commaSep1($.enum_variant),
        optional(','),
      )),
      '}',
    ),

    enum_variant: $ => seq(
      field('name', $.identifier),
      optional(choice(
        field('tuple', $.enum_variant_tuple),
        field('struct', $.field_declaration_list),
      )),
    ),

    enum_variant_tuple: $ => seq(
      '(',
      optional(seq(commaSep1($._type), optional(','))),
      ')',
    ),

    // -- Trait ------------------------------------------------------------
    trait_item: $ => seq(
      repeat($.decorator),
      optional('pub'),
      'trait',
      field('name', $.identifier),
      optional(field('type_parameters', $.type_parameters)),
      field('body', $.trait_body),
    ),

    trait_body: $ => seq(
      '{',
      repeat(choice(
        $.associated_type,
        $.trait_method,
      )),
      '}',
    ),

    associated_type: $ => seq(
      'type',
      field('name', $.identifier),
      optional(seq('=', field('default', $._type))),
      optional(';'),
    ),

    trait_method: $ => seq(
      repeat($.decorator),
      'fn',
      field('name', $.identifier),
      field('parameters', $.parameter_list),
      optional(seq('->', field('return_type', $._type))),
      choice(
        field('body', $.block),
        optional(';'),
      ),
    ),

    // -- Effect -----------------------------------------------------------
    effect_item: $ => seq(
      repeat($.decorator),
      optional('pub'),
      'effect',
      field('name', $.identifier),
      optional(field('type_parameters', $.type_parameters)),
      field('body', $.effect_body),
    ),

    effect_body: $ => seq(
      '{',
      repeat($.effect_signature),
      '}',
    ),

    effect_signature: $ => seq(
      optional('multi'),
      'fn',
      field('name', $.identifier),
      field('parameters', $.parameter_list),
      optional(seq('->', field('return_type', $._type))),
      optional(';'),
    ),

    // -- Schema -----------------------------------------------------------
    schema_item: $ => seq(
      repeat($.decorator),
      optional('pub'),
      'schema',
      field('name', $.identifier),
      optional(field('type_parameters', $.type_parameters)),
      field('body', $.field_declaration_list),
    ),

    // -- State machine ----------------------------------------------------
    machine_item: $ => seq(
      repeat($.decorator),
      optional('pub'),
      'machine',
      field('name', $.identifier),
      optional(field('type_parameters', $.type_parameters)),
      field('body', $.machine_body),
    ),

    machine_body: $ => seq('{', repeat($._machine_member), '}'),

    _machine_member: $ => choice(
      $.machine_state_decl,
      $.machine_initial,
      $.machine_transition,
    ),

    machine_state_decl: $ => seq('state', commaSep1(field('name', $.identifier))),

    machine_initial: $ => seq('initial', field('state', $.identifier)),

    machine_transition: $ => seq(
      field('from', $.identifier),
      '->',
      field('to', $.identifier),
      optional(seq('on', field('event', $.identifier))),
      optional(seq('after', field('delay', $._expression))),
      optional(field('action', $.block)),
    ),

    // -- Impl -------------------------------------------------------------
    impl_item: $ => seq(
      'impl',
      optional(field('type_parameters', $.type_parameters)),
      // [trait for] type
      choice(
        seq(
          field('trait', $._type),
          'for',
          field('type', $._type),
        ),
        field('type', $._type),
      ),
      field('body', $.impl_body),
    ),

    impl_body: $ => seq(
      '{',
      repeat(choice(
        $.function_item,
        $.associated_type,
        $.const_item,
      )),
      '}',
    ),

    // -- Use & Mod --------------------------------------------------------
    use_declaration: $ => seq(
      optional('pub'),
      'use',
      optional($._use_plugin_kw),
      field('path', $.use_path),
      optional(';'),
    ),

    _use_plugin_kw: _ => token(seq('plugin', /[ \t\r\n]+/)),

    use_path: $ => seq(
      $.identifier,
      repeat(seq('::', $.identifier)),
      optional(seq('::', choice(
        '*',
        $.use_list,
      ))),
    ),

    use_list: $ => seq(
      '{',
      commaSep1($.use_item),
      optional(','),
      '}',
    ),

    use_item: $ => seq(
      field('name', $.identifier),
      optional(seq('as', field('alias', $.identifier))),
    ),

    mod_declaration: $ => prec.right(seq(
      repeat($.decorator),
      'mod',
      field('path', $.mod_path),
      optional(choice(';', field('body', $.mod_body))),
    )),

    mod_body: $ => seq('{', repeat($._item), '}'),

    mod_path: $ => sep1($.identifier, '::'),

    // -- Type alias / Newtype --------------------------------------------
    type_alias: $ => seq(
      optional('pub'),
      'type',
      field('name', $.identifier),
      optional(field('type_parameters', $.type_parameters)),
      '=',
      field('type', choice($.intersection_type, $._type)),
      optional(';'),
    ),

    newtype_item: $ => seq(
      repeat($.decorator),
      optional('pub'),
      'newtype',
      field('name', $.identifier),
      '(',
      field('inner', $._type),
      ')',
      optional(';'),
    ),

    // -- Const ------------------------------------------------------------
    const_item: $ => seq(
      optional('pub'),
      'const',
      field('name', $.identifier),
      optional(seq(':', field('type', $._type))),
      '=',
      field('value', $._expression),
      optional(';'),
    ),

    // -- Const assert -----------------------------------------------------
    // `const_assert <expr> [, "msg"]` — compile-time assertion.
    // See `specs/const-assert.md`.
    const_assert_item: $ => seq(
      'const_assert',
      field('condition', $._expression),
      optional(seq(',', field('message', $.string_literal))),
      optional(';'),
    ),

    // -- Macro ------------------------------------------------------------
    macro_item: $ => seq(
      'macro',
      field('name', $.identifier),
      optional(field('parameters', $.macro_parameters)),
      field('body', $.macro_body),
    ),

    macro_parameters: $ => seq(
      '(',
      optional(commaSep1($.identifier)),
      optional(','),
      ')',
    ),

    // We try to parse the body as a block; in practice this allows either an
    // expression-shaped body or a sequence of statements. Parameters appear
    // as `$x` placeholders inside expressions.
    macro_body: $ => $.block,

    // -- Lifecycle hooks --------------------------------------------------
    on_declaration: $ => seq(
      'on',
      field('event', choice(
        seq('shutdown', optional(field('bind', $.identifier))),
        seq('panic', optional(field('bind', $.identifier))),
        seq('signal',
          commaSep1(field('signal', $.identifier)),
          optional(field('bind', $.identifier)),
        ),
        field('hook', $.identifier),
      )),
      field('body', $.block),
    ),

    // ---------------------------------------------------------------------
    // Statements
    // ---------------------------------------------------------------------
    _statement: $ => choice(
      $.let_declaration,
      $.assignment_statement,
      $.return_statement,
      $.break_statement,
      $.continue_statement,
      $.defer_statement,
      $.expression_statement,
    ),

    let_declaration: $ => seq(
      optional('pub'),
      choice('let', 'var'),
      optional(field('storage', $.storage_class)),
      optional('mut'),
      field('pattern', choice(
        $.identifier,
        $.tuple_pattern_binding,
        $.native_multi_binding,
        $._pattern,
      )),
      optional(seq(':', field('type', $._type))),
      optional(seq('=', field('value', $._expression))),
      optional(seq('else', field('else_block', $.block))),
      optional(';'),
    ),

    // storage class: let<lazy> / var<atomic, persistent>
    storage_class: $ => seq('<', commaSep1($.identifier), '>'),

    // let (a, b, c) = expr
    tuple_pattern_binding: $ => seq(
      '(',
      commaSep1($.identifier),
      optional(','),
      ')',
    ),

    // let a, b = expr  (Lua-style multi-return, no parens)
    native_multi_binding: $ => prec.dynamic(1, seq(
      $.identifier,
      ',',
      commaSep1($.identifier),
    )),

    assignment_statement: $ => prec.right(seq(
      field('target', $._expression),
      field('operator', choice(
        '=', '+=', '-=', '*=', '/=', '~/=', '%=', '??=',
      )),
      field('value', $._expression),
      optional(';'),
    )),

    return_statement: $ => prec.right(seq(
      'return',
      optional(commaSep1($._expression)),
      optional(';'),
    )),

    break_statement: $ => prec.right(seq(
      'break',
      optional($._expression),
      optional(';'),
    )),

    continue_statement: _ => prec.right(seq('continue', optional(';'))),

    defer_statement: $ => seq(
      choice('defer', 'defer_ok', 'defer_err'),
      field('value', $._expression),
      optional(';'),
    ),

    expression_statement: $ => prec(-1, seq(
      $._expression,
      optional(';'),
    )),

    // ---------------------------------------------------------------------
    // Expressions
    // ---------------------------------------------------------------------
    _expression: $ => choice(
      $._literal,
      $.identifier,
      $.self_expression,
      $.path_expression,
      $.unary_expression,
      $.binary_expression,
      $.approx_expression,
      $.pipe_expression,
      $.range_expression,
      $.spread_expression,
      $.call_expression,
      $.method_call_expression,
      $.field_expression,
      $.index_expression,
      $.optional_chain_expression,
      $.try_expression,
      $.iter_expression,
      $.cast_expression,
      $.type_check_expression,
      $.parenthesized_expression,
      $.tuple_expression,
      $.array_expression,
      $.map_expression,
      $.struct_expression,
      $.if_expression,
      $.if_let_expression,
      $.match_expression,
      $.block_expression,
      $.lambda_expression,
      $.for_expression,
      $.while_expression,
      $.while_let_expression,
      $.loop_expression,
      $.try_catch_expression,
      $.await_expression,
      $.yield_expression,
      $.spawn_expression,
      $.scope_expression,
      $.select_expression,
      $.perform_expression,
      $.handle_expression,
      $.handler_expression,
      $.every_expression,
      $.after_expression,
      $.timeout_expression,
      $.sleep_expression,
      $.macro_invocation,
      $.macro_param,
    ),

    self_expression: _ => 'self',

    // -- Path: Foo::Bar  Foo::Bar::Baz -----------------------------------
    path_expression: $ => prec(PREC.primary, seq(
      $.identifier,
      repeat1(seq('::', $.identifier)),
    )),

    // -- Unary ------------------------------------------------------------
    unary_expression: $ => prec(PREC.unary, seq(
      field('operator', choice('-', '!', '~')),
      field('operand', $._expression),
    )),

    // -- Binary -----------------------------------------------------------
    binary_expression: $ => {
      const ops = [
        ['||', PREC.or, 'left'],
        ['&&', PREC.and, 'left'],
        ['==', PREC.equality, 'left'],
        ['!=', PREC.equality, 'left'],
        ['<', PREC.comparison, 'left'],
        ['>', PREC.comparison, 'left'],
        ['<=', PREC.comparison, 'left'],
        ['>=', PREC.comparison, 'left'],
        ['in', PREC.in, 'left'],
        ['|', PREC.bit_or, 'left'],
        ['^', PREC.bit_xor, 'left'],
        ['&', PREC.bit_and, 'left'],
        ['<<', PREC.shift, 'left'],
        ['>>', PREC.shift, 'left'],
        ['+', PREC.additive, 'left'],
        ['-', PREC.additive, 'left'],
        ['*', PREC.multiplicative, 'left'],
        ['/', PREC.multiplicative, 'left'],
        ['~/', PREC.multiplicative, 'left'],
        ['%', PREC.multiplicative, 'left'],
        ['**', PREC.power, 'right'],
        ['??', PREC.null_coalesce, 'left'],
      ];
      return choice(...ops.map(([op, p, assoc]) => {
        const fn = assoc === 'right' ? prec.right : prec.left;
        return fn(p, seq(
          field('left', $._expression),
          field('operator', op),
          field('right', $._expression),
        ));
      }));
    },

    // -- Range ------------------------------------------------------------
    range_expression: $ => prec.left(PREC.range, choice(
      seq($._expression, choice('..', '..='), $._expression),
      seq($._expression, choice('..', '..=')),
      seq(choice('..', '..='), $._expression),
      '..',
    )),

    // approximate equality: a ~= b  /  a ~= b within 0.001  /  a !~= b ulps 4
    approx_expression: $ => prec.left(PREC.equality, seq(
      field('left', $._expression),
      field('operator', choice('~=', '!~=')),
      field('right', $._expression),
      optional(seq(
        'within',
        field('tolerance', $._expression),
        optional(field('mode', choice('relative', 'ulps', 'absolute'))),
      )),
    )),

    // pipe: x |> f(...)  /  x |> .map(g)  (elided receiver)  /  x &. tap
    pipe_expression: $ => prec.left(PREC.pipe, seq(
      field('left', $._expression),
      field('operator', choice('|>', '&.')),
      field('right', choice($._pipe_method, $._expression)),
    )),

    _pipe_method: $ => prec(PREC.call, seq(
      '.',
      field('method', $._method_name),
      field('arguments', $.argument_list),
    )),

    spread_expression: $ => prec.right(seq('...', $._expression)),

    // -- Call -------------------------------------------------------------
    call_expression: $ => prec(PREC.call, seq(
      field('function', $._expression),
      field('arguments', $.argument_list),
    )),

    argument_list: $ => seq(
      '(',
      optional(seq(commaSep1($.call_argument), optional(','))),
      ')',
    ),

    call_argument: $ => choice(
      // Named argument: name: value
      seq(
        field('name', $.identifier),
        ':',
        field('value', $._expression),
      ),
      $._expression,
    ),

    method_call_expression: $ => prec(PREC.call, seq(
      field('receiver', $._expression),
      '.',
      field('method', $._method_name),
      field('arguments', $.argument_list),
    )),

    field_expression: $ => prec(PREC.call, seq(
      field('object', $._expression),
      '.',
      field('field', $._method_name),
    )),

    // Method/field name can be an identifier or any keyword (per parser). The
    // keyword spellings are aliased to `identifier` so a keyword used in member
    // position (`f.type`, `x?.match`, `d.match()`) parses to an `identifier`
    // node instead of a bare keyword token. Highlight queries then style it as
    // a property/method, not a keyword — matching the VS Code tmLanguage grammar
    // (which uses a `(?<!\.)` lookbehind) and the web highlighters.
    _method_name: $ => choice(
      $.identifier,
      alias(choice(
        'let', 'mut', 'const', 'fn', 'return', 'if', 'else', 'for', 'while',
        'loop', 'break', 'continue', 'match', 'enum', 'struct', 'impl', 'trait',
        'mod', 'use', 'pub', 'in', 'as', 'is', 'where', 'nil', 'self', 'type',
        'newtype', 'comptime', 'async', 'await', 'yield', 'spawn', 'every',
        'after', 'timeout', 'sleep', 'try', 'catch', 'finally', 'defer',
        'guard', 'macro', 'on', 'var', 'override', 'defer_ok', 'defer_err',
        'plugin', 'effect', 'handle', 'perform', 'scope', 'select',
        'schema', 'machine', 'state', 'initial',
      ), $.identifier),
    ),

    index_expression: $ => prec(PREC.call, seq(
      field('object', $._expression),
      '[',
      field('index', $._expression),
      ']',
    )),

    optional_chain_expression: $ => prec(PREC.call, seq(
      field('object', $._expression),
      '?.',
      field('field', $._method_name),
    )),

    try_expression: $ => prec(PREC.try, seq(
      field('value', $._expression),
      '?',
    )),

    iter_expression: $ => prec(PREC.call, seq(
      field('value', $._expression),
      '.*',
    )),

    cast_expression: $ => prec.left(PREC.cast, seq(
      field('value', $._expression),
      'as',
      field('type', $._type),
    )),

    type_check_expression: $ => prec.left(PREC.cast, seq(
      field('value', $._expression),
      'is',
      optional('not'),
      field('type', $._type),
    )),

    // -- Grouping / Tuples / Arrays / Maps -------------------------------
    parenthesized_expression: $ => seq('(', $._expression, ')'),

    tuple_expression: $ => seq(
      '(',
      $._expression,
      ',',
      optional(seq(commaSep1($._expression), optional(','))),
      ')',
    ),

    array_expression: $ => seq(
      '[',
      optional(seq(commaSep1($._expression), optional(','))),
      ']',
    ),

    // Zolo map literal: #{ key: value, shorthand, ...spread }
    map_expression: $ => prec(1, seq(
      '#{',
      optional(seq(commaSep1($.map_entry), optional(','))),
      '}',
    )),

    map_entry: $ => choice(
      seq(
        field('key', choice(
          $.string_literal,
          $.integer_literal,
          $.identifier,
          seq('[', $._expression, ']'),
        )),
        ':',
        field('value', $._expression),
      ),
      field('shorthand', $.identifier),
      $.spread_expression,
    ),

    // -- Struct literal ---------------------------------------------------
    // `Name { ... }` is ambiguous with a control-flow head followed by a block
    // (e.g. `match x { ... }`, `if x { ... }`). We keep both parses alive via a
    // declared conflict and let the GLR parser prune the invalid one; a positive
    // dynamic precedence makes the struct win genuine ties (value positions like
    // `let p = Point { x: 1 }`).
    struct_expression: $ => prec.dynamic(1, seq(
      field('name', choice($.identifier, $.path_expression)),
      field('body', $.struct_expression_body),
    )),

    struct_expression_body: $ => seq(
      '{',
      optional(seq(commaSep1($.struct_expression_field), optional(','))),
      '}',
    ),

    struct_expression_field: $ => choice(
      seq(
        field('name', $.identifier),
        ':',
        field('value', $._expression),
      ),
      // Shorthand: { x } means { x: x }
      field('shorthand', $.identifier),
    ),

    // -- Control-flow expressions ----------------------------------------
    block: $ => seq('{', repeat($._statement), '}'),
    block_expression: $ => $.block,

    if_expression: $ => prec.right(seq(
      'if',
      field('condition', $._expression),
      field('consequence', $.block),
      optional(seq(
        'else',
        field('alternative', choice($.block, $.if_expression, $.if_let_expression)),
      )),
    )),

    if_let_expression: $ => prec.right(seq(
      'if', 'let',
      field('pattern', $._pattern),
      '=',
      field('value', $._expression),
      field('consequence', $.block),
      optional(seq(
        'else',
        field('alternative', choice($.block, $.if_expression, $.if_let_expression)),
      )),
    )),

    // The scrutinee uses a struct-free expression set so `match x { ... }`
    // parses the brace as the match body instead of letting `x { ... }` win as
    // a struct literal (which would swallow the arms). Wrap a struct scrutinee
    // in parentheses: `match (Point { x: 1 }) { ... }`.
    match_expression: $ => seq(
      'match',
      field('value', $._match_scrutinee),
      '{',
      optional(seq(commaSep1($.match_arm), optional(','))),
      '}',
    ),

    // The postfix forms recurse through the struct-free set (aliased to their
    // public node names) so the tree shape is identical to a normal expression
    // while closing the `Name { ... }` struct back-door (e.g. `match val { ... }`
    // must not read `val { ... }` as a struct via a call's function position).
    _match_scrutinee: $ => choice(
      $._literal,
      $.identifier,
      $.self_expression,
      $.path_expression,
      $.parenthesized_expression,
      $.tuple_expression,
      $.array_expression,
      $.macro_invocation,
      $.macro_param,
      alias($._scrutinee_call, $.call_expression),
      alias($._scrutinee_method_call, $.method_call_expression),
      alias($._scrutinee_field, $.field_expression),
      alias($._scrutinee_index, $.index_expression),
      alias($._scrutinee_optional_chain, $.optional_chain_expression),
      alias($._scrutinee_try, $.try_expression),
      alias($._scrutinee_cast, $.cast_expression),
      alias($._scrutinee_type_check, $.type_check_expression),
    ),

    _scrutinee_call: $ => prec(PREC.call, seq(
      field('function', $._match_scrutinee),
      field('arguments', $.argument_list),
    )),
    _scrutinee_method_call: $ => prec(PREC.call, seq(
      field('receiver', $._match_scrutinee),
      '.',
      field('method', $._method_name),
      field('arguments', $.argument_list),
    )),
    _scrutinee_field: $ => prec(PREC.call, seq(
      field('object', $._match_scrutinee),
      '.',
      field('field', $._method_name),
    )),
    _scrutinee_index: $ => prec(PREC.call, seq(
      field('object', $._match_scrutinee),
      '[',
      field('index', $._expression),
      ']',
    )),
    _scrutinee_optional_chain: $ => prec(PREC.call, seq(
      field('object', $._match_scrutinee),
      '?.',
      field('field', $._method_name),
    )),
    _scrutinee_try: $ => prec(PREC.try, seq(
      field('value', $._match_scrutinee),
      '?',
    )),
    _scrutinee_cast: $ => prec.left(PREC.cast, seq(
      field('value', $._match_scrutinee),
      'as',
      field('type', $._type),
    )),
    _scrutinee_type_check: $ => prec.left(PREC.cast, seq(
      field('value', $._match_scrutinee),
      'is',
      field('type', $._type),
    )),

    match_arm: $ => seq(
      field('pattern', $._pattern),
      optional(seq('if', field('guard', $._expression))),
      '=>',
      field('body', $._expression),
    ),

    lambda_expression: $ => prec(-1, seq(
      '|',
      optional(seq(commaSep1($.parameter), optional(','))),
      '|',
      optional(seq('->', field('return_type', $._type))),
      field('body', $._expression),
    )),

    for_expression: $ => seq(
      'for',
      field('binding', choice(
        $.identifier,
        $.tuple_pattern_binding,
      )),
      'in',
      field('iter', $._expression),
      field('body', $.block),
    ),

    while_expression: $ => seq(
      'while',
      field('condition', $._expression),
      field('body', $.block),
    ),

    while_let_expression: $ => seq(
      'while', 'let',
      field('pattern', $._pattern),
      '=',
      field('value', $._expression),
      field('body', $.block),
    ),

    loop_expression: $ => seq('loop', field('body', $.block)),

    // effect annotation on functions: fn f() with IO + Net -> T { ... }
    with_clause: $ => seq('with', sep1(choice($.generic_type, $.type_path), '+')),

    // Rust-style pattern macros: macro_rules! name { (matcher) => { body } ... }
    // Matchers and transcribers are token trees (balanced delimiters wrapping
    // arbitrary tokens), so we parse structure without interpreting the macro DSL.
    macro_rules_item: $ => seq(
      'macro_rules',
      '!',
      field('name', $.identifier),
      field('body', $.macro_rules_body),
    ),

    macro_rules_body: $ => seq('{', repeat($.macro_rule), '}'),

    macro_rule: $ => seq(
      field('matcher', $._macro_tt),
      '=>',
      field('transcriber', $._macro_tt),
      optional(';'),
    ),

    _macro_tt: $ => choice(
      seq('(', repeat($._macro_token), ')'),
      seq('[', repeat($._macro_token), ']'),
      seq('{', repeat($._macro_token), '}'),
    ),

    // macro metavariable: $name or $name:fragment
    macro_fragment: $ => prec.right(seq('$', $.identifier, optional(seq(':', $.identifier)))),

    // repetition: $( ... )sep* / $( ... )+ / $( ... )?
    macro_repetition: $ => seq(
      '$',
      '(', repeat($._macro_token), ')',
      optional(choice(',', ';', '|')),
      choice('*', '+', '?'),
    ),

    _macro_token: $ => choice(
      $._macro_tt,
      $.macro_repetition,
      $.macro_fragment,
      $.identifier,
      $._literal,
      ':', ';', ',', '.', '=>', '->', '=', '==', '!=', '<', '>', '<=', '>=',
      '+', '-', '*', '/', '%', '**', '&&', '||', '!', '&', '|', '^', '?', '@',
      '..', '::',
    ),

    // module-level override of an imported binding: override driver = "opengl"
    override_declaration: $ => seq(
      'override',
      field('name', $.identifier),
      optional(seq(':', field('type', $._type))),
      '=',
      field('value', $._expression),
      optional(';'),
    ),

    // raise an effect: perform IO::read(path)
    perform_expression: $ => prec.right(seq('perform', $._expression)),

    // interpret effects: handle expr with { Eff::op(args) => body, ... }
    handle_expression: $ => prec.right(seq(
      'handle',
      field('value', $._expression),
      'with',
      field('handler', choice(
        $.handle_block,
        $.path_expression,
        $.identifier,
      )),
    )),

    handle_block: $ => seq('{', repeat($.handle_arm), '}'),

    handle_arm: $ => seq(
      field('pattern', $._expression),
      '=>',
      field('body', $._expression),
      optional(','),
    ),

    // handler value literal: handler { Eff::op(args) => body, ... }
    // `handler {` is lexed as ONE token, so a bare `handler` stays a plain
    // identifier everywhere else (e.g. the std::handler module in
    // `handler.compose(...)`), avoiding a keyword/identifier clash.
    handler_expression: $ => seq(
      $._handler_open,
      repeat($.handle_arm),
      '}',
    ),

    _handler_open: _ => token(seq('handler', /\s*/, '{')),

    // structured concurrency: scope { spawn ...; spawn ... }
    scope_expression: $ => seq('scope', field('body', $.block)),

    // select { x := <- ch => ...; after Ns => ...; default => ... }
    select_expression: $ => seq('select', '{', repeat($.select_arm), '}'),

    select_arm: $ => seq(
      field('guard', $.select_guard),
      '=>',
      field('body', $._expression),
      optional(choice(',', ';')),
    ),

    select_guard: $ => choice(
      seq(field('binding', $.identifier), ':=', '<-', field('channel', $._expression)),
      seq('<-', field('channel', $._expression)),
      seq('after', field('duration', $._expression)),
      'default',
    ),

    try_catch_expression: $ => seq(
      'try',
      field('body', $.block),
      optional(seq(
        'catch',
        optional(field('binding', $.identifier)),
        field('handler', $.block),
      )),
      optional(seq(
        'finally',
        field('finalizer', $.block),
      )),
    ),

    await_expression: $ => prec.right(seq('await', $._expression)),
    yield_expression: $ => prec.right(seq('yield', optional($._expression))),
    spawn_expression: $ => prec.right(seq(
      'spawn',
      choice($._expression, $.block),
    )),

    every_expression: $ => seq(
      'every',
      optional(field('interval', $._expression)),
      field('body', $.block),
    ),

    after_expression: $ => seq(
      'after',
      field('delay', $._expression),
      field('body', $.block),
    ),

    timeout_expression: $ => seq(
      'timeout',
      field('duration', $._expression),
      field('body', $.block),
    ),

    sleep_expression: $ => prec.right(seq('sleep', $._expression)),

    // -- Macros -----------------------------------------------------------
    macro_invocation: $ => prec(PREC.call, seq(
      field('macro', $.identifier),
      '!',
      choice(
        seq('(', optional(seq(commaSep1($._expression), optional(','))), ')'),
        seq('[', optional(seq(commaSep1($._expression), optional(','))), ']'),
        seq('{', optional(seq(commaSep1($._expression), optional(','))), '}'),
      ),
    )),

    // $name placeholder inside macro body
    macro_param: $ => seq('$', $.identifier),

    // ---------------------------------------------------------------------
    // Patterns
    // ---------------------------------------------------------------------
    _pattern: $ => choice(
      $.literal_pattern,
      $.identifier_pattern,
      $.wildcard_pattern,
      $.tuple_pattern,
      $.array_pattern,
      $.struct_pattern,
      $.enum_pattern,
      $.range_pattern,
      $.or_pattern,
      $.binding_pattern,
    ),

    literal_pattern: $ => choice(
      $.integer_literal,
      $.float_literal,
      $.string_literal,
      $.char_literal,
      $.bool_literal,
      $.nil_literal,
      seq('-', $.integer_literal),
      seq('-', $.float_literal),
    ),

    identifier_pattern: $ => $.identifier,
    wildcard_pattern: _ => '_',

    tuple_pattern: $ => seq(
      '(',
      optional(seq(commaSep1($._pattern), optional(','))),
      ')',
    ),

    array_pattern: $ => seq(
      '[',
      optional(seq(
        commaSep1(choice(
          $._pattern,
          seq('..', optional(field('rest', $.identifier))),
        )),
        optional(','),
      )),
      ']',
    ),

    struct_pattern: $ => prec(3, seq(
      field('name', $.identifier),
      '{',
      optional(seq(
        commaSep1(choice($.field_pattern, '..')),
        optional(','),
      )),
      '}',
    )),

    enum_pattern: $ => prec(3, choice(
      // Dot-shorthand variant: `.Build { arch }`, `.Start`, `.Ok(x)`
      seq('.', field('variant', $.identifier), optional($._enum_pattern_body)),
      // Qualified variant: `Color::Red`, `Option::Some(x)`, `Shape::Rect { w, h }`
      // (the argument list is optional, allowing bare unit variants).
      seq(
        field('path', $.path_expression),
        optional($._enum_pattern_body),
      ),
      // Unqualified tuple variant: `Some(x)`, `Ok(v)`. The argument list is
      // required so a bare `Some` stays an identifier (binding) pattern, and a
      // bare `Point { ... }` stays a struct pattern.
      seq(
        field('path', $.identifier),
        seq('(', optional(seq(commaSep1($._pattern), optional(','))), ')'),
      ),
    )),

    _enum_pattern_body: $ => choice(
      seq('(', optional(seq(commaSep1($._pattern), optional(','))), ')'),
      seq('{', optional(seq(commaSep1(choice($.field_pattern, '..')), optional(','))), '}'),
    ),

    field_pattern: $ => seq(
      field('name', $.identifier),
      optional(seq(':', field('pattern', $._pattern))),
    ),

    range_pattern: $ => prec(2, seq(
      field('start', choice($.integer_literal, $.float_literal, $.char_literal, seq('-', $.integer_literal), seq('-', $.float_literal))),
      choice('..', '..='),
      field('end', choice($.integer_literal, $.float_literal, $.char_literal, seq('-', $.integer_literal), seq('-', $.float_literal))),
    )),

    or_pattern: $ => prec.left(1, seq(
      $._pattern,
      '|',
      $._pattern,
    )),

    binding_pattern: $ => prec(4, seq(
      field('name', $.identifier),
      '@',
      field('pattern', $._pattern),
    )),

    // ---------------------------------------------------------------------
    // Types
    // ---------------------------------------------------------------------
    _type: $ => choice(
      $.primitive_type,
      $.type_path,
      $.generic_type,
      $.array_type,
      $.map_type,
      $.tuple_type,
      $.parenthesized_type,
      $.optional_type,
      $.function_type,
      $.union_type,
      $.variadic_type,
    ),

    primitive_type: _ => choice(
      'int', 'float', 'bool', 'str', 'string', 'char',
      'any', 'void', 'never', 'bytes', 'bigint', 'decimal', 'bigdecimal',
    ),

    type_path: $ => prec.left(seq(
      $.identifier,
      repeat(seq('::', $.identifier)),
    )),

    generic_type: $ => seq(
      field('name', $.identifier),
      '<',
      commaSep1($._type),
      optional(','),
      '>',
    ),

    array_type: $ => seq('[', $._type, ']'),

    // varargs type: fn f(xs: ...int)
    variadic_type: $ => prec.right(seq('...', $._type)),

    map_type: $ => seq('{', $._type, ':', $._type, '}'),

    tuple_type: $ => seq(
      '(',
      $._type,
      ',',
      optional(seq(commaSep1($._type), optional(','))),
      ')',
    ),

    parenthesized_type: $ => seq('(', $._type, ')'),

    optional_type: $ => prec(1, seq($._type, '?')),

    function_type: $ => seq(
      'fn',
      '(',
      optional(seq(commaSep1($._type), optional(','))),
      ')',
      optional(seq('->', $._type)),
    ),

    union_type: $ => prec.left(seq($._type, '|', $._type)),

    // composite type: A + B + C  (e.g. effect/trait sets). Nominal operands
    // only, so it never fights function_type over a trailing `->` ... `+`.
    intersection_type: $ => prec.left(seq(
      field('left', choice($.type_path, $.generic_type, $.primitive_type, $.intersection_type)),
      '+',
      field('right', choice($.type_path, $.generic_type, $.primitive_type)),
    )),

    // ---------------------------------------------------------------------
    // Literals
    // ---------------------------------------------------------------------
    _literal: $ => choice(
      $.integer_literal,
      $.float_literal,
      $.bigint_literal,
      $.duration_literal,
      $.string_literal,
      $.raw_string_literal,
      $.triple_string_literal,
      $.fenced_string_literal,
      $.bytes_literal,
      $.regex_literal,
      $.tagged_string_literal,
      $.tagged_raw_string_literal,
      $.char_literal,
      $.bool_literal,
      $.nil_literal,
    ),

    bool_literal: _ => choice('true', 'false'),
    nil_literal: _ => 'nil',

    integer_literal: _ => token(choice(
      seq(/0[xX][0-9a-fA-F](_?[0-9a-fA-F])*/),
      seq(/0[oO][0-7](_?[0-7])*/),
      seq(/0[bB][01](_?[01])*/),
      seq(/[0-9](_?[0-9])*/),
    )),

    float_literal: _ => token(choice(
      // 1.0  1.0e10  1.0e-10  (optional d/bd = decimal/bigdecimal)
      /[0-9](_?[0-9])*\.[0-9](_?[0-9])*([eE][+-]?[0-9]+)?(bd|d)?/,
      // 1e10
      /[0-9](_?[0-9])*[eE][+-]?[0-9]+(bd|d)?/,
    )),

    bigint_literal: _ => token(/[0-9](_?[0-9])*n/),

    // 5s, 100ms, 30min, 2h, 1d, 1w, 500us, 1ns
    duration_literal: _ => token(seq(
      /[0-9](_?[0-9])*(\.[0-9](_?[0-9])*)?/,
      choice('min', 'ms', 'ns', 'us', 's', 'h', 'd', 'w'),
    )),

    char_literal: _ => token(seq(
      "'",
      choice(
        /[^'\\\n]/,
        /\\(.|\n)/,
      ),
      "'",
    )),

    // -- Strings ---------------------------------------------------------
    // Regular: "..." with optional interpolation {expr}
    string_literal: $ => seq(
      '"',
      repeat(choice(
        $.string_content,
        $.escape_sequence,
        $.string_interpolation,
      )),
      '"',
    ),

    string_content: _ => token.immediate(prec(1, /[^"\\{]+/)),

    escape_sequence: _ => token.immediate(seq(
      '\\',
      choice(
        /[abfnrtv0\\'"{}]/,
        /x[0-9a-fA-F]{2}/,
        /u\{[0-9a-fA-F]+\}/,
        /[0-9]{1,3}/,
      ),
    )),

    string_interpolation: $ => seq(
      token.immediate('{'),
      $._expression,
      optional(seq(':', $.format_spec)),
      '}',
    ),

    format_spec: _ => /[^}]+/,

    // r"..." and r#"..."# raw strings (no interpolation). The body alternatives
    // allow embedded quotes and a content that *ends* in quote(s) right before
    // the closing `"#` / `"##` / `"###` (e.g. `r#""hi""#`): the trailing `"*`
    // soaks up those quotes so the closer still matches.
    raw_string_literal: _ => token(seq(
      'r',
      choice(
        seq('"', /[^"]*/, '"'),
        seq('#"',   /([^"]|"+[^"#])*"*/, '"#'),
        seq('##"',  /([^"]|"+[^"#]|"+#[^#])*"*/, '"##'),
        seq('###"', /([^"]|"+[^"#]|"+#[^#]|"+##[^#])*"*/, '"###'),
      ),
    )),

    // """ ... """
    triple_string_literal: _ => token(seq(
      '"""',
      /([^"]|"[^"]|""[^"])*/,
      '"""',
    )),

    bytes_literal: $ => seq(
      'b"',
      repeat(choice(
        $.string_content,
        $.escape_sequence,
      )),
      '"',
    ),

    regex_literal: _ => token(seq('re"', /[^"\n]*/, '"')),

    // tag"..." or tag"...{expr}..."
    tagged_string_literal: $ => seq(
      field('tag', $.identifier),
      token.immediate('"'),
      repeat(choice(
        $.string_content,
        $.escape_sequence,
        $.string_interpolation,
      )),
      '"',
    ),

    // tag#"..."# — tagged RAW string (no interpolation; embedded quotes ok).
    // The whole `#"..."#` tail is one immediate token, so a plain identifier
    // elsewhere is untouched. e.g. `json#"{ "k": 1 }"#`, `sql#"... "x" ..."#`.
    tagged_raw_string_literal: $ => seq(
      field('tag', $.identifier),
      field('value', $._raw_string_tail),
    ),

    _raw_string_tail: _ => token.immediate(choice(
      seq('#"',   /([^"]|"+[^"#])*"*/, '"#'),
      seq('##"',  /([^"]|"+[^"#]|"+#[^#])*"*/, '"##'),
      seq('###"', /([^"]|"+[^"#]|"+#[^#]|"+##[^#])*"*/, '"###'),
    )),

    // ```lang ... ``` — fenced block string (raw, multi-line, optional tag).
    fenced_string_literal: _ => token(seq(
      '```',
      /[a-zA-Z0-9_+-]*/,
      /([^`]|`[^`]|``[^`])*/,
      '```',
    )),

    // ---------------------------------------------------------------------
    // Identifier
    // ---------------------------------------------------------------------
    identifier: _ => /[a-zA-Z_][a-zA-Z0-9_]*/,
  },
});

// Helpers --------------------------------------------------------------------

/**
 * Creates a comma-separated list, with at least one element.
 */
function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

/**
 * Creates a list separated by `sep`, with at least one element.
 */
function sep1(rule, sep) {
  return seq(rule, repeat(seq(sep, rule)));
}
