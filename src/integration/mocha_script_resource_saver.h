/**************************************************************************/
/*  mocha_script_resource_saver.h                                         */
/*                                                                        */
/*  Copyright (c) 2026 Cafecito Games LLC.                                */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include <godot_cpp/classes/resource_format_saver.hpp>

namespace mocha_script {

class MochaScriptResourceSaver final : public godot::ResourceFormatSaver {
	GDCLASS(MochaScriptResourceSaver, godot::ResourceFormatSaver)

protected:
	static void _bind_methods();

public:
	godot::Error _save(const godot::Ref<godot::Resource> &p_resource, const godot::String &p_path, uint32_t p_flags) override;
	bool _recognize(const godot::Ref<godot::Resource> &p_resource) const override;
	godot::PackedStringArray _get_recognized_extensions(const godot::Ref<godot::Resource> &p_resource) const override;
};

} // namespace mocha_script
