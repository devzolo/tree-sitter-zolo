'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '..', 'grammar.js'), 'utf8');
const boundary = source.indexOf('extendSeventhGrammar(contextGrammarData);');
assert.ok(boundary > 0);
const context = {};
vm.runInNewContext(source.slice(0, boundary) + `
this.before = JSON.parse(JSON.stringify(contextGrammarData));
extendSeventhGrammar(contextGrammarData);
this.after = contextGrammarData;
`, context);
const before = JSON.parse(JSON.stringify(context.before));
const after = JSON.parse(JSON.stringify(context.after));
const symbols = node => [
  ...(node.type === 'SYMBOL' ? [node.name] : []),
  ...(node.members || []).flatMap(symbols),
  ...(node.content ? symbols(node.content) : []),
];
const strings = node => [
  ...(node.type === 'STRING' ? [node.value] : []),
  ...(node.members || []).flatMap(strings),
  ...(node.content ? strings(node.content) : []),
];
const assertAppendOnlyExternal = () => {
  assert.deepEqual(after.externals.slice(0, -2), before.externals.slice(0, -1));
  assert.deepEqual(after.externals.at(-2), {type: 'SYMBOL', name: '_type_application_start'});
  assert.deepEqual(after.externals.at(-1), before.externals.at(-1));
};
test('foreign bodies and existing expression markers retain their token indices', () => {
  assertAppendOnlyExternal();
  const scanner = fs.readFileSync(path.join(__dirname, '..', 'src', 'scanner.c'), 'utf8');
  const tail = scanner.slice(scanner.indexOf('  TYPE_APPLICATION_START,'), scanner.indexOf('\n};'));
  assert.deepEqual(tail.match(/\b[A-Z][A-Z_]+\b/g), ['TYPE_APPLICATION_START', 'ERROR_SENTINEL']);
  for (const name of Object.keys(before.rules).filter(name => /^_scope_/.test(name))) {
    assert.deepEqual(after.rules[name], before.rules[name], name);
  }
  for (const name of ['extern_body', 'extern_module', 'raw_string_literal', 'raw_triple_string_literal']) {
    assert.deepEqual(after.rules[name], before.rules[name], name);
  }
});
test('named performs share ordinary named call children', () => {
  assert.ok(symbols(after.rules._perform_call).includes('argument_list'));
  assert.deepEqual(after.rules.argument_list, before.rules.argument_list);
  assert.equal(after.rules._perform_argument_list, undefined);
  assert.ok(symbols(after.rules.handler_override_arm).includes('_scoped_iterator'));
});
test('new data syntax retains real traversable expression children', () => {
  for (const name of ['map_spread_entry', 'map_pattern_key', 'map_pattern_entry']) {
    assert.ok(symbols(after.rules[name]).includes('_expression'), name);
  }
  assert.ok(strings(after.rules.struct_expression_field).includes('...'));
  assert.ok(strings(after.rules.record_update_path).includes('*'));
  assert.ok(strings(after.rules.map_pattern_entry).includes('?'));
  assert.ok(strings(after.rules.keyof_type).includes('keyof'));
  assert.ok(symbols(after.rules.indexed_field_type).includes('string_literal'));
});
test('new type applications leave cast and comparison productions intact', () => {
  assert.deepEqual(after.rules.cast_expression, before.rules.cast_expression);
  assert.deepEqual(after.rules.binary_expression, before.rules.binary_expression);
  assert.deepEqual(after.rules._handler_binary, before.rules._handler_binary);
  assert.ok(symbols(after.rules.generic_type).includes('named_type_argument'));
  assert.ok(symbols(after.rules.type_application_arguments).includes('named_type_argument'));
  assert.ok(symbols(after.rules.type_application_arguments).includes('_type_application_start'));
  assert.equal(after.rules.type_application_expression.value, 31);
  assert.ok(symbols(after.rules._handler_header).includes('_handler_type_application'));
  for (const fork of before.conflicts.filter(names => names.includes('_qualified_type_owner'))) {
    assert.ok(after.conflicts.some(names => names.length === fork.length + 1 &&
      [...fork, '_projectable_type_owner'].every(name => names.includes(name))), fork.join(', '));
  }
});
test('complete type projections retain their BP31 owner and fields across the type/value fork', () => {
  assert.equal(after.rules.type_projection.type, 'PREC_DYNAMIC');
  assert.equal(after.rules.type_projection.value, 1);
  const projected = JSON.parse(JSON.stringify(after.rules.type_projection.content));
  assert.equal(projected.type, before.rules.type_projection.type);
  assert.equal(projected.value, before.rules.type_projection.value);
  projected.content.members[0].content = after.rules._projectable_type_owner;
  assert.deepEqual(projected, before.rules.type_projection);
});
test('local declarations and foreign imports have separate source trees', () => {
  assert.ok(symbols(after.rules._statement).includes('use_declaration'));
  assert.ok(symbols(after.rules.self_parameter).includes('_type'));
  assert.ok(symbols(after.rules._extern_group_child).includes('extern_use'));
  assert.ok(symbols(after.rules._extern_group_child).includes('extern_module'));
  assert.ok(symbols(after.rules._extern_group_child).includes('extern_group'));
  assert.ok(!symbols(after.rules._extern_group_child).includes('use_declaration'));
  assert.ok(strings(after.rules.import_exclusion).includes('except'));
  assert.ok(symbols(after.rules.labeled_block_expression).includes('block'));
  assert.ok(strings(after.rules._lambda_header).includes('async'));
  assert.ok(symbols(after.rules.function_expression_body).includes('local_where_clause'));
});

test('factored callable tails preserve every authored field and nullable suffix', () => {
  const continuations = new Set([
    '_function_contract_body', '_function_where_body', '_function_fallible_body',
    '_function_return_body', '_function_effect_body', '_function_guard_body',
    '_function_parameters_body', '_extern_return_body', '_extern_parameters_body', '_extern_name_body',
    '_enum_body_tail', '_enum_where_body', '_enum_backing_body', '_enum_generic_body', '_enum_definition_tail',
    '_for_body_tail', '_for_iteration_body', '_for_binding_body', '_for_await_body',
  ]);
  const members = rule => rule.type === 'SEQ' ? rule.members : members(rule.content);
  const expand = name => members(after.rules[name]).flatMap(part =>
    part.type === 'SYMBOL' && continuations.has(part.name) ? expand(part.name) : [part]);
  assert.deepEqual(expand('_function_definition_tail'), before.rules._function_definition_tail.members);
  assert.deepEqual(expand('trait_method'), before.rules.trait_method.members);
  assert.deepEqual(expand('extern_function'), before.rules.extern_function.members);
  assert.deepEqual(expand('enum_item'), members(before.rules.enum_item));
  assert.deepEqual(expand('for_expression'), members(before.rules.for_expression));
  assert.deepEqual(after.rules.type_path, before.rules.type_path);
  assertAppendOnlyExternal();
});

test('published Neovim queries match the canonical grammar queries', () => {
  for (const name of ['highlights', 'locals', 'folds', 'indents', 'injections']) {
    const canonical = fs.readFileSync(path.join(__dirname, '..', 'queries', name + '.scm'), 'utf8');
    const neovim = fs.readFileSync(path.join(__dirname, '..', 'editors', 'nvim', 'queries', 'zolo', name + '.scm'), 'utf8');
    assert.equal(neovim, canonical, name);
  }
});

test('contextual timeout retains its original BP27 operand and required body', () => {
  assert.equal(after.rules.timeout_expression.type, 'PREC_DYNAMIC');
  const temporal = after.rules.timeout_expression.content;
  assert.equal(temporal.type, 'PREC');
  assert.equal(temporal.value, 27);
  assert.deepEqual(temporal.content, before.rules.timeout_expression.content);
  assert.ok(strings(after.rules._contextual_keyword).includes('timeout'));
  assert.ok(after.conflicts.some(names => names.includes('_contextual_keyword') && names.includes('timeout_expression')));
});
