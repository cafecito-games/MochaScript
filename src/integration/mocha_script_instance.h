/**************************************************************************/
/*  mocha_script_instance.h                                                */
/*                                                                        */
/*  The one script-instance vtable in the extension. godot-cpp ships no   */
/*  ScriptInstanceExtension wrapper, so the instance is a plain struct    */
/*  plus a static GDExtensionScriptInstanceInfo3 built by hand and given  */
/*  to script_instance_create3. Adapted from BaristaScript's public       */
/*  GDExtension integration pattern; see NOTICE.md.                       */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of MochaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "platform/mocha_platform.h"
#include "runtime/mocha_vm.h"

namespace mocha_script {

class MochaScript;

/**
 * One running instance of a compiled script, attached to one owner object.
 *
 * The engine reaches it only through the static vtable, whose every callback is filled: a null
 * slot the engine may call is a null-pointer call, so the vtable is asserted complete at startup
 * rather than discovered incomplete at a call site.
 *
 * The struct owns the host pointer, a strong script reference, per-instance members, and deferred
 * self-free state. RPC configuration is answered at script level by
 * `MochaScript::_get_rpc_config`.
 */
class MochaScriptInstance {
public:
	Object *owner = nullptr;
	Ref<MochaScript> script;
	Vector<Variant> members;
	int active_calls = 0;
	bool free_requested = false;

	MochaScriptInstance() = default;
	~MochaScriptInstance();

	/** The vtable every MochaScript instance shares. Complete: no callback the engine may call is null. */
	static const GDExtensionScriptInstanceInfo3 *get_vtable();

	/** True when every callback slot the engine may call is filled. Checked at extension startup. */
	static bool vtable_is_complete();

	/** The instance attached to `p_owner`, or null when its script is not a MochaScript. */
	static MochaScriptInstance *from_owner(const Object *p_owner);

	Variant call(const StringName &p_method, const Variant **p_arguments, int p_argument_count, GDExtensionCallError &r_error);
	void notification(int p_what, bool p_reversed);
	bool set_property(const StringName &p_name, const Variant &p_value);
	bool get_property(const StringName &p_name, Variant &r_value) const;
	bool has_method(const StringName &p_name) const;
};

} // namespace mocha_script
