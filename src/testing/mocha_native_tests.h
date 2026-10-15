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
	bool mismatched_failure_probe() const;
	bool multiple_failure_probe() const;
	bool shielded_failure_probe() const;
	bool initializer_mismatch_probe() const;
	int64_t get_shielded_value() const;

public:
	godot::PackedStringArray run() const;
};

} // namespace mocha_script
