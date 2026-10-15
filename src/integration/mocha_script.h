/**************************************************************************/
/*  mocha_script.h                                                        */
/*                                                                        */
/*  Copyright (c) 2026 Cafecito Games LLC.                                */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "language/mocha_ast.h"
#include "runtime/mocha_bytecode.h"

#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/templates/hash_set.hpp>

#include <mutex>

namespace mocha_script {

class MochaScript final : public godot::ScriptExtension {
	GDCLASS(MochaScript, godot::ScriptExtension)

	godot::String source_code;
	godot::String diagnostic_path;
	bool valid = false;
	int64_t token_count = 0;
	godot::TypedArray<godot::Dictionary> diagnostics;
	std::shared_ptr<Program> program;
	std::shared_ptr<CompiledModule> compiled_module;
	mutable std::mutex instances_mutex;
	mutable godot::HashSet<class MochaScriptInstance *> instances;

protected:
	static void _bind_methods();

public:
	int64_t get_token_count() const;
	godot::TypedArray<godot::Dictionary> get_diagnostics() const;
	bool is_compilation_valid() const;
	void set_diagnostic_path_for_validation(const godot::String &p_path) { diagnostic_path = p_path; }
	const Program *get_program() const { return program.get(); }
	const CompiledModule *get_compiled_module() const { return compiled_module.get(); }
	std::shared_ptr<CompiledModule> retain_compiled_module() const { return compiled_module; }
	const CompiledFunction *find_function(const godot::StringName &p_name) const;
	int64_t get_member_index(const godot::StringName &p_name) const;
	int64_t get_member_count() const;
	godot::StringName get_member_name(int64_t p_index) const;
	const TypeRef *get_member_type(int64_t p_index) const;
	void notify_instance_freed(MochaScriptInstance *p_instance);

	bool _editor_can_reload_from_file() override;
	void _placeholder_erased(void *p_placeholder) override;
	bool _can_instantiate() const override;
	godot::Ref<godot::Script> _get_base_script() const override;
	godot::StringName _get_global_name() const override;
	bool _inherits_script(const godot::Ref<godot::Script> &p_script) const override;
	godot::StringName _get_instance_base_type() const override;
	void *_instance_create(godot::Object *p_for_object) const override;
	void *_placeholder_instance_create(godot::Object *p_for_object) const override;
	bool _has_source_code() const override;
	godot::String _get_source_code() const override;
	void _set_source_code(const godot::String &p_code) override;
	godot::Error _reload(bool p_keep_state) override;
	godot::StringName _get_doc_class_name() const override;
	godot::TypedArray<godot::Dictionary> _get_documentation() const override;
	godot::String _get_class_icon_path() const override;
	bool _has_method(const godot::StringName &p_method) const override;
	bool _has_static_method(const godot::StringName &p_method) const override;
	godot::Variant _get_script_method_argument_count(const godot::StringName &p_method) const override;
	godot::Dictionary _get_method_info(const godot::StringName &p_method) const override;
	bool _is_tool() const override;
	bool _is_valid() const override;
	bool _is_abstract() const override;
	godot::ScriptLanguage *_get_language() const override;
	bool _has_script_signal(const godot::StringName &p_signal) const override;
	godot::TypedArray<godot::Dictionary> _get_script_signal_list() const override;
	bool _has_property_default_value(const godot::StringName &p_property) const override;
	godot::Variant _get_property_default_value(const godot::StringName &p_property) const override;
	void _update_exports() override;
	godot::TypedArray<godot::Dictionary> _get_script_method_list() const override;
	godot::TypedArray<godot::Dictionary> _get_script_property_list() const override;
	int32_t _get_member_line(const godot::StringName &p_member) const override;
	godot::Dictionary _get_constants() const override;
	godot::TypedArray<godot::StringName> _get_members() const override;
	bool _is_placeholder_fallback_enabled() const override;
	godot::Variant _get_rpc_config() const override;
	bool _instance_has(godot::Object *p_object) const override;
};

} // namespace mocha_script
