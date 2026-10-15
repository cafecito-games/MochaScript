/**************************************************************************/
/*  mocha_script_instance.cpp                                              */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of MochaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "mocha_script_instance.h"

#include "mocha_script.h"
#include "mocha_script_language.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/godot.hpp>

namespace mocha_script {

namespace {

Variant::Type carrier_for_type(const TypeRef &p_type) {
	switch (p_type.kind) {
		case TypeKind::BOOL: return Variant::BOOL;
		case TypeKind::INT: return Variant::INT;
		case TypeKind::FLOAT: return Variant::FLOAT;
		case TypeKind::STRING: return Variant::STRING;
		case TypeKind::ARRAY: return Variant::ARRAY;
		case TypeKind::DICTIONARY: return Variant::DICTIONARY;
		case TypeKind::OBJECT: return Variant::OBJECT;
		default: return Variant::NIL;
	}
}

void populate_value_info(GDExtensionPropertyInfo &r_info, const StringName &p_name, const TypeRef &p_type) {
	r_info = {};
	r_info.type = GDExtensionVariantType(carrier_for_type(p_type));
	r_info.name = memnew(StringName(p_name));
	r_info.class_name = memnew(StringName(p_type.kind == TypeKind::OBJECT ? p_type.name : StringName()));
	r_info.hint = PROPERTY_HINT_NONE;
	r_info.hint_string = memnew(String);
	r_info.usage = p_type.kind == TypeKind::ANY ? PROPERTY_USAGE_NIL_IS_VARIANT : PROPERTY_USAGE_DEFAULT;
}

MochaScriptInstance *instance_of(GDExtensionScriptInstanceDataPtr p_instance) {
	return reinterpret_cast<MochaScriptInstance *>(p_instance);
}

GDExtensionBool instance_set(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) {
	MochaScriptInstance *instance = instance_of(p_instance);
	if (instance == nullptr) {
		return false;
	}
	return instance->set_property(*reinterpret_cast<const StringName *>(p_name), *reinterpret_cast<const Variant *>(p_value));
}

GDExtensionBool instance_get(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_value) {
	MochaScriptInstance *instance = instance_of(p_instance);
	if (instance == nullptr) {
		return false;
	}
	Variant value;
	if (!instance->get_property(*reinterpret_cast<const StringName *>(p_name), value)) {
		return false;
	}
	*reinterpret_cast<Variant *>(r_value) = value;
	return true;
}

const GDExtensionPropertyInfo *instance_get_property_list(GDExtensionScriptInstanceDataPtr p_instance, uint32_t *r_count) {
	MochaScriptInstance *instance = instance_of(p_instance);
	// The C list borrows the names from this list and the matching free callback destroys it, so it
	// has to outlive the call rather than live on this frame.
	List<PropertyInfo> *properties = memnew(List<PropertyInfo>);
	if (instance != nullptr && instance->script.is_valid()) {
		const std::shared_ptr<CompiledModule> module = instance->script->retain_compiled_module();
		if (module) for (const CompiledMember &member : module->members) {
			const uint32_t usage = PROPERTY_USAGE_SCRIPT_VARIABLE |
					(member.type.kind == TypeKind::ANY ? PROPERTY_USAGE_NIL_IS_VARIANT : 0);
			properties->push_back(PropertyInfo(carrier_for_type(member.type), member.name, PROPERTY_HINT_NONE, "", usage,
					member.type.kind == TypeKind::OBJECT ? member.type.name : StringName()));
		}
	}
	return godot::internal::create_c_property_list(properties, r_count);
}

void instance_free_property_list(GDExtensionScriptInstanceDataPtr, const GDExtensionPropertyInfo *p_list, uint32_t) {
	godot::internal::free_c_property_list(const_cast<GDExtensionPropertyInfo *>(p_list));
}

GDExtensionBool instance_get_class_category(GDExtensionScriptInstanceDataPtr, GDExtensionPropertyInfo *) {
	// The inspector's per-script category is editor presentation, which the runtime does not supply.
	return false;
}

GDExtensionVariantType instance_get_property_type(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) {
	MochaScriptInstance *instance = instance_of(p_instance);
	if (instance != nullptr && instance->script.is_valid()) {
		const int64_t index = instance->script->get_member_index(*reinterpret_cast<const StringName *>(p_name));
		const TypeRef *type = instance->script->get_member_type(index);
		if (type != nullptr) {
			*r_is_valid = true;
			return GDExtensionVariantType(carrier_for_type(*type));
		}
	}
	*r_is_valid = false;
	return GDEXTENSION_VARIANT_TYPE_NIL;
}

GDExtensionBool instance_validate_property(GDExtensionScriptInstanceDataPtr, GDExtensionPropertyInfo *) {
	// No property is rewritten on its way to the inspector.
	return false;
}

GDExtensionBool instance_property_can_revert(GDExtensionScriptInstanceDataPtr, GDExtensionConstStringNamePtr) {
	return false;
}

GDExtensionBool instance_property_get_revert(GDExtensionScriptInstanceDataPtr, GDExtensionConstStringNamePtr, GDExtensionVariantPtr) {
	return false;
}

GDExtensionObjectPtr instance_get_owner(GDExtensionScriptInstanceDataPtr p_instance) {
	MochaScriptInstance *instance = instance_of(p_instance);
	return instance != nullptr && instance->owner != nullptr ? instance->owner->_owner : nullptr;
}

void instance_get_property_state(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionScriptInstancePropertyStateAdd p_add, void *p_userdata) {
	MochaScriptInstance *instance = instance_of(p_instance);
	if (instance == nullptr || instance->script.is_null()) {
		return;
	}
	for (int i = 0; i < instance->script->get_member_count() && i < instance->members.size(); i++) {
		const StringName name = instance->script->get_member_name(i);
		p_add(&name, &instance->members[i], p_userdata);
	}
}

const GDExtensionMethodInfo *instance_get_method_list(GDExtensionScriptInstanceDataPtr p_instance, uint32_t *r_count) {
	MochaScriptInstance *instance = instance_of(p_instance);
	const std::shared_ptr<CompiledModule> module = instance != nullptr && instance->script.is_valid()
			? instance->script->retain_compiled_module()
			: nullptr;
	*r_count = module ? uint32_t(module->functions.size()) : 0;
	if (!module || module->functions.empty()) {
		return nullptr;
	}
	GDExtensionMethodInfo *list = memnew_arr(GDExtensionMethodInfo, module->functions.size());
	for (size_t i = 0; i < module->functions.size(); i++) {
		const CompiledFunction &function = module->functions[i];
		GDExtensionMethodInfo &entry = list[i];
		entry = {};
		entry.name = memnew(StringName(function.name));
		populate_value_info(entry.return_value, StringName(), function.return_type);
		entry.flags = METHOD_FLAG_NORMAL;
		entry.argument_count = uint32_t(function.parameter_types.size());
		entry.arguments = entry.argument_count > 0 ? memnew_arr(GDExtensionPropertyInfo, entry.argument_count) : nullptr;
		for (uint32_t argument = 0; argument < entry.argument_count; argument++) {
			populate_value_info(entry.arguments[argument], function.parameter_names[argument], function.parameter_types[argument]);
		}
	}
	return list;
}

void instance_free_method_list(GDExtensionScriptInstanceDataPtr, const GDExtensionMethodInfo *p_list, uint32_t p_count) {
	if (p_list == nullptr) {
		return;
	}
	GDExtensionMethodInfo *list = const_cast<GDExtensionMethodInfo *>(p_list);
	for (uint32_t i = 0; i < p_count; i++) {
		memdelete(reinterpret_cast<StringName *>(list[i].name));
		memdelete(reinterpret_cast<StringName *>(list[i].return_value.name));
		memdelete(reinterpret_cast<StringName *>(list[i].return_value.class_name));
		memdelete(reinterpret_cast<String *>(list[i].return_value.hint_string));
		for (uint32_t argument = 0; argument < list[i].argument_count; argument++) {
			memdelete(reinterpret_cast<StringName *>(list[i].arguments[argument].name));
			memdelete(reinterpret_cast<StringName *>(list[i].arguments[argument].class_name));
			memdelete(reinterpret_cast<String *>(list[i].arguments[argument].hint_string));
		}
		if (list[i].arguments != nullptr) {
			memdelete_arr(list[i].arguments);
		}
	}
	memdelete_arr(list);
}

GDExtensionBool instance_has_method(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name) {
	MochaScriptInstance *instance = instance_of(p_instance);
	return instance != nullptr && instance->has_method(*reinterpret_cast<const StringName *>(p_name));
}

GDExtensionInt instance_get_method_argument_count(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) {
	MochaScriptInstance *instance = instance_of(p_instance);
	if (instance != nullptr && instance->script.is_valid()) {
		if (const CompiledFunction *function = instance->script->find_function(*reinterpret_cast<const StringName *>(p_name))) {
			*r_is_valid = true;
			return int64_t(function->parameter_types.size());
		}
	}
	*r_is_valid = false;
	return 0;
}

void instance_call(GDExtensionScriptInstanceDataPtr p_self, GDExtensionConstStringNamePtr p_method,
		const GDExtensionConstVariantPtr *p_arguments, GDExtensionInt p_argument_count,
		GDExtensionVariantPtr r_return, GDExtensionCallError *r_error) {
	MochaScriptInstance *instance = instance_of(p_self);
	if (instance == nullptr) {
		r_error->error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		*reinterpret_cast<Variant *>(r_return) = Variant();
		return;
	}
	*reinterpret_cast<Variant *>(r_return) = instance->call(*reinterpret_cast<const StringName *>(p_method),
			reinterpret_cast<const Variant **>(const_cast<GDExtensionConstVariantPtr *>(p_arguments)), (int)p_argument_count, *r_error);
}

void instance_notification(GDExtensionScriptInstanceDataPtr p_instance, int32_t p_what, GDExtensionBool p_reversed) {
	MochaScriptInstance *instance = instance_of(p_instance);
	if (instance != nullptr) {
		instance->notification(p_what, p_reversed);
	}
}

void instance_to_string(GDExtensionScriptInstanceDataPtr, GDExtensionBool *r_is_valid, GDExtensionStringPtr) {
	// Until MochaScript supports a user `_to_string`, defer to Godot's native
	// Object/Node representation so identity and node names are preserved.
	*r_is_valid = false;
}

void instance_refcount_incremented(GDExtensionScriptInstanceDataPtr) {}

GDExtensionBool instance_refcount_decremented(GDExtensionScriptInstanceDataPtr) {
	// The instance holds no strong reference to its owner, so the owner's own count decides.
	return true;
}

GDExtensionObjectPtr instance_get_script(GDExtensionScriptInstanceDataPtr p_instance) {
	MochaScriptInstance *instance = instance_of(p_instance);
	return instance != nullptr && instance->script.is_valid() ? instance->script->_owner : nullptr;
}

GDExtensionBool instance_is_placeholder(GDExtensionScriptInstanceDataPtr) {
	return false;
}

GDExtensionBool instance_set_fallback(GDExtensionScriptInstanceDataPtr, GDExtensionConstStringNamePtr, GDExtensionConstVariantPtr) {
	return false;
}

GDExtensionBool instance_get_fallback(GDExtensionScriptInstanceDataPtr, GDExtensionConstStringNamePtr, GDExtensionVariantPtr) {
	return false;
}

GDExtensionScriptLanguagePtr instance_get_language(GDExtensionScriptInstanceDataPtr) {
	MochaScriptLanguage *language = MochaScriptLanguage::get_singleton();
	return language != nullptr ? language->_owner : nullptr;
}

void instance_free(GDExtensionScriptInstanceDataPtr p_instance) {
	MochaScriptInstance *instance = instance_of(p_instance);
	if (instance != nullptr) {
		if (instance->active_calls > 0) {
			instance->free_requested = true;
		} else {
			memdelete(instance);
		}
	}
}

GDExtensionScriptInstanceInfo3 build_vtable() {
	GDExtensionScriptInstanceInfo3 info = {};
	info.set_func = instance_set;
	info.get_func = instance_get;
	info.get_property_list_func = instance_get_property_list;
	info.free_property_list_func = instance_free_property_list;
	info.get_class_category_func = instance_get_class_category;
	info.property_can_revert_func = instance_property_can_revert;
	info.property_get_revert_func = instance_property_get_revert;
	info.get_owner_func = instance_get_owner;
	info.get_property_state_func = instance_get_property_state;
	info.get_method_list_func = instance_get_method_list;
	info.free_method_list_func = instance_free_method_list;
	info.get_property_type_func = instance_get_property_type;
	info.validate_property_func = instance_validate_property;
	info.has_method_func = instance_has_method;
	info.get_method_argument_count_func = instance_get_method_argument_count;
	info.call_func = instance_call;
	info.notification_func = instance_notification;
	info.to_string_func = instance_to_string;
	info.refcount_incremented_func = instance_refcount_incremented;
	info.refcount_decremented_func = instance_refcount_decremented;
	info.get_script_func = instance_get_script;
	info.is_placeholder_func = instance_is_placeholder;
	info.set_fallback_func = instance_set_fallback;
	info.get_fallback_func = instance_get_fallback;
	info.get_language_func = instance_get_language;
	info.free_func = instance_free;
	return info;
}

} // namespace

const GDExtensionScriptInstanceInfo3 *MochaScriptInstance::get_vtable() {
	// Built on first use rather than in a static initializer: nothing here is engine-backed, but the
	// rule that keeps Godot values out of static initialization is easiest to keep by not having any.
	static const GDExtensionScriptInstanceInfo3 vtable = build_vtable();
	return &vtable;
}

bool MochaScriptInstance::vtable_is_complete() {
	const GDExtensionScriptInstanceInfo3 *vtable = get_vtable();
	// Every callback the engine may invoke, in the order the struct declares them. A null slot here
	// would be a null-pointer call the first time the engine asked for it.
	return vtable->set_func && vtable->get_func && vtable->get_property_list_func &&
			vtable->free_property_list_func && vtable->get_class_category_func &&
			vtable->property_can_revert_func && vtable->property_get_revert_func &&
			vtable->get_owner_func && vtable->get_property_state_func &&
			vtable->get_method_list_func && vtable->free_method_list_func &&
			vtable->get_property_type_func && vtable->validate_property_func &&
			vtable->has_method_func && vtable->get_method_argument_count_func &&
			vtable->call_func && vtable->notification_func && vtable->to_string_func &&
			vtable->refcount_incremented_func && vtable->refcount_decremented_func &&
			vtable->get_script_func && vtable->is_placeholder_func && vtable->set_fallback_func &&
			vtable->get_fallback_func && vtable->get_language_func && vtable->free_func;
}

MochaScriptInstance::~MochaScriptInstance() {
	if (script.is_valid()) script->notify_instance_freed(this);
}

bool MochaScriptInstance::set_property(const StringName &p_name, const Variant &p_value) {
	if (script.is_null()) {
		return false;
	}
	const int index = script->get_member_index(p_name);
	if (index < 0 || index >= members.size()) {
		return false;
	}
	// A store from outside a compiled function meets the same declaration a compiled store does: a
	// declared carrier takes the value converted, or refuses it. Storing whatever arrived would let
	// the inspector or a scene put a value in a slot the script's own code cannot produce.
	Variant converted;
	const TypeRef *type = script->get_member_type(index);
	if (type == nullptr || !MochaVM::accepts(p_value, *type, converted)) {
		return false;
	}
	members.write[index] = converted;
	return true;
}

bool MochaScriptInstance::get_property(const StringName &p_name, Variant &r_value) const {
	if (script.is_null()) {
		return false;
	}
	const int index = script->get_member_index(p_name);
	if (index < 0 || index >= members.size()) {
		return false;
	}
	r_value = members[index];
	return true;
}

bool MochaScriptInstance::has_method(const StringName &p_name) const {
	return script.is_valid() && script->find_function(p_name) != nullptr;
}

Variant MochaScriptInstance::call(const StringName &p_method, const Variant **p_arguments, int p_argument_count, GDExtensionCallError &r_error) {
	struct ActiveCall {
		MochaScriptInstance *instance;
		explicit ActiveCall(MochaScriptInstance *p_instance) : instance(p_instance) { instance->active_calls++; }
		~ActiveCall() {
			instance->active_calls--;
			if (instance->active_calls == 0 && instance->free_requested) memdelete(instance);
		}
	} active(this);
	const uint64_t call_owner_id = owner != nullptr ? owner->get_instance_id() : 0;
	r_error.error = GDEXTENSION_CALL_OK;
	if (script.is_null()) {
		r_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		return Variant();
	}
	const std::shared_ptr<CompiledModule> module = script->retain_compiled_module();
	if (!module) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return Variant();
	}
	const CompiledFunction *function = module->find_function(p_method);
	if (function == nullptr) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return Variant();
	}
	RuntimeFailure failure;
	Variant result = MochaVM::call(*module, *function, owner, members,
			p_arguments, p_argument_count, r_error, failure);
	if (!failure.message.is_empty()) {
		if (!failure.reported) {
			UtilityFunctions::push_error(vformat("%s:%d:%d: %s: %s", script->get_path(), failure.span.line,
					failure.span.column, failure.code, failure.message));
			failure.reported = true;
		}
		MochaVM::publish_nested_failure(failure, call_owner_id, p_method);
	}
	return result;
}

void MochaScriptInstance::notification(int p_what, bool p_reversed) {
	struct ActiveNotification {
		MochaScriptInstance *instance;
		explicit ActiveNotification(MochaScriptInstance *p_instance) : instance(p_instance) { instance->active_calls++; }
		~ActiveNotification() {
			instance->active_calls--;
			if (instance->active_calls == 0 && instance->free_requested) memdelete(instance);
		}
	} active(this);
	if (script.is_null()) {
		return;
	}
	// Godot delivers a notification to the whole script chain; `p_reversed` names the direction the
	// engine walked it in, and is passed on unchanged so a script can tell the two apart.
	const StringName notification_name("_notification");
	const std::shared_ptr<CompiledModule> module = script->retain_compiled_module();
	const CompiledFunction *function = module ? module->find_function(notification_name) : nullptr;
	if (function == nullptr) {
		return;
	}
	const Variant what = p_what;
	const Variant reversed = p_reversed;
	const Variant *arguments[2] = { &what, &reversed };
	GDExtensionCallError error;
	const int argument_count = int(function->parameter_types.size());
	if (argument_count < 1 || argument_count > 2) {
		UtilityFunctions::push_error(vformat("%s: _notification must declare one or two parameters.", script->get_path()));
		return;
	}
	call(notification_name, arguments, argument_count, error);
	if (error.error != GDEXTENSION_CALL_OK) {
		UtilityFunctions::push_error(vformat("%s: _notification call failed.", script->get_path()));
	}
}

} // namespace mocha_script
