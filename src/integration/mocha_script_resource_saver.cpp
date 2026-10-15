/**************************************************************************/
/*  mocha_script_resource_saver.cpp                                       */
/*                                                                        */
/*  Copyright (c) 2026 Cafecito Games LLC.                                */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "mocha_script_resource_saver.h"

#include "mocha_script.h"

#include <godot_cpp/classes/file_access.hpp>

namespace mocha_script {

void MochaScriptResourceSaver::_bind_methods() {}

godot::Error MochaScriptResourceSaver::_save(const godot::Ref<godot::Resource> &p_resource, const godot::String &p_path, uint32_t) {
	const godot::Ref<MochaScript> script(p_resource);
	if (script.is_null()) return godot::ERR_INVALID_PARAMETER;

	godot::Ref<godot::FileAccess> file = godot::FileAccess::open(p_path, godot::FileAccess::WRITE);
	if (file.is_null()) return godot::FileAccess::get_open_error();
	if (!file->store_string(script->_get_source_code())) {
		file->close();
		return godot::ERR_FILE_CANT_WRITE;
	}
	file->flush();
	const godot::Error write_error = file->get_error();
	file->close();
	return write_error == godot::OK ? godot::OK : godot::ERR_FILE_CANT_WRITE;
}

bool MochaScriptResourceSaver::_recognize(const godot::Ref<godot::Resource> &p_resource) const {
	return godot::Ref<MochaScript>(p_resource).is_valid();
}

godot::PackedStringArray MochaScriptResourceSaver::_get_recognized_extensions(const godot::Ref<godot::Resource> &p_resource) const {
	godot::PackedStringArray extensions;
	if (_recognize(p_resource)) extensions.push_back("mocha");
	return extensions;
}

} // namespace mocha_script
