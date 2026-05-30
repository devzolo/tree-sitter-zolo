# Sync the canonical tree-sitter queries into this plugin's queries/zolo/
# directory. Neovim expects queries under queries/<lang>/*.scm, whereas the
# grammar keeps them flat in tree-sitter-zolo/queries/*.scm. Re-run this script
# whenever the canonical queries change.
#
#   pwsh editors/nvim/scripts/sync-queries.ps1
$ErrorActionPreference = 'Stop'
$src = Join-Path $PSScriptRoot '..\..\..\queries'
$dst = Join-Path $PSScriptRoot '..\queries\zolo'
New-Item -ItemType Directory -Force -Path $dst | Out-Null
foreach ($q in 'highlights', 'folds', 'injections', 'locals', 'indents') {
  Copy-Item -Force (Join-Path $src "$q.scm") (Join-Path $dst "$q.scm")
  Write-Host "synced $q.scm"
}
