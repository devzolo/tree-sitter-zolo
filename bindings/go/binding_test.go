package tree_sitter_zolo_test

import (
	"testing"

	tree_sitter "github.com/smacker/go-tree-sitter"
	"github.com/tree-sitter/tree-sitter-zolo"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_zolo.Language())
	if language == nil {
		t.Errorf("Error loading Zolo grammar")
	}
}
