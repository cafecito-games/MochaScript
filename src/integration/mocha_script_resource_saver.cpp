/**************************************************************************/
/*  mocha_script_resource_saver.cpp                                       */
/*                                                                        */
/*  Copyright (c) 2026 Cafecito Games LLC.                                */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "mocha_script_resource_saver.h"

#include "mocha_script.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>

#include <atomic>

namespace mocha_script {

namespace {

std::atomic<uint64_t> save_nonce{ 0 };

} // namespace

void MochaScriptResourceSaver::_bind_methods() {}

godot::Error MochaScriptResourceSaver::_save(const godot::Ref<godot::Resource> &p_resource, const godot::String &p_path, uint32_t) {
	const godot::Ref<MochaScript> script(p_resource);
	if (script.is_null()) return godot::ERR_INVALID_PARAMETER;

	const godot::String directory_path = p_path.get_base_dir();
	godot::Ref<godot::DirAccess> directory = godot::DirAccess::open(directory_path);
	if (directory.is_null()) return godot::DirAccess::get_open_error();
	const godot::String temporary_name = p_path.get_file() + ".tmp." +
			godot::String::num_int64(godot::OS::get_singleton()->get_process_id()) + "." +
			godot::String::num_int64(save_nonce.fetch_add(1, std::memory_order_relaxed));
	const godot::String temporary_path = directory_path.path_join(temporary_name);
	const godot::String source = script->_get_source_code();
	auto discard_temporary = [&directory, &temporary_name]() {
		if (directory->file_exists(temporary_name)) directory->remove(temporary_name);
	};

	godot::Ref<godot::FileAccess> file = godot::FileAccess::open(temporary_path, godot::FileAccess::WRITE);
	if (file.is_null()) return godot::FileAccess::get_open_error();
	if (!file->store_string(source)) {
		file->close();
		discard_temporary();
		return godot::ERR_FILE_CANT_WRITE;
	}
	file->flush();
	const godot::Error write_error = file->get_error();
	file->close();
	if (write_error != godot::OK || !directory->file_exists(temporary_name)) {
		discard_temporary();
		return godot::ERR_FILE_CANT_WRITE;
	}

	godot::Ref<godot::FileAccess> persisted = godot::FileAccess::open(temporary_path, godot::FileAccess::READ);
	if (persisted.is_null() || persisted->get_as_text() != source) {
		if (persisted.is_valid()) persisted->close();
		discard_temporary();
		return godot::ERR_FILE_CANT_WRITE;
	}
	persisted->close();

	const godot::Error rename_error = directory->rename(temporary_name, p_path.get_file());
	if (rename_error != godot::OK) discard_temporary();
	return rename_error;
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
