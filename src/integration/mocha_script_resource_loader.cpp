/**************************************************************************/
/*  mocha_script_resource_loader.cpp                                      */
/*                                                                        */
/*  Copyright (c) 2026 Cafecito Games LLC.                                */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "mocha_script_resource_loader.h"

#include "mocha_script.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

namespace mocha_script {

namespace {

bool is_mocha_path(const godot::String &p_path) {
	return p_path.get_extension().to_lower() == "mocha";
}

} // namespace

void MochaScriptResourceLoader::_bind_methods() {}

godot::PackedStringArray MochaScriptResourceLoader::_get_recognized_extensions() const {
	godot::PackedStringArray extensions;
	extensions.push_back("mocha");
	return extensions;
}

bool MochaScriptResourceLoader::_recognize_path(const godot::String &p_path, const godot::StringName &p_type) const {
	return is_mocha_path(p_path) && (p_type.is_empty() || _handles_type(p_type));
}

bool MochaScriptResourceLoader::_handles_type(const godot::StringName &p_type) const {
	return p_type == godot::StringName("Script") || p_type == godot::StringName("MochaScript");
}

godot::String MochaScriptResourceLoader::_get_resource_type(const godot::String &p_path) const {
	return is_mocha_path(p_path) ? godot::String("MochaScript") : godot::String();
}

godot::String MochaScriptResourceLoader::_get_resource_script_class(const godot::String &) const { return {}; }
godot::PackedStringArray MochaScriptResourceLoader::_get_dependencies(const godot::String &, bool) const { return {}; }
godot::Error MochaScriptResourceLoader::_rename_dependencies(const godot::String &, const godot::Dictionary &) const { return godot::OK; }
bool MochaScriptResourceLoader::_exists(const godot::String &p_path) const { return godot::FileAccess::file_exists(p_path); }
godot::PackedStringArray MochaScriptResourceLoader::_get_classes_used(const godot::String &) const { return {}; }

godot::Variant MochaScriptResourceLoader::_load(const godot::String &p_path, const godot::String &p_original_path, bool, int32_t p_cache_mode) const {
	godot::Ref<godot::FileAccess> file = godot::FileAccess::open(p_path, godot::FileAccess::READ);
	if (file.is_null()) {
		return godot::FileAccess::get_open_error();
	}

	const godot::String source = file->get_as_text();
	const godot::Error read_error = file->get_error();
	if (read_error != godot::OK && read_error != godot::ERR_FILE_EOF) {
		return read_error;
	}

	const godot::String resource_path = p_original_path.is_empty() ? p_path : p_original_path;
	const bool replacing = p_cache_mode == godot::ResourceLoader::CACHE_MODE_REPLACE ||
			p_cache_mode == godot::ResourceLoader::CACHE_MODE_REPLACE_DEEP;
	if (replacing) {
		godot::Ref<godot::Resource> cached_resource = godot::ResourceLoader::get_singleton()->get_cached_ref(resource_path);
		godot::Ref<MochaScript> cached_script(cached_resource);
		if (cached_script.is_valid()) {
			cached_script->set_diagnostic_path_for_validation(resource_path);
			if (!cached_script->try_replace_source_code(source)) {
				return godot::ERR_ALREADY_IN_USE;
			}
			return cached_script;
		}
	}

	godot::Ref<MochaScript> script;
	script.instantiate();
	script->_set_source_code(source);
	script->set_diagnostic_path_for_validation(resource_path);
	// Invalid source remains inspectable as a Script resource. The error and its
	// stable diagnostics are exposed by MochaScript rather than being replaced by
	// a loader-level integer that loses source positions.
	script->_reload(false);
	return script;
}

} // namespace mocha_script
