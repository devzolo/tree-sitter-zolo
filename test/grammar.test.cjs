'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

// Inspect the authored data transformation before the tree-sitter DSL runs.
// These checks are cheap enough to catch destructive extractions before a
// full parser generation, while corpus tests still check the compiled parser.
const source = fs.readFileSync(path.join(__dirname, '..', 'grammar.js'), 'utf8');
const end = source.indexOf('extendFifthGrammar(contextGrammarData);');
assert.ok(end > 0, 'the authored extension must be available');
const context = {};
vm.runInNewContext(source.slice(0, end) + `
  globalThis.before = JSON.parse(JSON.stringify(contextGrammarData));
  extendFifthGrammar(contextGrammarData);
  globalThis.after = contextGrammarData;
`, context);
const before = JSON.parse(JSON.stringify(context.before));
const after = JSON.parse(JSON.stringify(context.after));
const old = before.rules;
const current = after.rules;
const members = rule => rule.type === 'SEQ' ? rule.members : members(rule.content);

function preservesAlternatives(name) {
  const original = old[name].type === 'CHOICE' ? old[name].members : [old[name]];
  assert.equal(current[name].type, 'CHOICE', name);
  assert.deepEqual(current[name].members.slice(0, original.length), original, name);
}

test('computed projections retain every segment and the existing as alias', () => {
  const selection = old.record_projection_body.members[1].members[0].members[0];
  assert.equal(selection.type, 'SEQ');
  assert.deepEqual(selection.members.map(member => member.type), ['FIELD', 'REPEAT', 'CHOICE']);
  assert.equal(selection.members[1].content.members[0].value, '.');
  assert.equal(selection.members[2].members[0].members[0].value, 'as');
  assert.deepEqual(current.record_projection_entry.members[0], selection);
  assert.equal(current.record_projection_entry.members[1].name, 'computed_projection_field');
});

test('indexed updates keep the original carrier, dotted path and block update', () => {
  assert.deepEqual(current.record_update_path.members[0], old.record_update_path.members[0]);
  assert.deepEqual(current.record_update_path.members[1].content.members[0], old.record_update_path.members[1].content);
  const original = members(old.record_update_expression);
  const extended = members(current.record_update_expression);
  assert.deepEqual(extended.slice(0, -1), original.slice(0, -1));
  assert.deepEqual(extended.at(-1).members[0], original.at(-1));
  assert.deepEqual(current.record_update_body.content, old.record_update_body);
});

test('expanded parameters and fallible signatures preserve complete old forms', () => {
  preservesAlternatives('parameter');
  for (const name of ['function_type', '_lambda_header']) {
    assert.deepEqual(members(current[name]).slice(0, -1), members(old[name]), name);
  }
});

test('stepped and zipped iteration retain the complete original iteration', () => {
  assert.deepEqual(members(current._for_iteration).slice(0, -2), members(old._for_iteration));
});

test('new types and control expressions retain all previous alternatives', () => {
  for (const name of ['_type', '_pattern', '_expression', '_tier_non_dot_prefix', '_tier_non_dot_atom', '_handler_atom', 'markup_attribute']) {
    preservesAlternatives(name);
  }
  assert.deepEqual(current.state_index_argument.content, old.state_index_argument);
  // `+` remains contextual in trait bounds and effect/handler lists; it was
  // never an ordinary annotation intersection in the compiler's parser.
  assert.equal(current.intersection_type.content.members[1].value, '&');
});

test('contextual prefixes preserve ordinary identifiers and the external token ABI', () => {
  const added = ['_patch_keyword', '_fold_keyword', '_freeze_keyword', '_typed_json_start'];
  assert.deepEqual(after.externals.filter(symbol => !added.includes(symbol.name)), before.externals);
  assert.deepEqual(after.externals.slice(-5).map(symbol => symbol.name), [...added, '_error_sentinel']);
  assert.equal(after.externals.at(-1).name, '_error_sentinel');
  assert.equal(after.externals.at(-2).name, '_typed_json_start');
  for (const name of ['identifier', '_literal', 'string_literal', 'raw_string_literal', 'tagged_string_literal', 'tagged_raw_string_literal']) {
    assert.deepEqual(current[name], old[name], name);
  }
  const introducer = current.typed_json_expression.members[0];
  assert.equal(introducer.content.name, '_typed_json_start');
  assert.equal(introducer.value, 'json');
  for (const word of ['patch', 'fold', 'freeze']) {
    const introducer = members(current[`${word}_expression`])[0];
    assert.deepEqual(introducer, {type: 'ALIAS', content: {type: 'SYMBOL', name: `_${word}_keyword`}, value: word, named: false});
  }
});

test('table labels and resources retain the existing header and declaration', () => {
  assert.deepEqual(current.test_table_header.members.slice(0, -1), old.test_table_header.members.slice(0, -1));
  assert.deepEqual(current.test_table_header.members.at(-1).content.members[0], old.test_table_header.members.at(-1).content);
  const oldMembers = members(old.titled_test_declaration);
  const newMembers = members(current.titled_test_declaration);
  assert.deepEqual(newMembers.slice(0, -2), oldMembers.slice(0, -1));
  assert.deepEqual(newMembers.at(-1), oldMembers.at(-1));
});

test('all rules outside the explicit extension set remain unchanged', () => {
  const changed = new Set([
    'parameter', 'record_update_path', 'record_update_expression', 'record_update_body',
    'record_projection_body', 'function_type', '_lambda_header', '_tier_non_dot_prefix',
    '_tier_non_dot_atom', '_handler_atom', '_expression', '_type', 'state_index_argument',
    '_pattern', '_for_iteration', 'markup_attribute', 'test_table_header', 'titled_test_declaration',
    'intersection_type', 'extern_dependency_options', 'for_expression', '_array_for_binding',
  ]);
  for (const [name, rule] of Object.entries(old)) {
    if (!changed.has(name)) assert.deepEqual(current[name], rule, name);
  }
});

test('for mut adds one binding alternative and leaves the loop shapes alone', () => {
  assert.deepEqual(current.mutable_loop_binding, {type: 'SEQ', members: [
    {type: 'STRING', value: 'mut'},
    {type: 'FIELD', name: 'name', content: {type: 'SYMBOL', name: 'identifier'}},
  ]});
  const added = {type: 'SYMBOL', name: 'mutable_loop_binding'};
  const binding = rule => JSON.stringify(rule).replace(',' + JSON.stringify(added), '');
  assert.equal(binding(current.for_expression), JSON.stringify(old.for_expression));
  assert.deepEqual(current._array_for_binding.members.at(-1), added);
  assert.deepEqual(current._array_for_binding.members.slice(0, -1), old._array_for_binding.members);
});
