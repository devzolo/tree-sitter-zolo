#!/usr/bin/env sh
# Sync the canonical tree-sitter queries into this plugin's queries/zolo/
# directory. Neovim expects queries under queries/<lang>/*.scm, whereas the
# grammar keeps them flat in tree-sitter-zolo/queries/*.scm. Re-run this script
# whenever the canonical queries change.
#
#   sh editors/nvim/scripts/sync-queries.sh
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
src="$here/../../../queries"
dst="$here/../queries/zolo"
mkdir -p "$dst"
for q in highlights folds injections locals indents; do
  cp "$src/$q.scm" "$dst/$q.scm"
  echo "synced $q.scm"
done
