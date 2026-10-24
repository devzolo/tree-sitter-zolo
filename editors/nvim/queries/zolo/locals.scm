; Locals: scopes and definitions for Zolo

; -- Scopes -----------------------------------------------------------------
(source_file) @local.scope
(function_item) @local.scope
(extern_function) @local.scope
(trait_method) @local.scope
(computed_property) @local.scope
(computed_property name: (identifier) @local.definition.function)
(lambda_expression) @local.scope
(trailing_lambda) @local.scope
(block) @local.scope
(match_arm) @local.scope
(for_expression) @local.scope
(while_expression) @local.scope
(while_let_expression) @local.scope
(if_expression) @local.scope
(if_let_expression) @local.scope
(loop_expression) @local.scope
(try_catch_expression) @local.scope
(impl_item) @local.scope
(struct_item) @local.scope
(schema_item) @local.scope
(enum_item) @local.scope
(trait_item) @local.scope

; -- Definitions ------------------------------------------------------------
(let_declaration
  pattern: (identifier) @local.definition.var)

(tuple_pattern_binding
  (identifier) @local.definition.var)

(native_multi_binding
  (identifier) @local.definition.var)

(const_item
  name: (identifier) @local.definition.constant)

(parameter
  name: (identifier) @local.definition.parameter)

; The public label is not a lexical binding in the function body.
(external_parameter
  name: (identifier) @local.definition.parameter)

(variadic_parameter
  name: (identifier) @local.definition.parameter)

(for_expression
  binding: (identifier) @local.definition.var)

(loop_expression
  label: (loop_label
    name: (identifier) @local.definition.label))

(for_expression
  label: (loop_label
    name: (identifier) @local.definition.label))

(while_expression
  label: (loop_label
    name: (identifier) @local.definition.label))

(while_let_expression
  label: (loop_label
    name: (identifier) @local.definition.label))

(function_item
  name: (identifier) @local.definition.function)

(extern_function
  name: (identifier) @local.definition.function)

(extern_module
  name: (identifier) @local.definition.import)

(macro_item
  name: (identifier) @local.definition.macro)

(struct_item
  name: (identifier) @local.definition.type)

(schema_item
  name: (identifier) @local.definition.type)

(enum_item
  name: (identifier) @local.definition.type)

(trait_item
  name: (identifier) @local.definition.type)

(type_alias
  name: (identifier) @local.definition.type)

(newtype_item
  name: (identifier) @local.definition.type)

(type_parameter
  name: (identifier) @local.definition.type)

(use_path
  (identifier) @local.definition.import)

(use_item
  name: (identifier) @local.definition.import)

; -- References -------------------------------------------------------------
(identifier) @local.reference

(break_statement
  label: (loop_label
    name: (identifier) @local.reference))

(continue_statement
  label: (loop_label
    name: (identifier) @local.reference))

; Ordinary callback bodies own their parameters and braces.
(trailing_callback_body) @local.scope

; Typed handler override source scopes.
(handler_override_arm) @local.scope
(handler_override_parameter name: (identifier) @local.definition.parameter)
(handler_override_parameter pattern: (tuple_pattern (identifier_pattern (identifier) @local.definition.parameter)))
(handler_override_parameter pattern: (array_pattern (identifier_pattern (identifier) @local.definition.parameter)))
(handler_override_parameter pattern: (array_pattern rest: (identifier) @local.definition.parameter))
(handler_override_parameter pattern: (struct_pattern (field_pattern name: (identifier) @local.definition.parameter !pattern)))
(handler_override_parameter pattern: (anon_struct_pattern (field_pattern name: (identifier) @local.definition.parameter !pattern)))
(handler_override_parameter pattern: (struct_pattern (field_pattern pattern: (identifier_pattern (identifier) @local.definition.parameter))))
(handler_override_parameter pattern: (anon_struct_pattern (field_pattern pattern: (identifier_pattern (identifier) @local.definition.parameter))))

; Authored titled tests own a source lexical scope without a synthetic name.
(titled_test_declaration) @local.scope

; Template parameters are local to their definition; pins remain references.
(pattern_alias_declaration) @local.scope
(pattern_alias_declaration name: (identifier) @local.definition.type)
(pattern_alias_declaration parameter: (identifier) @local.definition.parameter)
(tuple_rest_pattern name: (identifier) @local.definition.var)

; Fifth headers keep their bindings local to their authored callable/element.
(expanded_record_parameter name: (identifier) @local.definition.parameter)
(fold_expression) @local.scope
(fold_expression accumulator: (identifier) @local.definition.var)
(fold_expression binding: (identifier_pattern (identifier) @local.definition.var))
(resource_binding name: (identifier) @local.definition.var)
(lexical_resource_declaration name: (identifier) @local.definition.var)
(optional_pattern binding: (identifier) @local.definition.var)
(labeled_block_expression) @local.scope
(labeled_block_expression (loop_label name: (identifier) @local.definition.label))
(map_pattern_rest rest: (identifier) @local.definition.var)
(map_pattern_entry pattern: (identifier_pattern (identifier) @local.definition.var))
; Builder intermediates belong to one element, never the surrounding block.
(array_for_entry) @local.scope
(map_for_entry) @local.scope
(builder_binding binding: (identifier_pattern (identifier) @local.definition.var))
(assert_let_statement pattern: (identifier_pattern (identifier) @local.definition.var))
(assert_let_statement pattern: (enum_pattern (identifier_pattern (identifier) @local.definition.var)))
(markup_element) @local.scope
(markup_self_closing_tag) @local.scope
((markup_attribute
  name: (markup_attribute_name) @directive
  value: (markup_attribute_expression
    (expression_statement
      (binary_expression left: (identifier) @local.definition.var "in"))))
 (#eq? @directive "v:for"))
