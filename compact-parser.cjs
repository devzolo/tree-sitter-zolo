#!/usr/bin/env node
'use strict';

// Whitespace-only compaction of pinned tree-sitter output. Do not minimize C
// expressions or edit generated tables. Protected source is copied verbatim.
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { once } = require('node:events');

const MAX_PARSER_BYTES = 100 * 1024 * 1024;
const PACK_WIDTH = 1000;
const PUNCTUATORS = ['%:%:', '>>=', '<<=', '...', '->', '++', '--', '<<', '>>', '<=', '>=', '==', '!=', '&&', '||', '*=', '/=', '%=', '+=', '-=', '&=', '^=', '|=', '##', '<:', ':>', '<%', '%>', '%:'];

function compactPlain(source) {
  return source
    .replace(/[ \t]*([,;\[\]{}()])[ \t]*/g, '$1')
    .replace(/([A-Za-z0-9_\])}])[ \t]*=[ \t]*(?=[A-Za-z0-9_({])/g, '$1=')
    .replace(/[ \t]*(>=|<=|==|!=|&&|\|\|)[ \t]*/g, '$1');
}

function compactGeneratedQuotedLine(line) {
  // Generated symbol names and lexer character comparisons account for a
  // large part of the parser. Keep every quoted preprocessing token exactly
  // as emitted; only compact its surrounding ordinary C whitespace. Comments,
  // directives, continuations and authored/protected lines keep the original
  // physical-line path below. The full-file lexical digest proves the result.
  let result = '', start = 0;
  const quoted = /"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g;
  for (const match of line.matchAll(quoted)) {
    const plain = compactPlain(line.slice(start, match.index))
      .replace(/([A-Za-z0-9_\])}])[ \t]*=[ \t]*$/, '$1=');
    result += plain + match[0];
    start = match.index + match[0].length;
  }
  return result + compactPlain(line.slice(start));
}

class Compactor {
  constructor(width = PACK_WIDTH) {
    this.width = width;
    this.packed = '';
    this.blockComment = false;
    this.quote = null;
    this.logicalLine = '';
    this.continuation = false;
  }

  flush() {
    const result = this.packed ? `${this.packed}\n` : '';
    this.packed = '';
    return result;
  }

  scanLogicalLine(line) {
    for (let index = 0; index < line.length; index++) {
      const current = line[index];
      const next = line[index + 1];
      if (this.blockComment) {
        if (current === '*' && next === '/') {
          this.blockComment = false;
          index++;
        }
      } else if (this.quote) {
        if (current === '\\') index++;
        else if (current === this.quote) this.quote = null;
      } else if (current === '/' && next === '*') {
        this.blockComment = true;
        index++;
      } else if (current === '/' && next === '/') {
        break;
      } else if (current === '"' || current === "'") {
        this.quote = current;
      }
    }
  }

  push(raw) {
    const newline = raw.endsWith('\n');
    const body = newline ? raw.slice(0, raw.endsWith('\r\n') ? -2 : -1) : raw;
    const line = body.trimStart();
    const protectedBefore = this.blockComment || this.quote !== null || this.continuation;
    const splice = newline && body.endsWith('\\');
    const generatedQuoted = /^(?:\[[A-Za-z_][A-Za-z_0-9]*\][ \t]*=|(?:else[ \t]+)?if[ \t]*\(lookahead\b)/.test(line);
    const eligible = !protectedBefore && line.length > 0 && !line.startsWith('#')
      && (!/["'\\]/.test(line) || generatedQuoted && !splice)
      && !line.includes('//') && !line.includes('/*') && !line.includes('*/');

    // C phase-2 splicing happens before comment/string recognition. Preserve
    // every physical line in the logical group, including directive bodies.
    this.logicalLine += splice ? body.slice(0, -1) : body;
    if (!splice) {
      if (protectedBefore || /["'\\/]/.test(this.logicalLine)) this.scanLogicalLine(this.logicalLine);
      this.logicalLine = '';
    }
    this.continuation = splice;

    if (!eligible) return this.flush() + raw;
    const compact = generatedQuoted ? compactGeneratedQuotedLine(line) : compactPlain(line);
    // Never split an original long C line or invent a token boundary within it.
    if (compact.length > this.width) return this.flush() + raw;
    const separator = /[,;\[\]{}()]$/.test(this.packed) ? '' : ' ';
    if (this.packed && this.packed.length + separator.length + compact.length > this.width) {
      const result = this.flush();
      this.packed = compact;
      return result;
    }
    this.packed += this.packed ? separator + compact : compact;
    return '';
  }

  finish() {
    return this.flush();
  }
}

function compactSource(source, width = PACK_WIDTH) {
  const compactor = new Compactor(width);
  let result = '';
  for (const line of source.match(/[^\n]*\n|[^\n]+$/g) || []) result += compactor.push(line);
  return result + compactor.finish();
}

// Incremental C preprocessing-token proof. Whitespace is ignored, while raw
// comments, quoted literals and complete logical directives are hashed exactly.
// Buffering is limited to an incomplete token, not the generated parser/table.
class TokenDigest {
  constructor({ rejectContextMacros = false } = {}) {
    this.hash = crypto.createHash('sha256');
    this.pending = '';
    this.lineStart = true;
    this.tokens = 0;
    this.rejectContextMacros = rejectContextMacros;
  }

  token(kind, text) {
    if (this.rejectContextMacros && (kind === 'identifier' || kind === 'directive')
      && /\b(?:__FILE__|__FILE_NAME__|__LINE__|__BASE_FILE__|__INCLUDE_LEVEL__|__TIMESTAMP__|__builtin_FILE|__builtin_LINE)\b/.test(text)) {
      throw new Error(`cannot partition context-sensitive C macro ${text}`);
    }
    this.hash.update(`${kind}:${Buffer.byteLength(text)}:`);
    this.hash.update(text);
    this.tokens++;
  }

  write(chunk, final = false) {
    const source = this.pending + chunk;
    let index = 0;
    while (index < source.length) {
      const start = index;
      const current = source[index];
      if (/[ \t\r\n\v\f]/.test(current)) {
        if (current === '\n') this.lineStart = true;
        index++;
        continue;
      }
      if (current === '#' && this.lineStart) {
        let end = index;
        while (true) {
          end = source.indexOf('\n', end);
          if (end < 0) break;
          const before = source[end - 1] === '\r' ? end - 2 : end - 1;
          if (source[before] !== '\\') break;
          end++;
        }
        if (end < 0 && !final) break;
        index = end < 0 ? source.length : end + 1;
        this.token('directive', source.slice(start, index));
        this.lineStart = end >= 0;
        continue;
      }
      if (source.startsWith('/*', index)) {
        const end = source.indexOf('*/', index + 2);
        if (end < 0 && !final) break;
        if (end < 0) throw new Error('unterminated C block comment');
        index = end + 2;
        const text = source.slice(start, index);
        this.token('comment', text);
        if (text.includes('\n')) this.lineStart = true;
        continue;
      }
      if (source.startsWith('//', index)) {
        let end = index;
        while (true) {
          end = source.indexOf('\n', end);
          if (end < 0) break;
          const before = source[end - 1] === '\r' ? end - 2 : end - 1;
          if (source[before] !== '\\') break;
          end++;
        }
        if (end < 0 && !final) break;
        index = end < 0 ? source.length : end + 1;
        this.token('comment', source.slice(start, index));
        this.lineStart = end >= 0;
        continue;
      }
      const prefix = /^(?:u8|u|U|L)?(["'])/.exec(source.slice(index, index + 4));
      if (prefix) {
        const quote = prefix[1];
        index += prefix[0].length;
        let closed = false;
        while (index < source.length) {
          if (source[index] === '\\') {
            index += source[index + 1] === '\r' && source[index + 2] === '\n' ? 3 : 2;
          } else if (source[index++] === quote) {
            closed = true;
            break;
          }
        }
        if (!closed && !final) { index = start; break; }
        if (!closed) throw new Error('unterminated C quoted literal');
        this.token('quoted', source.slice(start, index));
      } else if (/[A-Za-z_$\u0080-\uffff]/.test(current)) {
        index++;
        while (index < source.length && /[A-Za-z_0-9$\u0080-\uffff]/.test(source[index])) index++;
        if (index === source.length && !final) { index = start; break; }
        this.token('identifier', source.slice(start, index));
      } else if (/[0-9]/.test(current) || (current === '.' && /[0-9]/.test(source[index + 1] || ''))) {
        index++;
        while (index < source.length) {
          const value = source[index];
          if (/[A-Za-z_0-9.]/.test(value) || (/[+-]/.test(value) && /[eEpP]/.test(source[index - 1]))) index++;
          else break;
        }
        if (index === source.length && !final) { index = start; break; }
        this.token('number', source.slice(start, index));
      } else {
        if (!final && source.length - index < 4) break;
        const operator = PUNCTUATORS.find(value => source.startsWith(value, index)) || current;
        index += operator.length;
        this.token('punctuator', operator);
      }
      this.lineStart = false;
    }
    this.pending = source.slice(index);
    if (final && this.pending.length) throw new Error('unconsumed C source');
  }

  finish() {
    this.write('', true);
    return { sha256: this.hash.digest('hex'), tokens: this.tokens };
  }
}

function lexicalTokenDigest(source) {
  const proof = new TokenDigest();
  proof.write(source);
  return proof.finish();
}

async function fileTokenDigest(file, options) {
  const proof = new TokenDigest(options);
  for await (const chunk of fs.createReadStream(file, { encoding: 'utf8' })) proof.write(chunk);
  return proof.finish();
}

async function assertPublishedSize(file, limit = MAX_PARSER_BYTES) {
  const bytes = (await fsp.stat(file)).size;
  if (bytes >= limit) throw new Error(`${file} is ${bytes} bytes; publication requires less than ${limit} bytes (100 MiB).`);
  return bytes;
}

async function assertPublishedTree(directory, limit = MAX_PARSER_BYTES) {
  const artifacts = [];
  for (const item of await fsp.readdir(directory, { withFileTypes: true })) {
    const file = path.join(directory, item.name);
    if (item.isDirectory()) artifacts.push(...await assertPublishedTree(file, limit));
    else if (item.isFile()) artifacts.push({ file, bytes: await assertPublishedSize(file, limit) });
    else throw new Error(`publication does not admit non-file artifact ${file}`);
  }
  return artifacts;
}

async function* physicalLines(file) {
  let pending = '';
  for await (const chunk of fs.createReadStream(file, { encoding: 'utf8' })) {
    pending += chunk;
    let start = 0, end;
    while ((end = pending.indexOf('\n', start)) >= 0) {
      yield pending.slice(start, end + 1);
      start = end + 1;
    }
    pending = pending.slice(start);
  }
  if (pending) yield pending;
}

// C #include performs token insertion inside array/function bodies too, but an
// included file cannot close its parent's conditional group. Only partition at
// complete physical/logical lines outside comments/quotes and #if groups.
class IncludeBoundary {
  constructor() {
    this.scanner = new Compactor();
    this.depth = 0;
    this.logical = '';
    this.directive = false;
    this.continuation = false;
  }

  push(raw) {
    const newline = raw.endsWith('\n');
    const body = newline ? raw.slice(0, raw.endsWith('\r\n') ? -2 : -1) : raw;
    if (!this.logical) this.directive = !this.scanner.blockComment && !this.scanner.quote && /^\s*#/.test(body);
    const splice = newline && body.endsWith('\\');
    this.logical += splice ? body.slice(0, -1) : body;
    if (!splice) {
      if (this.directive) {
        const command = /^\s*#\s*([A-Za-z_]+)/.exec(this.logical)?.[1];
        if (command === 'if' || command === 'ifdef' || command === 'ifndef') this.depth++;
        if (command === 'endif') this.depth--;
        if (this.depth < 0) throw new Error('unmatched C conditional directive');
      }
      this.scanner.scanLogicalLine(this.logical);
      this.logical = '';
    }
    this.continuation = splice;
    return newline && !splice && !this.scanner.blockComment && !this.scanner.quote && this.depth === 0;
  }

  finish() {
    if (this.depth || this.continuation || this.scanner.blockComment || this.scanner.quote) {
      throw new Error('unterminated protected C group cannot be partitioned');
    }
  }
}

async function includedTokenDigest(parts) {
  const proof = new TokenDigest();
  for (const part of parts) {
    for await (const chunk of fs.createReadStream(part, { encoding: 'utf8' })) proof.write(chunk);
  }
  return proof.finish();
}

async function partitionCheckedFile(file, output, original, { limit, chunkBytes }) {
  if (!Number.isSafeInteger(chunkBytes) || chunkBytes <= 0 || chunkBytes >= limit) {
    throw new Error('chunkBytes must be a positive integer smaller than the publication limit');
  }
  // The generated parser uses no location-dependent macros. Reject them if a
  // future generator introduces one rather than quietly changing its meaning.
  await fileTokenDigest(file, { rejectContextMacros: true });
  const boundary = new IncludeBoundary();
  const parts = [];
  let sink, bytes = 0, writeError, previousSafe = true;
  const closePart = async () => {
    if (!sink) return;
    sink.end();
    await once(sink, 'finish');
    if (writeError) throw writeError;
    await assertPublishedSize(parts.at(-1).temporary, limit);
    sink = undefined;
  };
  try {
    for await (const line of physicalLines(file)) {
      const lineBytes = Buffer.byteLength(line);
      if (sink && previousSafe && bytes + lineBytes >= limit) await closePart();
      if (!sink) {
        const name = `${path.basename(output, path.extname(output))}-${original.sha256}-${String(parts.length).padStart(3, '0')}.inc`;
        parts.push({ name, file: path.join(path.dirname(output), name), temporary: `${file}-part-${parts.length}`, bytes: 0 });
        sink = fs.createWriteStream(parts.at(-1).temporary, { encoding: 'utf8', flags: 'wx' });
        writeError = undefined;
        sink.on('error', error => { writeError = error; });
        bytes = 0;
      }
      bytes += lineBytes;
      if (bytes >= limit) throw new Error(`protected C group exceeds publication limit ${limit}`);
      if (writeError) throw writeError;
      if (!sink.write(line)) await once(sink, 'drain');
      const safe = boundary.push(line);
      previousSafe = safe;
      parts.at(-1).bytes = bytes;
      if (bytes >= chunkBytes && safe) await closePart();
    }
    boundary.finish();
    await closePart();
    const expanded = await includedTokenDigest(parts.map(part => part.temporary));
    if (expanded.sha256 !== original.sha256 || expanded.tokens !== original.tokens) {
      throw new Error(`C include expansion proof failed: ${JSON.stringify({ original, expanded })}`);
    }
    return parts;
  } catch (error) {
    if (sink) {
      sink.destroy();
      if (!sink.closed) await new Promise(resolve => sink.once('close', resolve));
    }
    for (const part of parts) await fsp.unlink(part.temporary).catch(() => {});
    throw error;
  }
}

async function compactFile(input, output = input, {
  width = PACK_WIDTH, limit = MAX_PARSER_BYTES, split = false,
  chunkBytes = Math.min(32 * 1024 * 1024, Math.floor(limit / 2)),
} = {}) {
  input = path.resolve(input);
  output = path.resolve(output);
  await fsp.mkdir(path.dirname(output), { recursive: true });
  const temporary = `${output}.compact-${process.pid}-${crypto.randomBytes(6).toString('hex')}`;
  const sink = fs.createWriteStream(temporary, { encoding: 'utf8', flags: 'wx' });
  // Install the error listener immediately, including before the first write.
  let writeError;
  sink.on('error', error => { writeError = error; });
  const compactor = new Compactor(width);
  let pending = '';
  let parts = [], stub;
  try {
    for await (const chunk of fs.createReadStream(input, { encoding: 'utf8' })) {
      pending += chunk;
      let start = 0;
      let end;
      while ((end = pending.indexOf('\n', start)) >= 0) {
        const data = compactor.push(pending.slice(start, end + 1));
        if (writeError) throw writeError;
        if (data && !sink.write(data)) await once(sink, 'drain');
        start = end + 1;
      }
      pending = pending.slice(start);
    }
    const tail = (pending ? compactor.push(pending) : '') + compactor.finish();
    if (writeError) throw writeError;
    if (tail && !sink.write(tail)) await once(sink, 'drain');
    sink.end();
    await once(sink, 'finish');
    const compactedBytes = (await fsp.stat(temporary)).size;
    const original = await fileTokenDigest(input);
    const compacted = await fileTokenDigest(temporary);
    if (original.sha256 !== compacted.sha256 || original.tokens !== compacted.tokens) {
      throw new Error(`C token preservation proof failed: ${JSON.stringify({ original, compacted })}`);
    }
    const beforeBytes = (await fsp.stat(input)).size;
    if (compactedBytes < limit) {
      await fsp.rename(temporary, output);
      return { beforeBytes, afterBytes: compactedBytes, ...original, output, parts: [] };
    }
    if (!split) await assertPublishedSize(temporary, limit);
    parts = await partitionCheckedFile(temporary, output, original, { limit, chunkBytes });
    stub = `${temporary}-stub`;
    await fsp.writeFile(stub, parts.map(part => `#include "${part.name}"\n`).join(''), { flag: 'wx' });
    const afterBytes = await assertPublishedSize(stub, limit);
    // Each immutable part is content-addressed. Publish them before the small
    // entrypoint; a failed proof never replaces an existing parser.c.
    for (const part of parts) {
      try {
        const existing = await fsp.readFile(part.file);
        const candidate = await fsp.readFile(part.temporary);
        if (!existing.equals(candidate)) throw new Error(`immutable C part collision: ${part.file}`);
        await fsp.unlink(part.temporary);
      } catch (error) {
        if (error.code !== 'ENOENT') throw error;
        await fsp.rename(part.temporary, part.file);
      }
    }
    await fsp.rename(stub, output);
    return { beforeBytes, compactedBytes, afterBytes, ...original, output,
      parts: parts.map(({ name, bytes }) => ({ name, bytes })) };
  } finally {
    sink.destroy();
    if (!sink.closed) await new Promise(resolve => sink.once('close', resolve));
    await fsp.unlink(temporary).catch(error => { if (error.code !== 'ENOENT') throw error; });
    if (stub) await fsp.unlink(stub).catch(error => { if (error.code !== 'ENOENT') throw error; });
    for (const part of parts) await fsp.unlink(part.temporary).catch(error => { if (error.code !== 'ENOENT') throw error; });
  }
}

module.exports = { MAX_PARSER_BYTES, PACK_WIDTH, Compactor, TokenDigest, compactSource, lexicalTokenDigest, fileTokenDigest, includedTokenDigest, assertPublishedSize, assertPublishedTree, compactFile };

if (require.main === module) {
  const args = process.argv.slice(2);
  const defaultInput = path.join(__dirname, 'src', 'parser.c');
  const operation = args[0] === '--check-size'
    ? (args.length <= 2 ? assertPublishedTree(path.dirname(path.resolve(args[1] || defaultInput))).then(artifacts => ({ artifacts })) : Promise.reject(new Error('usage: compact-parser.cjs --check-size [PARSER]')))
    : (args.length <= 2 && args.every(arg => !arg.startsWith('--'))
      ? compactFile(args[0] || defaultInput, args[1] || args[0] || defaultInput, { split: true })
      : Promise.reject(new Error('usage: compact-parser.cjs [INPUT [OUTPUT]]')));
  operation.then(result => console.log(JSON.stringify(result))).catch(error => {
    console.error(`compact-parser: ${error.message}`);
    process.exitCode = 1;
  });
}
