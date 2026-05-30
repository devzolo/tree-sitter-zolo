-- Buffer-local settings for Zolo files.

-- Zolo uses C-style comments.
vim.bo.commentstring = "// %s"
vim.bo.comments = ":///,://"

-- Start the built-in tree-sitter highlighter. This is what makes highlighting
-- work *without* nvim-treesitter's highlight module (the parser must still be
-- installed in 'runtimepath'/parser/). It is a harmless no-op when the parser
-- is missing, and idempotent when nvim-treesitter is already managing the
-- buffer. Set `vim.g.zolo_no_ts_autostart = true` to opt out.
if not vim.g.zolo_no_ts_autostart then
  pcall(vim.treesitter.start)
end
