//! This crate provides Zolo language support for the [tree-sitter][] parsing
//! library.
//!
//! Typically, you will use the [`LANGUAGE`][LANGUAGE] constant to add this
//! grammar to a tree-sitter [`Parser`][Parser], and then use the parser to
//! parse some code:
//!
//! ```ignore
//! let code = r#"
//!     fn main() {
//!         print("hello, world")
//!     }
//! "#;
//! let mut parser = tree_sitter::Parser::new();
//! let language = tree_sitter_zolo::LANGUAGE;
//! parser
//!     .set_language(&language.into())
//!     .expect("Error loading Zolo parser");
//! let tree = parser.parse(code, None).unwrap();
//! assert!(!tree.root_node().has_error());
//! ```
//!
//! [Parser]: https://docs.rs/tree-sitter/*/tree_sitter/struct.Parser.html
//! [tree-sitter]: https://tree-sitter.github.io/

use tree_sitter_language::LanguageFn;

extern "C" {
    fn tree_sitter_zolo() -> *const ();
}

/// The tree-sitter [`LanguageFn`] for this grammar.
pub const LANGUAGE: LanguageFn = unsafe { LanguageFn::from_raw(tree_sitter_zolo) };

/// The content of the [`node-types.json`][] file for this grammar.
///
/// [`node-types.json`]: https://tree-sitter.github.io/tree-sitter/using-parsers#static-node-types
pub const NODE_TYPES: &str = include_str!("../../src/node-types.json");

/// The syntax highlighting query for this language.
pub const HIGHLIGHTS_QUERY: &str = include_str!("../../queries/highlights.scm");

/// The local-variable syntax highlighting query for this language.
pub const LOCALS_QUERY: &str = include_str!("../../queries/locals.scm");

/// The injections query for this language.
pub const INJECTIONS_QUERY: &str = include_str!("../../queries/injections.scm");

/// The folds query for this language.
pub const FOLDS_QUERY: &str = include_str!("../../queries/folds.scm");

#[cfg(test)]
mod tests {
    #[test]
    fn test_can_load_grammar() {
        let mut parser = tree_sitter::Parser::new();
        parser
            .set_language(&super::LANGUAGE.into())
            .expect("Error loading Zolo parser");
    }
}
