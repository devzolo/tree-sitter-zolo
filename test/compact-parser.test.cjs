'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const {
  MAX_PARSER_BYTES, PACK_WIDTH, TokenDigest, compactSource,
  lexicalTokenDigest, fileTokenDigest, includedTokenDigest, assertPublishedSize, assertPublishedTree, compactFile,
} = require('../compact-parser.cjs');

function preserves(source, width) {
  const result = compactSource(source, width);
  assert.deepEqual(lexicalTokenDigest(result), lexicalTokenDigest(source));
  return result;
}

test('generated tables compact deterministically without changing C tokens', () => {
  const source = 'static const unsigned short table [ 3 ] = {\n'
    + '  [ 0 ] = 10,\n  [ 1 ] = 20,\n  [ 2 ] = 30,\n};\n'
    + 'static void parse ( void ) {\n  unsigned value = table [ 1 ];\n  consume ( value );\n}\n';
  const result = preserves(source);
  assert.ok(result.length < source.length);
  assert.match(result, /table\[3\]=\{\[0\]=10,\[1\]=20,\[2\]=30,\};/);
  assert.equal(compactSource(source), result);
  assert.equal(compactSource(result), result, 'a compact generated table stays compact');
  assert.ok(result.split('\n').every(line => line.length <= PACK_WIDTH));
});

test('all physical lines of multiline comments, strings and character literals remain verbatim', () => {
  const protectedLines = [
    '  /* opening { [ =  */ /* second opening\n',
    '    token [ 2 ] = 3;   \n',
    '    quoted-looking "a quote" // still a block comment\n',
    '    last body ( token );\n',
    '  */\n',
    'const char *text = "/* not a comment */ // \\\" quoted";\n',
    "const int quote = '\\'';\n",
    "const int slash = '\\\\';\n",
    '  // comment with [ 2 ] = { 3 }\n',
  ];
  // The first fixture deliberately closes/reopens a comment on the same line;
  // quote-looking contents cannot reset an active C block-comment state.
  const source = protectedLines.join('') + '  value [ 0 ] = 42;\n';
  const result = preserves(source);
  assert.ok(result.startsWith(protectedLines.join('')));
  assert.ok(result.endsWith('value[0]=42;\n'));
});

test('generated quoted symbol names and lexer comparisons retain their raw C tokens', () => {
  const source = '  [sym_name] = "name with spaces",\n'
    + '  [sym_quote] = "\\\"quote\\\"",\n'
    + "    if (lookahead >= 'a' && lookahead <= 'z') ADVANCE(42);\n"
    + "    if (lookahead == '\\\\') ADVANCE(43);\n";
  const result = preserves(source);
  assert.ok(result.length < source.length * 0.85, 'generated quotes must not preserve unrelated indentation/operator padding');
  assert.ok(result.includes('"name with spaces"'));
  assert.ok(result.includes("'\\\\'"));
  assert.equal(compactSource(result), result);
});

test('directives and complete backslash continuations preserve raw CRLF bytes', () => {
  const directive = '  #define TABLE(X) \\\r\n'
    + '    X ( [ 0 ] = 42 ) \\\r\n'
    + '    X ( [ 1 ] = 99 )\r\n';
  const ordinarySplice = '  value \\\r\n    [ 0 ] = 1;\r\n';
  const source = '  first ( );\r\n' + directive + ordinarySplice + '  last ( );\r\n';
  const result = preserves(source);
  assert.ok(result.includes(directive));
  assert.ok(result.includes(ordinarySplice));
  assert.equal(result, 'first();\n' + directive + ordinarySplice + 'last();\n');
});

test('line-spliced comment openers protect subsequent physical comment lines', () => {
  const comment = ' /\\\n* comment begins before this newline\n'
    + '  looks [ 0 ] = like_code;\n'
    + ' */\n';
  const source = '  first();\n' + comment + '  last ( );\n';
  const result = preserves(source);
  assert.ok(result.includes(comment));
  assert.ok(result.endsWith('last();\n'));
});

test('compound operators, pp-numbers and separated operators retain exact token boundaries', () => {
  const source = 'a + +b;\na - -b;\na < <b;\na >> = b;\n'
    + 'a += b;\na == b;\na != b;\na <<= b;\na && b;\na -> b;\n'
    + 'a = 0x1.fp+2;\na = 1e-3;\na = .25;\n'
    + 'a = b;\nvalue = (next);\n';
  const result = preserves(source, 80);
  assert.ok(result.includes('a + +b;'));
  assert.ok(result.includes('a >> = b;'));
  assert.notDeepEqual(lexicalTokenDigest('a + +b;'), lexicalTokenDigest('a++ +b;'));
  assert.notDeepEqual(lexicalTokenDigest('a < <b;'), lexicalTokenDigest('a << b;'));
  assert.notDeepEqual(lexicalTokenDigest('1e - 3;'), lexicalTokenDigest('1e-3;'));
});

test('packing inserts spaces between identifier lines and preserves original oversized lines', () => {
  const source = '  unsigned\n  value\n  = 1;\n  consume ( value );\n';
  const result = preserves(source, 30);
  assert.ok(result.startsWith('unsigned value = 1;'));
  assert.ok(result.split('\n').every(line => line.length <= 30));
  const long = `  ${'long_identifier_'.repeat(10)} = 1;\n`;
  assert.equal(preserves(long, 30), long);
});

test('streaming token proofs agree at every small chunk boundary', () => {
  const source = '#define X(v) \\\r\n  ((v) += 1)\r\n'
    + '/* block\n comment */ static const char *s = u8"\\\"/* escaped */";\n'
    + "L'\\''; U'\\\\'; // comment\\\n continued\n"
    + 'value = 0x1.fp+2 + .25; value >>= 1; other %:%: value;\n';
  const expected = lexicalTokenDigest(source);
  for (let width = 1; width <= 19; width++) {
    const proof = new TokenDigest();
    for (let start = 0; start < source.length; start += width) proof.write(source.slice(start, start + width));
    assert.deepEqual(proof.finish(), expected, `chunk width ${width}`);
  }
  preserves(source);
});

test('C lexical proof rejects unterminated protected source', () => {
  assert.throws(() => lexicalTokenDigest('/* open'), /unterminated C block comment/);
  assert.throws(() => lexicalTokenDigest('"open'), /unterminated C quoted literal/);
});

test('CLI compacts a separate output with a checked lexical proof and leaves its source intact', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'zolo-parser-compact-'));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const input = path.join(dir, 'original.c');
  const output = path.join(dir, 'compact.c');
  const source = 'static int table [ ] = {\n  [ 0 ] = 1,\n  [ 1 ] = 2,\n};\n';
  await fs.writeFile(input, source);
  const result = spawnSync(process.execPath, [path.join(__dirname, '..', 'compact-parser.cjs'), input, output], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
  const report = JSON.parse(result.stdout);
  assert.ok(report.afterBytes < report.beforeBytes);
  assert.deepEqual(await fileTokenDigest(input), await fileTokenDigest(output));
  assert.equal(await fs.readFile(input, 'utf8'), source);
  const compacted = await fs.readFile(output, 'utf8');
  await compactFile(output);
  assert.equal(await fs.readFile(output, 'utf8'), compacted);
});

test('publication guard rejects exactly 100 MiB and failed proof/size checks never replace an output', async t => {
  assert.equal(MAX_PARSER_BYTES, 104857600);
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'zolo-parser-guard-'));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const boundary = path.join(dir, 'boundary.c');
  const handle = await fs.open(boundary, 'w');
  await handle.truncate(MAX_PARSER_BYTES);
  await handle.close();
  await assert.rejects(assertPublishedSize(boundary), /publication requires less than 104857600 bytes/);
  await fs.truncate(boundary, MAX_PARSER_BYTES - 1);
  assert.equal(await assertPublishedSize(boundary), MAX_PARSER_BYTES - 1);
  const input = path.join(dir, 'source.c');
  const output = path.join(dir, 'output.c');
  await fs.writeFile(input, 'int value = 42;\n');
  await fs.writeFile(output, 'old output');
  await assert.rejects(compactFile(input, output, { limit: 4 }), /publication requires/);
  assert.equal(await fs.readFile(input, 'utf8'), 'int value = 42;\n');
  assert.equal(await fs.readFile(output, 'utf8'), 'old output');
  await fs.writeFile(input, '/* unterminated');
  await assert.rejects(compactFile(input, output), /unterminated C block comment/);
  assert.equal(await fs.readFile(output, 'utf8'), 'old output');
  assert.ok(!(await fs.readdir(dir)).some(name => name.includes('.compact-')));
  await assert.rejects(compactFile(path.join(dir, 'missing.c'), output), { code: 'ENOENT' });
  assert.equal(await fs.readFile(output, 'utf8'), 'old output');
  assert.ok(!(await fs.readdir(dir)).some(name => name.includes('.compact-')));
});

test('oversized generated tables partition into checked C includes without altering the expanded token stream', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'zolo-parser-includes-'));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const input = path.join(dir, 'original.c');
  const output = path.join(dir, 'published', 'parser.c');
  // The conditional/continued directive and multiline comment must each stay
  // in a single part, while the large ordinary table can span included files.
  const source = '#if defined(TEST)\n#define TEST_VALUE(x) \\\n  ((x) + 1)\n#endif\n'
    + '/* protected\n comment */\nstatic const unsigned table[600] = {\n'
    + Array.from({ length: 600 }, (_, i) => `  [${i}] = ${i},\n`).join('')
    + '};\nint answer(void) { return table[42]; }\n';
  await fs.writeFile(input, source);
  const result = await compactFile(input, output, { limit: 2048, chunkBytes: 1200, split: true });
  assert.ok(result.parts.length > 1);
  assert.ok(result.afterBytes < 2048);
  const entrypoint = await fs.readFile(output, 'utf8');
  const includes = Array.from(entrypoint.matchAll(/^#include "([^"/\\]+\.inc)"$/gm), match => match[1]);
  assert.deepEqual(includes, result.parts.map(part => part.name));
  const files = includes.map(name => path.join(path.dirname(output), name));
  assert.deepEqual(await includedTokenDigest(files), await fileTokenDigest(input));
  assert.ok((await assertPublishedTree(path.dirname(output), 2048)).every(artifact => artifact.bytes < 2048));
  const firstRun = await Promise.all(files.map(file => fs.readFile(file, 'utf8')));
  const repeated = await compactFile(input, output, { limit: 2048, chunkBytes: 1200, split: true });
  assert.deepEqual(repeated, result, 'parts and complete lexical proof must be deterministic');
  assert.deepEqual(await Promise.all(files.map(file => fs.readFile(file, 'utf8'))), firstRun);
  assert.ok(firstRun.some(part => part.includes('#if defined(TEST)\n#define TEST_VALUE(x) \\\n  ((x) + 1)\n#endif\n')));
});

test('partitioning rejects context-dependent macros and oversized protected groups before replacing the entrypoint', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'zolo-parser-include-guard-'));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const input = path.join(dir, 'original.c');
  const output = path.join(dir, 'parser.c');
  const padding = 'static int values[] = {' + '1,'.repeat(800) + '0};\n';
  await fs.writeFile(output, 'old parser');
  for (const sensitive of ['const int line = __LINE__;\n', '#define LOCATION __FILE__\n']) {
    await fs.writeFile(input, sensitive + padding);
    await assert.rejects(compactFile(input, output, { limit: 1024, split: true }), /context-sensitive C macro/);
    assert.equal(await fs.readFile(output, 'utf8'), 'old parser');
  }
  await fs.writeFile(input, '#if TEST\n' + padding + '#endif\n');
  await assert.rejects(compactFile(input, output, { limit: 1024, split: true }), /protected C group exceeds/);
  assert.equal(await fs.readFile(output, 'utf8'), 'old parser');
  assert.ok(!(await fs.readdir(dir)).some(name => name.includes('.compact-') || name.endsWith('.inc')));
});

test('npm, bash CI and official Rust publishing all use the same published compactor', async () => {
  const root = path.join(__dirname, '..');
  const manifest = JSON.parse(await fs.readFile(path.join(root, 'package.json'), 'utf8'));
  const lock = JSON.parse(await fs.readFile(path.join(root, 'package-lock.json'), 'utf8'));
  assert.ok(manifest.files.includes('compact-parser.cjs'));
  assert.ok(manifest.files.includes('src/**'), 'npm must ship the generated includes and parser headers');
  assert.equal(manifest.devDependencies['tree-sitter-cli'], '0.26.8');
  assert.equal(lock.packages[''].devDependencies['tree-sitter-cli'], '0.26.8');
  assert.equal(lock.packages['node_modules/tree-sitter-cli'].version, '0.26.8');
  assert.match(manifest.scripts.build, /^node --test test\/grammar\.test\.cjs && tree-sitter generate && node compact-parser\.cjs && node-gyp build$/);
  const bash = await fs.readFile(path.join(root, 'scripts', 'publish.sh'), 'utf8');
  const rust = await fs.readFile(path.join(root, '..', 'xtask', 'src', 'tree_sitter_sync.rs'), 'utf8');
  const cargo = await fs.readFile(path.join(root, 'Cargo.toml'), 'utf8');
  const pythonManifest = await fs.readFile(path.join(root, 'MANIFEST.in'), 'utf8');
  assert.match(cargo, /"src\/\*\*"/);
  assert.match(pythonManifest, /recursive-include src[^\n]*\*\.inc/);
  for (const buildFile of ['binding.gyp', 'pyproject.toml', 'setup.py', 'MANIFEST.in', 'Package.swift']) {
    assert.ok(rust.includes(`"${buildFile}"`), `Rust publication must include ${buildFile}`);
    assert.ok(bash.includes(`  ${buildFile}\n`), `bash publication must include ${buildFile}`);
  }
  assert.match(bash, /PUBLISH_SET=\([\s\S]*?\n  compact-parser\.cjs\n/);
  assert.match(bash, /generate \)\s*\n[\s\S]*?node "\$SRC_DIR\/compact-parser\.cjs" "\$SRC_DIR\/src\/parser\.c"/);
  assert.match(bash, /--check-size "\$WORK_DIR\/src\/parser\.c"/);
  assert.match(bash, /TREE_SITTER_CLI_VERSION="0\.26\.8"/);
  assert.match(bash, /"\$TS_VERSION" != "\$TREE_SITTER_CLI_VERSION"/);
  assert.match(rust, /const PUBLISH_SET:[\s\S]*?"compact-parser\.cjs"/);
  assert.match(rust, /arg\("generate"\)[\s\S]*?Command::new\("node"\)[\s\S]*?arg\(gen_dir.join\("compact-parser\.cjs"\)\)/);
  assert.match(rust, /enforce_parser_size\(&mirror_dir.join\("src"\).join\("parser.c"\)\)/);
});
