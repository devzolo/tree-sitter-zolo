'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '..', 'grammar.js'), 'utf8');
const boundary = source.indexOf('extendSixthGrammar(contextGrammarData);');
assert.ok(boundary > 0, 'sixth grammar extension exists');
const context = {};
vm.runInNewContext(source.slice(0, boundary) + `
  this.before = JSON.parse(JSON.stringify(contextGrammarData));
  extendSixthGrammar(contextGrammarData);
  this.after = contextGrammarData;
`, context);
const before = JSON.parse(JSON.stringify(context.before));
const after = JSON.parse(JSON.stringify(context.after));
const old = before.rules, current = after.rules;
const members = rule => rule.type === 'SEQ' ? rule.members : members(rule.content);
const symbols = rule => [
  ...(rule.type === 'SYMBOL' ? [rule.name] : []),
  ...(rule.members || []).flatMap(symbols),
  ...(rule.content ? symbols(rule.content) : []),
];

test('relational bounds share the compiler BP20 endpoint instead of consuming pattern operators', () => {
  const parts = members(current.relational_pattern);
  const bound = parts.find(part => part.name === 'bound').content;
  assert.equal(bound.type, 'PREC');
  assert.equal(bound.value, 20);
  assert.deepEqual(bound.content, {type: 'SYMBOL', name: '_expression'});
  assert.equal(parts.at(-1).name, '_pratt_end20');
});

test('literal type strings retain string nodes and escapes without expression interpolation', () => {
  const literal = members(current.literal_type);
  assert.equal(literal[0].name, '_sixth_literal_type_start');
  for (const name of ['string_literal', 'triple_string_literal']) assert.ok(symbols(literal[1]).includes(name));
  assert.ok(!('_literal_type_string' in current));
  assert.ok(!('_literal_type_triple_string' in current));
  assert.deepEqual(current.string_literal, old.string_literal);
  assert.deepEqual(current.triple_string_literal, old.triple_string_literal);
});

test('literal string types retain raw triple and fenced forms without enabling interpolation braces', () => {
  for (const name of ['raw_string_literal', 'raw_triple_string_literal', 'fenced_string_literal']) {
    assert.ok(symbols(current.literal_type).includes(name), name);
  }
  assert.equal(members(current.literal_type)[0].name, '_sixth_literal_type_start');
  assert.ok(!('_literal_type_fence' in current));
});

test('fences retain line-oriented lexer boundaries and only untagged fences are scalar strings', () => {
  const whole = new RegExp(`^(?:${current.fenced_string_literal.content.value})$`);
  for (const source of ['```\n```', '```sql\nSELECT "{name}"\n```', '```\r\n  `\r\n  ``\r\n  text ``` inside\r\n  ```']) {
    assert.ok(whole.test(source), source);
  }
  for (const source of ['```sql SELECT 1 ```', '```c++\nbody\n```', '```\nbody ```', '```\nbody\n']) {
    assert.ok(!whole.test(source), source);
  }
  // The complete-lexeme C regression rejects tagged/inline fences before
  // this shared production; literal text and public children stay unchanged.
  assert.ok(symbols(current.literal_type).includes('fenced_string_literal'));
  // Bytes are a Str token in the parser; typeck rejects their non-scalar ABI.
  assert.ok(symbols(current.literal_type).includes('bytes_literal'));
});

test('empty tuples are admitted only as authored call expansions', () => {
  assert.ok(symbols(current.named_record_argument).includes('_empty_tuple_expansion'));
  assert.ok(!symbols(current._expression).includes('_empty_tuple_expansion'));
  assert.deepEqual(current.tuple_expression, old.tuple_expression);
});

test('foreign expression mode requires an authored bare extern token', () => {
  const parts = members(current.extern_expression);
  assert.deepEqual(parts[0], {type: 'STRING', value: 'extern'});
  assert.equal(parts[1].content.content.name, '_foreign_provider');
  assert.deepEqual(current.extern_expression, old.extern_expression);
});

test('scalar types share original numbers behind the strict guard and keep nil nominal', () => {
  assert.ok(!JSON.stringify(current.literal_type).includes('"value":"nil"'));
  assert.ok(symbols(current._type).includes('type_path'));
  for (const name of ['integer_literal', 'float_literal']) assert.ok(symbols(current.literal_type).includes(name));
  assert.ok(!('_literal_type_integer' in current));
  assert.ok(!('_literal_type_float' in current));
  assert.deepEqual(current.integer_literal, old.integer_literal);
  assert.deepEqual(current.float_literal, old.float_literal);
});

test('without shares the compiler BP0 continuation boundary and left association', () => {
  const parts = members(current.without_expression);
  assert.equal(current.without_expression.type, 'PREC_LEFT');
  assert.equal(current.without_expression.value, 0);
  assert.equal(parts[1].name, '_pratt_low_tail_start');
  assert.equal(parts[2].content.name, '_sixth_without_keyword');
  assert.equal(parts[2].value, 'without');
});

test('type suffixes admit structural and typeof owners and require grouping to repeat without', () => {
  const owner = members(current.without_type)[0];
  const ownerNames = [...symbols(owner), ...symbols(current._structural_type_base)];
  for (const name of ['record_type', 'type_query', 'parenthesized_type', 'type_projection']) {
    assert.ok(ownerNames.includes(name), name);
  }
  assert.ok(!symbols(owner).includes('without_type'));
  assert.ok(!symbols(owner).includes('optional_type'));
  assert.ok(!symbols(owner).includes('union_type'));
  assert.ok(symbols(members(current.type_projection)[0]).includes('_structural_type_base'));
  assert.ok(symbols(current._structural_type_base).includes('record_type'));
});

test('coalescing exits explicitly choose same-line values or empty lexical endings', () => {
  const endings = symbols(current.coalescing_exit);
  assert.ok(endings.includes('_sixth_exit_empty'));
  assert.ok(endings.includes('_sixth_exit_value'));
  assert.ok(endings.includes('_pratt_end0'));
});

test('computed properties retain decorators on both sides of public visibility', () => {
  const parts = members(current.computed_property);
  assert.deepEqual(parts[0], {type: 'REPEAT', content: {type: 'SYMBOL', name: 'decorator'}});
  const visibility = parts[1].members[0].members;
  assert.equal(visibility[0].value, 'pub');
  assert.deepEqual(visibility[1], parts[0]);
  assert.equal(parts[2].value, 'get');
  assert.equal(parts[3].name, '_function_definition_tail');
  const original = members(old.function_item);
  const start = original.findIndex(part => part.type === 'FIELD' && part.name === 'name');
  assert.deepEqual(members(current._function_definition_tail), original.slice(start));
  assert.deepEqual(members(current.function_item).slice(0, start), original.slice(0, start));
  assert.equal(members(current.function_item)[start].name, '_function_definition_tail');
});

test('lexical using preserves acquisition flags and every existing grouped binding', () => {
  const original = members(old.resource_scope_expression);
  const lexical = members(current.lexical_resource_declaration);
  assert.equal(lexical[0].name, '_resource_acquisition');
  assert.deepEqual(members(current._resource_acquisition), original.slice(0, -1));
  assert.equal(members(current.resource_scope_expression)[0].name, '_resource_acquisition');
  assert.deepEqual(members(current.resource_scope_expression)[1], original.at(-1));
  assert.deepEqual(current.resource_binding, old.resource_binding);
});

test('defer outcomes are visible queryable nodes while legacy calls stay available', () => {
  const introducer = members(current.defer_statement)[0];
  assert.deepEqual(introducer.members[0], members(old.defer_statement)[0]);
  const outcome = introducer.members[1].members[1];
  assert.equal(outcome.name, 'outcome');
  assert.equal(outcome.content.value, 'defer_outcome');
  assert.equal(outcome.content.named, true);
});

test('escaped identifiers retain ordinary names and reject empty or multiline names', () => {
  const identifier = new RegExp(`^(?:${current.identifier.value})$`);
  for (const name of ['plain', '`line-total`', '`return`', '`ümlaut`', '`x\\`y`', '`x\\\\y`']) {
    assert.ok(identifier.test(name), name);
  }
  for (const name of ['``', '`line\nname`', '`line\rname`']) {
    assert.ok(!identifier.test(name), name);
  }
});

test('sixth contextual token insertion preserves the prior external scanner ABI', () => {
  const additions = ['_sixth_set_keyword', '_sixth_without_keyword', '_sixth_get_keyword', '_sixth_defer_outcome', '_sixth_assert_keyword', '_sixth_where_keyword', '_sixth_exit_empty', '_sixth_exit_value', '_sixth_literal_type_start', '_sixth_const_argument_start', '_sixth_raw_string_literal', '_sixth_raw_triple_string_literal', '_sixth_storage_start', '_sixth_loop_label_start'];
  assert.deepEqual(after.externals.filter(symbol => !additions.includes(symbol.name)), before.externals);
  assert.deepEqual(after.externals.slice(-(additions.length + 1)).map(symbol => symbol.name), [...additions, '_error_sentinel']);
});

test('factored tails stay hidden and shared without changing original const alternatives', () => {
  for (const name of ['_function_definition_tail', '_resource_acquisition', '_structural_type_base']) {
    assert.ok(!after.inline.includes(name));
  }
  assert.deepEqual(current.const_type_argument.members[0], old.const_type_argument);
  assert.equal(current.const_type_argument.members[1].members[0].name, '_sixth_const_argument_start');
  assert.deepEqual(current.const_type_argument.members[1].members[1], old.const_type_argument);
});

test('opaque raw lexer tokens retain the public nodes and append after all existing tokens', () => {
  assert.deepEqual(current.raw_string_literal, {type: 'SYMBOL', name: '_sixth_raw_string_literal'});
  assert.deepEqual(current.raw_triple_string_literal, {type: 'SYMBOL', name: '_sixth_raw_triple_string_literal'});
  const scanner = fs.readFileSync(path.join(__dirname, '..', 'src', 'scanner.c'), 'utf8');
  const tail = scanner.slice(scanner.indexOf('  SIXTH_SET_KEYWORD,'), scanner.indexOf('\n};', scanner.indexOf('  SIXTH_SET_KEYWORD,')));
  const names = tail.match(/\b[A-Z][A-Z_]+\b/g);
  assert.deepEqual(names, after.externals.slice(-15).map(symbol => symbol.name.slice(1).toUpperCase()));
});

test('new parenthesized atoms preserve all expression flag profiles and storage/label decisions', () => {
  for(const name of ['operator_value', '_empty_tuple_expansion']) {
    const profiles=members(current[name])[1].members;
    assert.equal(profiles.length,8);
    const names=profiles.flatMap(profile=>symbols(current[profile.name]));
    for(let flags=0;flags<8;flags++) {
      assert.ok(names.includes(`_scope_paren_enter${flags}`), `${name} enter ${flags}`);
      assert.ok(names.includes(`_scope_paren_restore${flags}`), `${name} restore ${flags}`);
    }
    const bodies=names.filter(name=>name.endsWith('_paren_body'));
    assert.equal(new Set(bodies).size,1,'profiles reuse one hidden body');
    assert.ok(symbols(current[bodies[0]]).includes('_scope_delimited_end'));
  }
  assert.equal(members(current.storage_class)[0].name,'_sixth_storage_start');
  assert.equal(members(current.loop_label_reference)[0].name,'_sixth_loop_label_start');
});
