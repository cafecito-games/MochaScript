/**************************************************************************/
/*  mocha_vm.h                                                            */
/**************************************************************************/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "runtime/mocha_bytecode.h"

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/godot.hpp>

namespace mocha_script {

struct RuntimeFailure {
	String code;
	String message;
	SourceSpan span;
	bool reported = false;
};

class MochaVM {
	static Variant execute(const CompiledModule &p_module, const CompiledFunction &p_function,
			Object *p_owner, Vector<Variant> &r_members, const Variant **p_arguments,
			int p_argument_count, GDExtensionCallError &r_call_error,
			RuntimeFailure &r_failure);
	static bool convert_value(const Variant &p_value, const TypeRef &p_type, Variant &r_converted);
	static bool variant_operator(MochaTokenizer::Token::Type p_operation, Variant::Operator &r_operator);

public:
	static Error initialize(const CompiledModule &p_module, Object *p_owner,
			Vector<Variant> &r_members, RuntimeFailure &r_failure);
	static Variant call(const CompiledModule &p_module, const CompiledFunction &p_function,
			Object *p_owner, Vector<Variant> &r_members, const Variant **p_arguments,
			int p_argument_count, GDExtensionCallError &r_call_error,
			RuntimeFailure &r_failure);
	static bool accepts(const Variant &p_value, const TypeRef &p_type, Variant &r_converted) {
		return convert_value(p_value, p_type, r_converted);
	}
	// A script instance reached through a host `callp` reports a nested VM failure
	// here so the outer VM frame cannot mistake its nil result for success.
	static void publish_nested_failure(const RuntimeFailure &p_failure, uint64_t p_owner_id,
			const StringName &p_method);
};

} // namespace mocha_script
