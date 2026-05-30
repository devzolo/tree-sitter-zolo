-- Registers the Zolo tree-sitter parser with nvim-treesitter.
--
-- This file is sourced automatically when its parent directory
-- (tree-sitter-zolo/editors/nvim) is on Neovim's 'runtimepath'. It supports
-- both nvim-treesitter branches:
--   * `master` (classic): parser_config table via get_parser_configs()
--   * `main`   (rewrite): registration inside a `User TSUpdate` autocommand
--
-- The parser is pointed at the *local* grammar checkout that ships alongside
-- this plugin, so `:TSInstall zolo` / `:TSInstall! zolo` compiles whatever is
-- currently in ../../src/parser.c — handy while iterating on the grammar.
-- To install from GitHub instead, replace `url`/`path` below with the
-- repository URL and `location = "tree-sitter-zolo"`.

if vim.g.loaded_tree_sitter_zolo then
  return
end
vim.g.loaded_tree_sitter_zolo = true

-- Absolute path to the grammar root (…/tree-sitter-zolo), derived from this
-- file: …/tree-sitter-zolo/editors/nvim/plugin/zolo.lua  ->  4 levels up.
local grammar_dir = vim.fn.fnamemodify(debug.getinfo(1, "S").source:sub(2), ":p:h:h:h:h")
grammar_dir = grammar_dir:gsub("\\", "/") -- normalise for Windows

-- Classic `master` branch -----------------------------------------------------
local ok, parsers = pcall(require, "nvim-treesitter.parsers")
if ok and type(parsers) == "table" and type(parsers.get_parser_configs) == "function" then
  parsers.get_parser_configs().zolo = {
    install_info = {
      url = grammar_dir,
      files = { "src/parser.c" },
      branch = "main",
    },
    filetype = "zolo",
  }
end

-- New `main` branch -----------------------------------------------------------
vim.api.nvim_create_autocmd("User", {
  pattern = "TSUpdate",
  callback = function()
    local ok2, p = pcall(require, "nvim-treesitter.parsers")
    if ok2 and type(p) == "table" and type(p.get_parser_configs) ~= "function" then
      p.zolo = {
        install_info = {
          path = grammar_dir,
          queries = "editors/nvim/queries/zolo",
        },
      }
    end
  end,
})
