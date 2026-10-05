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
    const eligible = !protectedBefore && line.length > 0 && !line.startsWith('#')
      && !/["'\\]/.test(line) && !line.includes('//') && !line.includes('/*') && !line.includes('*/');

    // C phase-2 splicing happens before comment/string recognition. Preserve
    // every physical line in the logical group, including directive bodies.
    this.logicalLine += splice ? body.slice(0, -1) : body;
    if (!splice) {
      if (protectedBefore || /["'\\/]/.test(this.logicalLine)) this.scanLogicalLine(this.logicalLine);
      this.logicalLine = '';
    }
    this.continuation = splice;

    if (!eligible) return this.flush() + raw;
    const compact = line
      .replace(/[ \t]*([,;\[\]{}()])[ \t]*/g, '$1')
      .replace(/([A-Za-z0-9_\])}])[ \t]*=[ \t]*(?=[A-Za-z0-9_({])/g, '$1=');
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
  constructor() {
    this.hash = crypto.createHash('sha256');
    this.pending = '';
    this.lineStart = true;
    this.tokens = 0;
  }

  token(kind, text) {
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

async function fileTokenDigest(file) {
  const proof = new TokenDigest();
  for await (const chunk of fs.createReadStream(file, { encoding: 'utf8' })) proof.write(chunk);
  return proof.finish();
}

async function assertPublishedSize(file, limit = MAX_PARSER_BYTES) {
  const bytes = (await fsp.stat(file)).size;
  if (bytes >= limit) throw new Error(`${file} is ${bytes} bytes; publication requires less than ${limit} bytes (100 MiB).`);
  return bytes;
}

async function compactFile(input, output = input, { width = PACK_WIDTH, limit = MAX_PARSER_BYTES } = {}) {
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
    const afterBytes = await assertPublishedSize(temporary, limit);
    const original = await fileTokenDigest(input);
    const compacted = await fileTokenDigest(temporary);
    if (original.sha256 !== compacted.sha256 || original.tokens !== compacted.tokens) {
      throw new Error(`C token preservation proof failed: ${JSON.stringify({ original, compacted })}`);
    }
    const beforeBytes = (await fsp.stat(input)).size;
    await fsp.rename(temporary, output);
    return { beforeBytes, afterBytes, ...original, output };
  } finally {
    sink.destroy();
    if (!sink.closed) await new Promise(resolve => sink.once('close', resolve));
    await fsp.unlink(temporary).catch(error => { if (error.code !== 'ENOENT') throw error; });
  }
}

module.exports = { MAX_PARSER_BYTES, PACK_WIDTH, Compactor, TokenDigest, compactSource, lexicalTokenDigest, fileTokenDigest, assertPublishedSize, compactFile };

if (require.main === module) {
  const args = process.argv.slice(2);
  const defaultInput = path.join(__dirname, 'src', 'parser.c');
  const operation = args[0] === '--check-size'
    ? (args.length <= 2 ? assertPublishedSize(args[1] || defaultInput).then(bytes => ({ bytes })) : Promise.reject(new Error('usage: compact-parser.cjs --check-size [PARSER]')))
    : (args.length <= 2 && args.every(arg => !arg.startsWith('--'))
      ? compactFile(args[0] || defaultInput, args[1] || args[0] || defaultInput)
      : Promise.reject(new Error('usage: compact-parser.cjs [INPUT [OUTPUT]]')));
  operation.then(result => console.log(JSON.stringify(result))).catch(error => {
    console.error(`compact-parser: ${error.message}`);
    process.exitCode = 1;
  });
}
