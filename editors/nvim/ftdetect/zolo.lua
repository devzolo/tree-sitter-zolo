-- Filetype detection for Zolo source files.
-- Loaded eagerly by Neovim (and by plugin managers such as lazy.nvim) so that
-- `.zolo` buffers get the `zolo` filetype even before the rest of this plugin
-- is loaded.
vim.filetype.add({
  extension = {
    zolo = "zolo",
  },
})
