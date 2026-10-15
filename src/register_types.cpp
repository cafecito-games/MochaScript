/**************************************************************************/
/*  register_types.cpp                                                    */
/*                                                                        */
/*  Copyright (c) 2026 Cafecito Games LLC.                                */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "register_types.h"

#include "integration/mocha_script.h"
#include "integration/mocha_script_language.h"
#include "integration/mocha_script_resource_loader.h"
#include "integration/mocha_script_resource_saver.h"
#include "integration/mocha_script_instance.h"
#include "testing/mocha_native_tests.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace {

mocha_script::MochaScriptLanguage *language = nullptr;
godot::Ref<mocha_script::MochaScriptResourceLoader> resource_loader;
godot::Ref<mocha_script::MochaScriptResourceSaver> resource_saver;

} // namespace

void initialize_mocha_script(godot::ModuleInitializationLevel p_level) {
	if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	if (!mocha_script::MochaScriptInstance::vtable_is_complete()) {
		godot::UtilityFunctions::push_error("MochaScript instance vtable is incomplete.");
		return;
	}

	GDREGISTER_CLASS(mocha_script::MochaScriptLanguage);
	GDREGISTER_CLASS(mocha_script::MochaScript);
	GDREGISTER_CLASS(mocha_script::MochaScriptResourceLoader);
	GDREGISTER_CLASS(mocha_script::MochaScriptResourceSaver);
	GDREGISTER_CLASS(mocha_script::MochaNativeTests);

	language = memnew(mocha_script::MochaScriptLanguage);
	const godot::Error registration_error = godot::Engine::get_singleton()->register_script_language(language);
	if (registration_error != godot::OK) {
		godot::UtilityFunctions::push_error(
				godot::vformat("Failed to register MochaScript language (error %d).", registration_error));
		memdelete(language);
		language = nullptr;
		return;
	}

	resource_loader.instantiate();
	godot::ResourceLoader::get_singleton()->add_resource_format_loader(resource_loader, true);
	resource_saver.instantiate();
	godot::ResourceSaver::get_singleton()->add_resource_format_saver(resource_saver, true);
}

void uninitialize_mocha_script(godot::ModuleInitializationLevel p_level) {
	if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	if (resource_loader.is_valid()) {
		godot::ResourceLoader::get_singleton()->remove_resource_format_loader(resource_loader);
		resource_loader.unref();
	}
	if (resource_saver.is_valid()) {
		godot::ResourceSaver::get_singleton()->remove_resource_format_saver(resource_saver);
		resource_saver.unref();
	}

	if (language != nullptr) {
		godot::Engine::get_singleton()->unregister_script_language(language);
		memdelete(language);
		language = nullptr;
	}
}

extern "C" {

GDExtensionBool GDE_EXPORT mocha_script_library_init(
		GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library,
		GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_object(p_get_proc_address, p_library, r_initialization);
	init_object.register_initializer(initialize_mocha_script);
	init_object.register_terminator(uninitialize_mocha_script);
	init_object.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_object.init();
}

} // extern "C"
