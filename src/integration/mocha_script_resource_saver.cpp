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
#include <godot_cpp/templates/hash_set.hpp>

#include <atomic>

namespace mocha_script {

namespace {

std::atomic<uint64_t> save_nonce{ 0 };

} // namespace

void MochaScriptResourceSaver::_bind_methods() {}

godot::Error MochaScriptResourceSaver::_save(const godot::Ref<godot::Resource> &p_resource, const godot::String &p_path, uint32_t) {
	const godot::Ref<MochaScript> script(p_resource);
	if (script.is_null()) return godot::ERR_INVALID_PARAMETER;

	godot::String target_path = p_path;
	godot::HashSet<godot::String> visited_links;
	bool target_resolved = false;
	for (int hop = 0; hop < 32; ++hop) {
		target_path = target_path.simplify_path();
		if (visited_links.has(target_path)) return godot::ERR_CYCLIC_LINK;
		visited_links.insert(target_path);
		godot::Ref<godot::DirAccess> link_directory = godot::DirAccess::open(target_path.get_base_dir());
		if (link_directory.is_null()) return godot::DirAccess::get_open_error();
		if (!link_directory->is_link(target_path.get_file())) {
			target_resolved = true;
			break;
		}
		godot::String link_target = link_directory->read_link(target_path.get_file());
		if (link_target.is_relative_path()) link_target = target_path.get_base_dir().path_join(link_target);
		target_path = link_target;
	}
	if (!target_resolved) return godot::ERR_CYCLIC_LINK;

	const godot::String directory_path = target_path.get_base_dir();
	godot::Ref<godot::DirAccess> directory = godot::DirAccess::open(directory_path);
	if (directory.is_null()) return godot::DirAccess::get_open_error();
	const godot::String temporary_name = "." + target_path.get_file() + "." +
			godot::String::num_int64(godot::OS::get_singleton()->get_process_id()) + "." +
			godot::String::num_int64(save_nonce.fetch_add(1, std::memory_order_relaxed)) + ".tmp";
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

	const godot::PackedByteArray expected_bytes = source.to_utf8_buffer();
	if (godot::FileAccess::get_size(temporary_path) != expected_bytes.size() ||
			godot::FileAccess::get_file_as_bytes(temporary_path) != expected_bytes) {
		discard_temporary();
		return godot::ERR_FILE_CANT_WRITE;
	}
	if (godot::FileAccess::file_exists(target_path)) {
		const godot::BitField<godot::FileAccess::UnixPermissionFlags> permissions = godot::FileAccess::get_unix_permissions(target_path);
		if (permissions != 0) {
			const godot::Error permission_error = godot::FileAccess::set_unix_permissions(temporary_path, permissions);
			if (permission_error != godot::OK && permission_error != godot::ERR_UNAVAILABLE) {
				discard_temporary();
				return permission_error;
			}
		}
	}

	const godot::Error rename_error = directory->rename(temporary_name, target_path.get_file());
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
