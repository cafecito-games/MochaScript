/**************************************************************************/
/*  mocha_native_tests.h                                                  */
/**************************************************************************/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace mocha_script {

class MochaNativeTests final : public godot::RefCounted {
	GDCLASS(MochaNativeTests, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::PackedStringArray run() const;
};

} // namespace mocha_script
