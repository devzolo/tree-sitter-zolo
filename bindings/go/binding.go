package tree_sitter_zolo

// #cgo CFLAGS: -std=c11 -fPIC
// #include "../../src/parser.c"
// // `_markup_lt` — see src/scanner.c.
// #include "../../src/scanner.c"
import "C"

import "unsafe"

// Get the tree-sitter Language for this grammar.
func Language() unsafe.Pointer {
	return unsafe.Pointer(C.tree_sitter_zolo())
}
