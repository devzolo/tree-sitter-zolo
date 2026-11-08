'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '..', 'grammar.js'), 'utf8');
const end = source.indexOf('extendLocalTypeQueries(contextGrammarData);');
assert.ok(end > 0);
const context = {};
vm.runInNewContext(source.slice(0, end) + `
  globalThis.before = JSON.parse(JSON.stringify(contextGrammarData));
  extendLocalTypeQueries(contextGrammarData);
  globalThis.after = contextGrammarData;
`, context);
const before = JSON.parse(JSON.stringify(context.before));
const after = JSON.parse(JSON.stringify(context.after));

test('local type aliases reuse the declaration without changing item precedence', () => {
  assert.deepEqual(after.rules.type_alias, before.rules.type_alias);
  assert.deepEqual(after.rules._statement.members.slice(0, -1), before.rules._statement.members);
  assert.deepEqual(after.rules._statement.members.at(-1), {
    type: 'PREC', value: -1, content: {type: 'SYMBOL', name: 'type_alias'},
  });
});

test('callable query types expose the owner and complete authored selector', () => {
  const parameter = after.rules.parameter_type_query;
  assert.equal(parameter.value, 31);
  assert.deepEqual(parameter.content.members.map(member => member.name ?? member.value),
    ['paramof', '_markup_after_28', 'owner', '_markup_after_2c', 'selector', ')']);
  assert.equal(parameter.content.members[2].content.name, '_type');
  assert.equal(parameter.content.members[4].content.name, '_expression');
  assert.deepEqual(after.rules.return_type_query.content.members.map(member => member.name ?? member.value),
    ['returnof', '_markup_after_28', 'owner', ')']);
});

test('query types compose with existing structural suffixes and leave expressions untouched', () => {
  assert.deepEqual(after.rules._structural_type_base.members.slice(0, -2), before.rules._structural_type_base.members);
  assert.deepEqual(after.rules._structural_type_base.members.slice(-2).map(member => member.name),
    ['parameter_type_query', 'return_type_query']);
  const changed = new Set(['_statement', '_structural_type_base']);
  for (const [name, rule] of Object.entries(before.rules)) {
    if (!changed.has(name)) assert.deepEqual(after.rules[name], rule, name);
  }
  assert.deepEqual(after.externals.slice(0, -2), before.externals);
  assert.deepEqual(after.externals.slice(-2).map(rule => rule.name),
    ['_paramof_keyword', '_returnof_keyword']);
  assert.deepEqual(after.precedences, before.precedences);
  assert.deepEqual(after.conflicts, before.conflicts);
});

test('contextual query tokens retain ordinary type names and scanner token IDs', () => {
  for (const [name, token] of [
    ['parameter_type_query', '_paramof_keyword'],
    ['return_type_query', '_returnof_keyword'],
  ]) {
    const keyword = after.rules[name].content.members[0];
    assert.equal(keyword.type, 'ALIAS');
    assert.equal(keyword.content.name, token);
    assert.equal(keyword.named, false);
  }
  const scanner = fs.readFileSync(path.join(__dirname, '..', 'src', 'scanner.c'), 'utf8');
  const tokens = scanner.match(/enum TokenType\s*\{([\s\S]*?)\};/)[1]
    .replace(/\/\/[^\n]*/g, '')
    .split(',').map(token => token.trim()).filter(Boolean);
  const externalNames = after.externals.map(rule => rule.name.replace(/^_/, '').toUpperCase());
  assert.deepEqual(tokens.slice(0, -1), externalNames);
  assert.equal(tokens.at(-1), 'SCANNER_TOKEN_COUNT');
});
