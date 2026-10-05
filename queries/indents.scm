; Indentation for Zolo.
; Capture names follow the modern nvim-treesitter `indent` module convention
; (@indent.begin / @indent.branch / @indent.ignore). Helix and other editors
; that read this file fall back gracefully on captures they don't recognise.

; -- Containers that open an indentation level ------------------------------
[
  (block)
  (extern_body)
  (extern_group)
  (dependencies_declaration)
  (dependency_group)
  (extern_dependencies)
  (extern_dependency_options)
  (trailing_lambda)
  (declaration_body)
  (field_declaration_list)
  (compact_field_list)
  (enum_variant_list)
  (enum_variant_tuple)
  (trait_body)
  (impl_body)
  (parameter_list)
  (argument_list)
  (decorator_arguments)
  (macro_parameters)
  (use_list)
  (array_expression)
  (map_expression)
  (tuple_expression)
  (struct_expression_body)
  (match_expression)
  (tuple_pattern)
  (array_pattern)
  (struct_pattern)
  (where_clause)
] @indent.begin

; -- Closing delimiters dedent back to the opening line ---------------------
[
  "}"
  "]"
  ")"
] @indent.branch

; -- Never re-indent inside multi-line string literals ----------------------
[
  (triple_string_literal)
  (raw_string_literal)
  (string_literal)
  (foreign_content)
  (python_foreign_content)
  (javascript_foreign_content)
  (typescript_foreign_content)
  (java_foreign_content)
  (kotlin_foreign_content)
  (c_foreign_content)
  (cpp_foreign_content)
  (rust_foreign_content)
  (go_foreign_content)
] @indent.ignore

; Ordinary callback bodies own their parameters and braces.
(trailing_callback_body) @indent.begin
