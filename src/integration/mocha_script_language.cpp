/**************************************************************************/
/*  mocha_script_language.cpp                                             */
/*                                                                        */
/*  Copyright (c) 2026 Cafecito Games LLC.                                */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "mocha_script_language.h"

#include "mocha_script.h"
#include "language/mocha_tokenizer.h"

#include <godot_cpp/variant/packed_int32_array.hpp>

namespace mocha_script {

MochaScriptLanguage *MochaScriptLanguage::singleton = nullptr;

MochaScriptLanguage::MochaScriptLanguage() {
	if (singleton == nullptr) {
		singleton = this;
	}
}

MochaScriptLanguage::~MochaScriptLanguage() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void MochaScriptLanguage::_bind_methods() {}

MochaScriptLanguage *MochaScriptLanguage::get_singleton() {
	return singleton;
}

godot::String MochaScriptLanguage::_get_name() const { return "MochaScript"; }
void MochaScriptLanguage::_init() {}
godot::String MochaScriptLanguage::_get_type() const { return "MochaScript"; }
godot::String MochaScriptLanguage::_get_extension() const { return "mocha"; }
void MochaScriptLanguage::_finish() {}
godot::PackedStringArray MochaScriptLanguage::_get_reserved_words() const { return MochaTokenizer::get_keyword_spellings(); }
bool MochaScriptLanguage::_is_control_flow_keyword(const godot::String &p_word) const {
	return p_word == "if" || p_word == "elif" || p_word == "else" || p_word == "for" ||
			p_word == "while" || p_word == "break" || p_word == "continue" || p_word == "return" ||
			p_word == "match" || p_word == "when";
}
godot::PackedStringArray MochaScriptLanguage::_get_comment_delimiters() const { return { "#" }; }
godot::PackedStringArray MochaScriptLanguage::_get_doc_comment_delimiters() const { return { "##" }; }
godot::PackedStringArray MochaScriptLanguage::_get_string_delimiters() const { return { "\" \"", "' '" }; }

godot::Ref<godot::Script> MochaScriptLanguage::_make_template(const godot::String &p_template, const godot::String &, const godot::String &) const {
	godot::Ref<MochaScript> script;
	script.instantiate();
	script->_set_source_code(p_template);
	return script;
}

godot::TypedArray<godot::Dictionary> MochaScriptLanguage::_get_built_in_templates(const godot::StringName &) const { return {}; }
bool MochaScriptLanguage::_is_using_templates() { return false; }

godot::Dictionary MochaScriptLanguage::_validate(const godot::String &p_script, const godot::String &p_path, bool, bool, bool, bool) const {
	godot::Ref<MochaScript> script;
	script.instantiate();
	script->_set_source_code(p_script);
	if (!p_path.is_empty()) {
		script->set_path(p_path);
	}
	script->_reload(false);

	godot::Dictionary result;
	result["valid"] = script->_is_valid();
	result["functions"] = godot::PackedStringArray();
	result["errors"] = script->get_diagnostics();
	result["warnings"] = godot::TypedArray<godot::Dictionary>();
	result["safe_lines"] = godot::PackedInt32Array();
	return result;
}

godot::String MochaScriptLanguage::_validate_path(const godot::String &p_path) const {
	return p_path.get_extension().to_lower() == "mocha" ? godot::String() : godot::String("MochaScript files must use the .mocha extension.");
}
godot::Object *MochaScriptLanguage::_create_script() const { return nullptr; }
bool MochaScriptLanguage::_has_named_classes() const { return false; }
bool MochaScriptLanguage::_supports_builtin_mode() const { return false; }
bool MochaScriptLanguage::_supports_documentation() const { return false; }
bool MochaScriptLanguage::_can_inherit_from_file() const { return false; }
int32_t MochaScriptLanguage::_find_function(const godot::String &, const godot::String &) const { return -1; }
godot::String MochaScriptLanguage::_make_function(const godot::String &, const godot::String &, const godot::PackedStringArray &) const { return {}; }
bool MochaScriptLanguage::_can_make_function() const { return false; }
godot::Error MochaScriptLanguage::_open_in_external_editor(const godot::Ref<godot::Script> &, int32_t, int32_t) { return godot::ERR_UNAVAILABLE; }
bool MochaScriptLanguage::_overrides_external_editor() { return false; }
godot::ScriptLanguage::ScriptNameCasing MochaScriptLanguage::_preferred_file_name_casing() const { return godot::ScriptLanguage::SCRIPT_NAME_CASING_SNAKE_CASE; }
godot::Dictionary MochaScriptLanguage::_complete_code(const godot::String &, const godot::String &, godot::Object *) const { return {}; }
godot::Dictionary MochaScriptLanguage::_lookup_code(const godot::String &, const godot::String &, const godot::String &, godot::Object *) const { return {}; }
godot::String MochaScriptLanguage::_auto_indent_code(const godot::String &p_code, int32_t, int32_t) const { return p_code; }
void MochaScriptLanguage::_add_global_constant(const godot::StringName &, const godot::Variant &) {}
void MochaScriptLanguage::_add_named_global_constant(const godot::StringName &, const godot::Variant &) {}
void MochaScriptLanguage::_remove_named_global_constant(const godot::StringName &) {}
void MochaScriptLanguage::_thread_enter() {}
void MochaScriptLanguage::_thread_exit() {}
godot::String MochaScriptLanguage::_debug_get_error() const { return {}; }
int32_t MochaScriptLanguage::_debug_get_stack_level_count() const { return 0; }
int32_t MochaScriptLanguage::_debug_get_stack_level_line(int32_t) const { return -1; }
godot::String MochaScriptLanguage::_debug_get_stack_level_function(int32_t) const { return {}; }
godot::String MochaScriptLanguage::_debug_get_stack_level_source(int32_t) const { return {}; }
godot::Dictionary MochaScriptLanguage::_debug_get_stack_level_locals(int32_t, int32_t, int32_t) { return {}; }
godot::Dictionary MochaScriptLanguage::_debug_get_stack_level_members(int32_t, int32_t, int32_t) { return {}; }
void *MochaScriptLanguage::_debug_get_stack_level_instance(int32_t) { return nullptr; }
godot::Dictionary MochaScriptLanguage::_debug_get_globals(int32_t, int32_t) { return {}; }
godot::String MochaScriptLanguage::_debug_parse_stack_level_expression(int32_t, const godot::String &, int32_t, int32_t) { return {}; }
godot::TypedArray<godot::Dictionary> MochaScriptLanguage::_debug_get_current_stack_info() { return {}; }
void MochaScriptLanguage::_reload_all_scripts() {}
void MochaScriptLanguage::_reload_scripts(const godot::Array &, bool) {}
void MochaScriptLanguage::_reload_tool_script(const godot::Ref<godot::Script> &, bool) {}

godot::PackedStringArray MochaScriptLanguage::_get_recognized_extensions() const {
	godot::PackedStringArray extensions;
	extensions.push_back("mocha");
	return extensions;
}

godot::TypedArray<godot::Dictionary> MochaScriptLanguage::_get_public_functions() const { return {}; }
godot::Dictionary MochaScriptLanguage::_get_public_constants() const { return {}; }
godot::TypedArray<godot::Dictionary> MochaScriptLanguage::_get_public_annotations() const { return {}; }
void MochaScriptLanguage::_profiling_start() {}
void MochaScriptLanguage::_profiling_stop() {}
void MochaScriptLanguage::_profiling_set_save_native_calls(bool) {}
int32_t MochaScriptLanguage::_profiling_get_accumulated_data(godot::ScriptLanguageExtensionProfilingInfo *, int32_t) { return 0; }
int32_t MochaScriptLanguage::_profiling_get_frame_data(godot::ScriptLanguageExtensionProfilingInfo *, int32_t) { return 0; }
void MochaScriptLanguage::_frame() {}
bool MochaScriptLanguage::_handles_global_class_type(const godot::String &) const { return false; }
godot::Dictionary MochaScriptLanguage::_get_global_class_name(const godot::String &) const { return {}; }

} // namespace mocha_script
