# tree-sitter-zolo

Official [tree-sitter](https://tree-sitter.github.io/) grammar for the
[Zolo](https://github.com/devzolo/zolo-lang) language — a modern, typed
language that compiles to Lua 5.1.

This package provides the incremental parser used by editors such as Neovim,
Helix, Zed, Emacs and GitHub for syntax highlighting, folding, structural
navigation and refactoring.

## Features covered

- Top-level items: `fn`, `fn*` (generator), `struct`, `enum`, `trait`, `impl`, `use`, `mod`, `type`, `newtype`, `const`, `const_assert`, `macro`, `on`
- Decorators: `@derive(Eq, Hash)`, `@get("/path")`, `@memoize`, etc.
- Expressions: literals, calls, methods, `?.`, `??`, `?`, `..`, `..=`, `|>`, `&.`, `as`, `is`
- Match with guards and patterns (struct, enum, tuple, array, range, or, `@` binding)
- Generics `<T: Bound>` and `where` clauses
- Async/await, `yield`, `spawn`, `every`, `after`, `timeout`, `sleep`
- Try/catch/finally and `defer`
- Lambdas `|x| expr` or `|x| { ... }`
- String interpolation: `"hello {name}"`
- Temporal literals: `5s`, `100ms`, `30min`, `2h`, `1d`, `1w`
- Tagged templates: `sql"..."`, `html"..."`, etc.
- Declarative macros (`macro name(args) { ... }`) and invocation `name!(...)`

## Repository layout

```text
tree-sitter-zolo/
├── grammar.js            # the grammar (source of truth)
├── src/                  # generated parser — regenerate with `tree-sitter generate`
├── queries/              # canonical queries (tree-sitter layout: queries/*.scm)
│   ├── highlights.scm
│   ├── folds.scm
│   ├── indents.scm
│   ├── injections.scm
│   └── locals.scm
├── editors/nvim/         # drop-in Neovim plugin (see editors/nvim/README.md)
├── test/corpus/          # parser test cases
└── bindings/             # Node and Rust bindings
```

> **Query layout note.** The grammar keeps queries flat in `queries/*.scm`
> (the tree-sitter convention, used by Helix/Zed/etc.). Neovim instead expects
> `queries/<lang>/*.scm`, so the bundled plugin ships them under
> `editors/nvim/queries/zolo/`, kept in sync from `queries/` by
> `editors/nvim/scripts/sync-queries.{ps1,sh}`.

## Editor integration — prerequisites

- **Neovim** 0.9+ (0.10+ recommended; the nvim-treesitter `main` branch tracks
  the latest Neovim).
- **A C compiler on `PATH`** to build the parser:

  | OS | Compiler |
  | --- | --- |
  | Windows | `gcc` via [MSYS2](https://www.msys2.org/) / mingw-w64, or `zig cc`, or MSVC `cl` |
  | macOS | `clang` (`xcode-select --install`) |
  | Linux | `gcc` or `clang` |

- The [tree-sitter CLI](https://github.com/tree-sitter/tree-sitter) and Node.js
  to generate `src/parser.c` from `grammar.js`. The generated parser is
  **git-ignored** (not committed), so you generate it once per clone — the
  sections below say where.

The parser has **no external scanner** (only `src/parser.c`), so building it is
a single-file compile.

## Neovim — quick start (recommended)

Use the bundled plugin in [`editors/nvim`](editors/nvim). It registers the
filetype, registers the parser (pointed at this local checkout) and ships the
queries. Load it from a **local path** with your plugin manager — replace the
path with your `zolo-lang` clone (forward slashes work on Windows):

```lua
-- lazy.nvim
{
  dir = "/path/to/zolo-lang/tree-sitter-zolo/editors/nvim",
  dependencies = { "nvim-treesitter/nvim-treesitter" },
  lazy = false, -- load at startup so :TSInstall zolo is available immediately
}
```

Then, inside Neovim:

```vim
:TSInstall zolo      " compile the parser from the local grammar
:edit example.zolo   " highlighting, folding and indentation are now active
```

Use `:TSInstall! zolo` to force a rebuild after you change the grammar.

**Generated parser.** `src/parser.c` is git-ignored (produced by
`tree-sitter generate`), so it is not committed. In a fresh clone, generate it
once before the first `:TSInstall`:

```bash
cd /path/to/zolo-lang/tree-sitter-zolo
tree-sitter generate
```

The bundled plugin points `:TSInstall zolo` at your local checkout and compiles
that file, so afterwards you only need a C compiler.

## Neovim — other plugin managers

The bundled plugin lives in a subdirectory of this monorepo, so install it by
**local path**. After loading it, run `:TSInstall zolo` once.

### packer.nvim

```lua
use {
  "/path/to/zolo-lang/tree-sitter-zolo/editors/nvim",
  after = "nvim-treesitter",
}
```

### vim-plug

```vim
Plug 'nvim-treesitter/nvim-treesitter', { 'do': ':TSUpdate' }
Plug '/path/to/zolo-lang/tree-sitter-zolo/editors/nvim'
```

### No plugin manager (runtimepath)

```lua
vim.opt.runtimepath:append("/path/to/zolo-lang/tree-sitter-zolo/editors/nvim")
```

or in Vimscript:

```vim
set runtimepath+=/path/to/zolo-lang/tree-sitter-zolo/editors/nvim
```

## Neovim — nvim-treesitter without the bundled plugin

If you only want the parser from GitHub (no local clone of the bundled plugin),
register it directly. nvim-treesitter has two branches with different APIs.

### nvim-treesitter `master` branch (classic)

```lua
local parser_config = require("nvim-treesitter.parsers").get_parser_configs()
parser_config.zolo = {
  install_info = {
    url = "https://github.com/devzolo/zolo-lang",
    branch = "main",
    location = "tree-sitter-zolo",   -- parser lives in this monorepo subdirectory
    files = { "src/parser.c" },
    requires_generate_from_grammar = true, -- src/parser.c is git-ignored; regenerate on install
  },
  filetype = "zolo",
}

vim.filetype.add({ extension = { zolo = "zolo" } })
```

### nvim-treesitter `main` branch (rewrite)

```lua
vim.api.nvim_create_autocmd("User", {
  pattern = "TSUpdate",
  callback = function()
    require("nvim-treesitter.parsers").zolo = {
      install_info = {
        url = "https://github.com/devzolo/zolo-lang",
        branch = "main",
        location = "tree-sitter-zolo",
        generate = true, -- src/parser.c is git-ignored; regenerate on install
        -- queries = "tree-sitter-zolo/queries", -- also install queries from the repo
      },
    }
  end,
})

vim.filetype.add({ extension = { zolo = "zolo" } })
```

Then `:TSInstall zolo`. Because the repository does not ship the generated
parser, both snippets tell nvim-treesitter to regenerate it on install — this
needs Node.js and the tree-sitter CLI available where you run `:TSInstall`.

**Queries.** On the `master` branch nvim-treesitter installs only the *parser*,
not the grammar's queries. Copy them into your config so Neovim can find them
under `queries/<lang>/` (the bundled plugin does this for you):

```bash
# from your Neovim config directory
#   Linux/macOS: ~/.config/nvim
#   Windows:     %LOCALAPPDATA%\nvim
mkdir -p queries/zolo
cp /path/to/zolo-lang/tree-sitter-zolo/queries/*.scm queries/zolo/
```

## Neovim — without nvim-treesitter (built-in tree-sitter)

Neovim 0.9+ can run the parser directly, no nvim-treesitter required.

1. Generate (in a fresh clone `src/parser.c` is git-ignored) and build the
   parser into your config's `parser/` directory:

   ```bash
   # Linux/macOS
   cd /path/to/zolo-lang/tree-sitter-zolo
   tree-sitter generate            # only if src/parser.c is missing
   cc -o ~/.config/nvim/parser/zolo.so -shared -Isrc src/parser.c -Os -fPIC
   ```

   ```powershell
   # Windows (gcc from MSYS2); Neovim still expects the .so suffix
   cd V:\path\to\zolo-lang\tree-sitter-zolo
   tree-sitter generate            # only if src/parser.c is missing
   gcc -o "$env:LOCALAPPDATA\nvim\parser\zolo.so" -shared -Isrc src/parser.c -Os
   ```

2. Provide the queries and filetype by loading the bundled
   [`editors/nvim`](editors/nvim) plugin (it ships `queries/zolo/`, `ftdetect/`
   and an `ftplugin` that calls `vim.treesitter.start()`), or do it by hand:
   copy `queries/*.scm` into `<config>/queries/zolo/`, add
   `vim.filetype.add({ extension = { zolo = "zolo" } })`, and start highlighting
   on the `zolo` filetype with `vim.treesitter.start()`.

## Neovim — verifying and troubleshooting

```vim
:checkhealth nvim-treesitter   " compiler + parser status (nvim-treesitter)
:TSInstallInfo zolo            " is the parser installed? (master branch)
:edit example.zolo
:Inspect                       " show the capture/highlight under the cursor
:InspectTree                   " show the live syntax tree
```

- **`no parser for 'zolo'` / no highlighting** — the parser is not installed or
  not built. Run `:TSInstall zolo` (or build it manually, above) and confirm a C
  compiler is on `PATH`.
- **Parser installs but nothing is highlighted** — the queries aren't on
  `runtimepath`. Use the bundled plugin or copy `queries/*.scm` to
  `<config>/queries/zolo/`.
- **`:set filetype?` is not `zolo`** — add
  `vim.filetype.add({ extension = { zolo = "zolo" } })` (the bundled plugin does
  this).
- **Windows: compiler errors** — install MSYS2 and add `…\msys64\mingw64\bin`
  to `PATH`, or use `zig cc`.
- **Grammar changed but highlights are stale** — rebuild with `:TSInstall! zolo`.

## Helix

Helix compiles grammars at build time. In your Helix `languages.toml`:

```toml
[[language]]
name = "zolo"
scope = "source.zolo"
file-types = ["zolo"]
comment-token = "//"
indent = { tab-width = 4, unit = "    " }

[[grammar]]
name = "zolo"
source = { path = "/path/to/zolo-lang/tree-sitter-zolo" }
```

Then fetch/build the grammar and install the queries into Helix's runtime:

```bash
hx --grammar fetch
hx --grammar build

# Helix expects queries under runtime/queries/<lang>/
mkdir -p ~/.config/helix/runtime/queries/zolo
cp /path/to/zolo-lang/tree-sitter-zolo/queries/*.scm ~/.config/helix/runtime/queries/zolo/
```

## Building and development

Install dev dependencies:

```bash
cd tree-sitter-zolo
npm install
```

Regenerate the parser from `grammar.js` (run this whenever the grammar changes —
the committed `src/parser.c` must stay in sync):

```bash
tree-sitter generate           # or: npx tree-sitter generate
```

Run the parser test corpus:

```bash
tree-sitter test               # cases live in test/corpus/*.txt
```

Build the native bindings:

```bash
npm run build                  # Node bindings
cargo build                    # Rust bindings
```

## Known limitations

- Macro bodies are parsed as a generic block — `$param` placeholders are not
  expanded during parsing.
- `comptime` blocks have no dedicated rule yet (they fall back to `block`).
- Some rare interpolation forms with a complex format spec may need tweaks.
- Tagged templates assume the tag name is a simple ASCII identifier.

## License

MIT — same license as the Zolo project.
