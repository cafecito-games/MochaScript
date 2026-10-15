/**************************************************************************/
/*  mocha_script.cpp                                                      */
/*                                                                        */
/*  Copyright (c) 2026 Cafecito Games LLC.                                */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "mocha_script.h"

#include "mocha_script_language.h"
#include "mocha_script_instance.h"
#include "language/mocha_analyzer.h"
#include "language/mocha_compiler.h"
#include "language/mocha_parser.h"
#include "language/mocha_tokenizer.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace mocha_script {

namespace {

godot::Variant::Type carrier_for_type(const TypeRef &p_type) {
	switch (p_type.kind) {
		case TypeKind::BOOL: return godot::Variant::BOOL;
		case TypeKind::INT: return godot::Variant::INT;
		case TypeKind::FLOAT: return godot::Variant::FLOAT;
		case TypeKind::STRING: return godot::Variant::STRING;
		case TypeKind::ARRAY: return godot::Variant::ARRAY;
		case TypeKind::DICTIONARY: return godot::Variant::DICTIONARY;
		case TypeKind::OBJECT: return godot::Variant::OBJECT;
		default: return godot::Variant::NIL;
	}
}

godot::Dictionary method_value_info(const godot::StringName &p_name, const TypeRef &p_type) {
	godot::Dictionary info;
	info["name"] = p_name;
	info["type"] = int64_t(carrier_for_type(p_type));
	info["class_name"] = p_type.kind == TypeKind::OBJECT ? p_type.name : godot::StringName();
	info["hint"] = int64_t(godot::PROPERTY_HINT_NONE);
	info["hint_string"] = godot::String();
	info["usage"] = int64_t(p_type.kind == TypeKind::ANY ? godot::PROPERTY_USAGE_NIL_IS_VARIANT : godot::PROPERTY_USAGE_DEFAULT);
	return info;
}

} // namespace

void MochaScript::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("get_token_count"), &MochaScript::get_token_count);
	godot::ClassDB::bind_method(godot::D_METHOD("get_diagnostics"), &MochaScript::get_diagnostics);
	godot::ClassDB::bind_method(godot::D_METHOD("is_compilation_valid"), &MochaScript::is_compilation_valid);
}

int64_t MochaScript::get_token_count() const { return token_count; }
godot::TypedArray<godot::Dictionary> MochaScript::get_diagnostics() const { return diagnostics; }
bool MochaScript::is_compilation_valid() const { return valid; }
const CompiledFunction *MochaScript::find_function(const godot::StringName &p_name) const {
	return compiled_module ? compiled_module->find_function(p_name) : nullptr;
}
int64_t MochaScript::get_member_index(const godot::StringName &p_name) const {
	return compiled_module ? compiled_module->find_member(p_name) : -1;
}
int64_t MochaScript::get_member_count() const { return compiled_module ? int64_t(compiled_module->members.size()) : 0; }
godot::StringName MochaScript::get_member_name(int64_t p_index) const {
	return compiled_module && p_index >= 0 && p_index < int64_t(compiled_module->members.size())
			? compiled_module->members[size_t(p_index)].name : godot::StringName();
}
const TypeRef *MochaScript::get_member_type(int64_t p_index) const {
	return compiled_module && p_index >= 0 && p_index < int64_t(compiled_module->members.size())
			? &compiled_module->members[size_t(p_index)].type : nullptr;
}
void MochaScript::notify_instance_freed(MochaScriptInstance *p_instance) { instances.erase(p_instance); }
bool MochaScript::_editor_can_reload_from_file() { return true; }
void MochaScript::_placeholder_erased(void *) {}
bool MochaScript::_can_instantiate() const { return valid && compiled_module != nullptr; }
godot::Ref<godot::Script> MochaScript::_get_base_script() const { return {}; }
godot::StringName MochaScript::_get_global_name() const { return {}; }
bool MochaScript::_inherits_script(const godot::Ref<godot::Script> &) const { return false; }
godot::StringName MochaScript::_get_instance_base_type() const {
	return compiled_module ? compiled_module->base_type : godot::StringName();
}
void *MochaScript::_instance_create(godot::Object *p_for_object) const {
	if (!_can_instantiate() || p_for_object == nullptr) return nullptr;
	if (!compiled_module->base_type.is_empty() && !godot::ClassDB::is_parent_class(p_for_object->get_class(), compiled_module->base_type)) {
		godot::UtilityFunctions::push_error(godot::vformat("Cannot attach %s to %s; MochaScript extends %s.", get_path(), p_for_object->get_class(), godot::String(compiled_module->base_type)));
		return nullptr;
	}
	const std::shared_ptr<CompiledModule> module = compiled_module;
	auto *instance = memnew(MochaScriptInstance);
	instance->owner = p_for_object;
	instance->script = godot::Ref<MochaScript>(const_cast<MochaScript *>(this));
	const_cast<MochaScript *>(this)->instances.insert(instance);
	RuntimeFailure failure;
	if (MochaVM::initialize(*module, p_for_object, instance->members, failure) != godot::OK) {
		godot::UtilityFunctions::push_error(godot::vformat("%s:%d:%d: %s: %s", get_path(), failure.span.line, failure.span.column, failure.code, failure.message));
		MochaVM::publish_nested_failure(failure);
		const_cast<MochaScript *>(this)->instances.erase(instance);
		memdelete(instance);
		return nullptr;
	}
	void *handle = godot::gdextension_interface::script_instance_create3(MochaScriptInstance::get_vtable(), instance);
	if (handle == nullptr) {
		const_cast<MochaScript *>(this)->instances.erase(instance);
		memdelete(instance);
	}
	return handle;
}
void *MochaScript::_placeholder_instance_create(godot::Object *p_for_object) const {
	MochaScriptLanguage *language = MochaScriptLanguage::get_singleton();
	if (language == nullptr || p_for_object == nullptr) return nullptr;
	return godot::gdextension_interface::placeholder_script_instance_create(language->_owner,
			const_cast<MochaScript *>(this)->_owner, p_for_object->_owner);
}
bool MochaScript::_has_source_code() const { return true; }
godot::String MochaScript::_get_source_code() const { return source_code; }
void MochaScript::_set_source_code(const godot::String &p_code) {
	if (p_code == source_code) return;
	if (!instances.is_empty() && p_code != source_code) {
		diagnostics.clear();
		godot::Dictionary diagnostic;
		diagnostic["code"] = "M0003";
		diagnostic["message"] = "Source replacement is refused while a MochaScript instance is alive.";
		diagnostic["path"] = get_path().is_empty() ? godot::String("<memory>") : get_path();
		diagnostic["line"] = 1;
		diagnostic["column"] = 1;
		diagnostic["end_line"] = 1;
		diagnostic["end_column"] = 1;
		diagnostics.push_back(diagnostic);
		return;
	}
	source_code = p_code;
	valid = false;
	token_count = 0;
	diagnostics.clear();
	program.reset();
	compiled_module.reset();
}

godot::Error MochaScript::_reload(bool) {
	if (!instances.is_empty()) {
		return godot::ERR_ALREADY_IN_USE;
	}
	valid = true;
	token_count = 0;
	diagnostics.clear();
	program.reset();
	compiled_module.reset();

	auto append_diagnostic = [this](const Diagnostic &p_source) {
		godot::Dictionary diagnostic;
		diagnostic["code"] = p_source.code;
		diagnostic["message"] = p_source.message;
		diagnostic["path"] = p_source.path;
		diagnostic["line"] = p_source.span.line;
		diagnostic["column"] = p_source.span.column;
		diagnostic["end_line"] = p_source.span.end_line;
		diagnostic["end_column"] = p_source.span.end_column;
		diagnostics.push_back(diagnostic);
	};

	// Keep the tokenizer as an independently observable layer. Parser diagnostics
	// start only after this pass succeeds, so lexical errors keep their stable M0001 code.
	MochaTokenizerText tokenizer;
	tokenizer.set_source_code(source_code);
	const int64_t scan_limit = source_code.length() * 4 + 1024;
	for (int64_t scanned = 0; scanned < scan_limit; ++scanned) {
		const MochaTokenizer::Token token = tokenizer.scan();
		token_count++;
		if (token.type == MochaTokenizer::Token::ERROR) {
			valid = false;
			append_diagnostic({ "M0001", godot::String(token.literal),
					get_path().is_empty() ? godot::String("<memory>") : get_path(), span_from_token(token) });
		}
		if (token.type == MochaTokenizer::Token::TK_EOF) {
			break;
		}
	}
	if (!valid) return godot::ERR_PARSE_ERROR;

	const godot::String source_path = get_path().is_empty() ? godot::String("<memory>") : get_path();
	auto parsed = std::make_shared<Program>();
	MochaParser parser;
	const godot::Error parse_status = parser.parse(source_code, source_path, *parsed);
	token_count = parser.get_token_count();
	for (const Diagnostic &entry : parser.get_diagnostics()) append_diagnostic(entry);
	if (parse_status != godot::OK) {
		valid = false;
		return godot::ERR_PARSE_ERROR;
	}

	MochaAnalyzer analyzer;
	const godot::Error analysis_status = analyzer.analyze(*parsed, source_path);
	for (const Diagnostic &entry : analyzer.get_diagnostics()) append_diagnostic(entry);
	if (analysis_status != godot::OK) {
		valid = false;
		return godot::ERR_COMPILATION_FAILED;
	}

	MochaCompiler compiler;
	std::shared_ptr<CompiledModule> compiled;
	const godot::Error compile_status = compiler.compile(*parsed, source_path, compiled);
	for (const Diagnostic &entry : compiler.get_diagnostics()) append_diagnostic(entry);
	if (compile_status != godot::OK) {
		valid = false;
		return godot::ERR_COMPILATION_FAILED;
	}

	program = std::move(parsed);
	compiled_module = std::move(compiled);
	valid = true;
	return godot::OK;
}
godot::StringName MochaScript::_get_doc_class_name() const { return {}; }
godot::TypedArray<godot::Dictionary> MochaScript::_get_documentation() const { return {}; }
godot::String MochaScript::_get_class_icon_path() const { return {}; }
bool MochaScript::_has_method(const godot::StringName &p_method) const { return find_function(p_method) != nullptr; }
bool MochaScript::_has_static_method(const godot::StringName &) const { return false; }
godot::Variant MochaScript::_get_script_method_argument_count(const godot::StringName &p_method) const {
	const CompiledFunction *function = find_function(p_method);
	return function ? godot::Variant(int64_t(function->parameter_types.size())) : godot::Variant();
}
godot::Dictionary MochaScript::_get_method_info(const godot::StringName &p_method) const {
	godot::Dictionary info;
	const CompiledFunction *function = find_function(p_method);
	if (function) {
		info["name"] = p_method;
		godot::Array arguments;
		for (size_t index = 0; index < function->parameter_names.size(); ++index) {
			arguments.push_back(method_value_info(function->parameter_names[index], function->parameter_types[index]));
		}
		info["args"] = arguments;
		info["return"] = method_value_info(godot::StringName(), function->return_type);
		info["default_args"] = godot::Array();
		info["flags"] = int64_t(godot::METHOD_FLAG_NORMAL);
	}
	return info;
}
bool MochaScript::_is_tool() const { return false; }
bool MochaScript::_is_valid() const { return valid; }
bool MochaScript::_is_abstract() const { return false; }
godot::ScriptLanguage *MochaScript::_get_language() const { return MochaScriptLanguage::get_singleton(); }
bool MochaScript::_has_script_signal(const godot::StringName &) const { return false; }
godot::TypedArray<godot::Dictionary> MochaScript::_get_script_signal_list() const { return {}; }
bool MochaScript::_has_property_default_value(const godot::StringName &) const { return false; }
godot::Variant MochaScript::_get_property_default_value(const godot::StringName &) const { return {}; }
void MochaScript::_update_exports() {}
godot::TypedArray<godot::Dictionary> MochaScript::_get_script_method_list() const {
	godot::TypedArray<godot::Dictionary> methods;
	if (compiled_module) for (const CompiledFunction &function : compiled_module->functions) methods.push_back(_get_method_info(function.name));
	return methods;
}
godot::TypedArray<godot::Dictionary> MochaScript::_get_script_property_list() const {
	godot::TypedArray<godot::Dictionary> properties;
	if (!compiled_module) return properties;
	for (size_t index = 0; index < compiled_module->members.size(); ++index) {
		const CompiledMember &member = compiled_module->members[index];
		godot::Dictionary property = method_value_info(member.name, member.type);
		property["usage"] = int64_t(godot::PROPERTY_USAGE_SCRIPT_VARIABLE |
				(member.type.kind == TypeKind::ANY ? godot::PROPERTY_USAGE_NIL_IS_VARIANT : 0));
		properties.push_back(property);
	}
	return properties;
}
int32_t MochaScript::_get_member_line(const godot::StringName &p_member) const {
	const int64_t index = get_member_index(p_member);
	return index >= 0 ? compiled_module->members[size_t(index)].span.line : -1;
}
godot::Dictionary MochaScript::_get_constants() const { return {}; }
godot::TypedArray<godot::StringName> MochaScript::_get_members() const {
	godot::TypedArray<godot::StringName> result;
	if (compiled_module) for (const CompiledMember &member : compiled_module->members) result.push_back(member.name);
	return result;
}
bool MochaScript::_is_placeholder_fallback_enabled() const { return false; }
godot::Variant MochaScript::_get_rpc_config() const { return godot::Dictionary(); }
bool MochaScript::_instance_has(godot::Object *p_object) const {
	MochaScriptInstance *instance = MochaScriptInstance::from_owner(p_object);
	return instance != nullptr && instance->script.ptr() == this;
}

} // namespace mocha_script
