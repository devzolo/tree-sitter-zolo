# Zolo — Neovim integration

A drop-in Neovim plugin that bundles everything Neovim needs for the
[Zolo](https://github.com/devzolo/zolo-lang) language:

| File | Purpose |
| --- | --- |
| `ftdetect/zolo.lua` | Maps the `.zolo` extension to the `zolo` filetype. |
| `plugin/zolo.lua` | Registers the parser with nvim-treesitter (both the `master` and `main` branches) so `:TSInstall zolo` works. The parser points at the **local** grammar checkout next to this folder. |
| `ftplugin/zolo.lua` | Buffer-local settings (`commentstring`, comments) and starts the built-in tree-sitter highlighter when nvim-treesitter is not in charge. |
| `queries/zolo/*.scm` | `highlights`, `folds`, `injections`, `locals`, `indents` — the queries Neovim loads from `runtimepath`. |
| `scripts/sync-queries.{ps1,sh}` | Regenerate the files in `queries/zolo/` from the canonical `../../queries/*.scm`. |

> `queries/zolo/*.scm` are **generated copies** of the canonical queries in
> `tree-sitter-zolo/queries/`. After editing a canonical query, re-run
> `scripts/sync-queries.ps1` (Windows) or `scripts/sync-queries.sh` (Unix).

## Install

This folder is a subdirectory of the `zolo-lang` monorepo, so load it from a
**local path** with your plugin manager. Replace the path with where you cloned
`zolo-lang` (forward slashes work on Windows too).

### lazy.nvim

```lua
{
  dir = "/path/to/zolo-lang/tree-sitter-zolo/editors/nvim",
  dependencies = { "nvim-treesitter/nvim-treesitter" },
  lazy = false, -- load at startup so :TSInstall zolo is available immediately
}
```

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

## Then

```vim
:TSInstall zolo      " compile the parser from the local grammar
:edit example.zolo   " open a Zolo file — highlighting should be on
```

`:TSInstall zolo` needs a C compiler on `PATH` (gcc/clang/zig/cl). Use
`:TSInstall! zolo` to force a rebuild after the grammar changes.

See the [main README](../../README.md) for the full step-by-step, the
nvim-treesitter-only path (no local clone), the no-nvim-treesitter path,
verification, and troubleshooting.
