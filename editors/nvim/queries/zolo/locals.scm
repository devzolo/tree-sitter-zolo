; Locals: scopes and definitions for Zolo

; -- Scopes -----------------------------------------------------------------
(source_file) @local.scope
(function_item) @local.scope
(trait_method) @local.scope
(lambda_expression) @local.scope
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

(variadic_parameter
  name: (identifier) @local.definition.parameter)

(for_expression
  binding: (identifier) @local.definition.var)

(function_item
  name: (identifier) @local.definition.function)

(macro_item
  name: (identifier) @local.definition.macro)

(struct_item
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

(use_list
  (identifier) @local.definition.import)

; -- References -------------------------------------------------------------
(identifier) @local.reference
