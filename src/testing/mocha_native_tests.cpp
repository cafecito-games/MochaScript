/**************************************************************************/
/*  mocha_native_tests.cpp                                                */
/**************************************************************************/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "testing/mocha_native_tests.h"

#include "language/mocha_analyzer.h"
#include "language/mocha_compiler.h"
#include "language/mocha_parser.h"
#include "language/mocha_tokenizer.h"
#include "integration/mocha_script_language.h"
#include "runtime/mocha_vm.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

namespace mocha_script {

void MochaNativeTests::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("run"), &MochaNativeTests::run);
}

godot::PackedStringArray MochaNativeTests::run() const {
	godot::PackedStringArray failures;

	{
		MochaTokenizerText tokenizer;
		tokenizer.set_source_code("var answer: int = 42\n");
		const MochaTokenizer::Token::Type expected[] = {
			MochaTokenizer::Token::VAR, MochaTokenizer::Token::IDENTIFIER,
			MochaTokenizer::Token::COLON, MochaTokenizer::Token::IDENTIFIER,
			MochaTokenizer::Token::EQUAL, MochaTokenizer::Token::LITERAL,
			MochaTokenizer::Token::NEWLINE, MochaTokenizer::Token::TK_EOF,
		};
		for (const MochaTokenizer::Token::Type type : expected) {
			const MochaTokenizer::Token actual = tokenizer.scan();
			if (actual.type != type) {
				failures.push_back(godot::vformat("tokenizer: expected %s, received %s", MochaTokenizer::get_token_name(type), actual.get_debug_name()));
				break;
			}
		}
	}

	{
		// Keep a real resource cached at the validation path. Validation must use the
		// supplied diagnostic path without trying to occupy that ResourceCache key.
		const godot::Ref<godot::Resource> cached = godot::ResourceLoader::get_singleton()->load("res://example.mocha");
		MochaScriptLanguage *language = MochaScriptLanguage::get_singleton();
		if (cached.is_null() || language == nullptr) {
			failures.push_back("language: could not establish cached validation fixture");
		} else {
			const godot::Dictionary result = language->_validate("var label: String = 42\n", "res://example.mocha", true, true, true, true);
			const godot::TypedArray<godot::Dictionary> errors = result["errors"];
			const godot::Dictionary first_error = errors.is_empty() ? godot::Dictionary() : godot::Dictionary(errors[0]);
			if (bool(result["valid"]) || first_error.is_empty() || first_error.get("path", "") != "res://example.mocha") {
				failures.push_back("language: validation did not preserve the requested diagnostic path");
			}
		}
	}

	const godot::String executable =
			"extends RefCounted\n"
			"var seed: int = 2\n"
			"var default_int: int\n"
			"var default_float: float\n"
			"var default_string: String\n"
			"var default_array: Array\n"
			"var target = null\n"
			"var touched: int\n"
			"var forward: int = later\n"
			"var later: int = 3\n"
			"func add(value: int) -> int:\n"
			"\tvar numbers: Array = [value, seed]\n"
			"\tif numbers[0] > 0:\n"
			"\t\treturn numbers[0] + numbers[1]\n"
			"\treturn 0\n"
			"func half() -> float:\n"
			"\tvar value: float = 1\n"
			"\treturn value / 2.0\n"
			"func fresh_array_size() -> int:\n"
			"\tvar values: Array\n"
			"\tvalues.append(1)\n"
			"\treturn values.size()\n"
			"func initializer_scope() -> int:\n"
			"\tvar seed: int = seed\n"
			"\treturn seed\n"
			"func mark() -> bool:\n"
			"\ttouched += 1\n"
			"\treturn true\n"
			"func short_and() -> bool:\n"
			"\treturn false and mark()\n"
			"func short_or() -> bool:\n"
			"\treturn true or mark()\n"
			"func scoped(flag: bool) -> int:\n"
			"\tif flag:\n"
			"\t\tvar seed: int = 99\n"
			"\treturn seed\n"
			"func set_target(value: Variant) -> Variant:\n"
			"\ttarget = value\n"
			"\treturn target\n"
			"func missing(flag: bool) -> int:\n"
			"\tif flag:\n"
			"\t\treturn 1\n"
			"func not_rule() -> bool:\n"
			"\treturn not 1 == 1\n"
			"func dynamic_store() -> String:\n"
			"\tvar value: String = get_instance_id()\n"
			"\treturn value\n"
			"func class_text() -> String:\n"
			"\tvar value: String = get_class()\n"
			"\treturn value\n"
			"func dynamic_sum() -> Variant:\n"
			"\tvar values: Array = [1.25, 2.25]\n"
			"\tvar result = values[0] + values[1]\n"
			"\treturn result\n"
			"func dynamic_suffix(value: Variant) -> Variant:\n"
			"\treturn value + \"!\"\n"
			"func recurse() -> int:\n"
			"\treturn recurse()\n";
	Program program;
	MochaParser parser;
	if (parser.parse(executable, "res://native_pipeline.mocha", program) != godot::OK) {
		failures.push_back("parser: valid native pipeline fixture was rejected");
	} else {
		MochaAnalyzer analyzer;
		if (analyzer.analyze(program, "res://native_pipeline.mocha") != godot::OK) {
			failures.push_back("analyzer: valid native pipeline fixture was rejected");
		} else {
			MochaCompiler compiler;
			std::shared_ptr<CompiledModule> module;
			if (compiler.compile(program, "res://native_pipeline.mocha", module) != godot::OK || !module) {
				failures.push_back("compiler: valid native pipeline fixture was rejected");
			} else {
				godot::Ref<godot::RefCounted> owner;
				owner.instantiate();
				godot::Vector<godot::Variant> members;
				RuntimeFailure failure;
				if (MochaVM::initialize(*module, owner.ptr(), members, failure) != godot::OK) {
					failures.push_back("vm: member initialization failed: " + failure.message);
				} else {
					auto invoke = [&](const godot::StringName &p_name, const std::vector<godot::Variant> &p_values,
							GDExtensionCallError &r_error, RuntimeFailure &r_failure) {
						std::vector<const godot::Variant *> pointers;
						for (const godot::Variant &value : p_values) pointers.push_back(&value);
						return MochaVM::call(*module, *module->find_function(p_name), owner.ptr(), members,
								pointers.data(), int(pointers.size()), r_error, r_failure);
					};
					GDExtensionCallError call_error;
					godot::Variant result = invoke("add", { int64_t(40) }, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(42))) {
						failures.push_back(godot::vformat("vm: expected 42, received %s", result));
					}
					if (members[module->find_member("default_int")].get_type() != godot::Variant::INT ||
							members[module->find_member("default_float")].get_type() != godot::Variant::FLOAT ||
							members[module->find_member("default_string")].get_type() != godot::Variant::STRING ||
							members[module->find_member("default_array")].get_type() != godot::Variant::ARRAY) {
						failures.push_back("vm: typed declaration defaults have incorrect carriers");
					}
					godot::Vector<godot::Variant> second_members;
					RuntimeFailure second_failure;
					if (MochaVM::initialize(*module, owner.ptr(), second_members, second_failure) != godot::OK) {
						failures.push_back("vm: second member initialization failed");
					} else {
						godot::Array first_array = members[module->find_member("default_array")];
						first_array.push_back(1);
						godot::Array second_array = second_members[module->find_member("default_array")];
						if (!second_array.is_empty()) failures.push_back("vm: collection member default was shared between instances");
					}
					if (int64_t(members[module->find_member("forward")]) != 0) failures.push_back("vm: forward member read did not see its typed default");
					result = invoke("half", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(0.5)) failures.push_back("vm: local int-to-float widening failed");
					result = invoke("fresh_array_size", {}, call_error, failure);
					godot::Variant second_fresh = invoke("fresh_array_size", {}, call_error, failure);
					if (result != godot::Variant(int64_t(1)) || second_fresh != godot::Variant(int64_t(1))) failures.push_back("vm: collection local default was shared between calls");
					result = invoke("initializer_scope", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(2))) failures.push_back("compiler: local was visible inside its own initializer");
					result = invoke("short_and", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(false)) failures.push_back("compiler: short-circuit and failed");
					result = invoke("short_or", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(true) || int64_t(members[module->find_member("touched")]) != 0) failures.push_back("compiler: logical operators evaluated the guarded call");
					result = invoke("scoped", { true }, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(2))) failures.push_back("compiler: block local leaked over member resolution");
					result = invoke("set_target", { int64_t(7) }, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(7))) failures.push_back("analyzer: null inference did not remain Variant");
					result = invoke("not_rule", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(false)) failures.push_back("parser: 'not' comparison precedence is incorrect");
					result = invoke("class_text", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result.get_type() != godot::Variant::STRING) failures.push_back("vm: StringName was not converted for a String local");
					result = invoke("dynamic_sum", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(3.5)) failures.push_back("analyzer: Variant arithmetic was incorrectly narrowed");
					result = invoke("dynamic_suffix", { godot::String("Mocha") }, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant("Mocha!")) failures.push_back("analyzer: Variant plus String was rejected or narrowed");
					invoke("add", { godot::String("wrong") }, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT || failure.message.is_empty()) failures.push_back("vm: wrong external argument did not preserve INVALID_ARGUMENT");
					invoke("dynamic_store", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT || failure.message.is_empty()) failures.push_back("vm: dynamically wrong typed local store did not preserve INVALID_ARGUMENT");
					invoke("missing", { false }, call_error, failure);
					if (call_error.error == GDEXTENSION_CALL_OK || failure.message.is_empty()) failures.push_back("vm: typed function fell off the end successfully");
					invoke("recurse", {}, call_error, failure);
					if (call_error.error == GDEXTENSION_CALL_OK || !failure.message.contains("depth")) failures.push_back("vm: recursion limit did not propagate as a call failure");
				}
			}
		}
	}

	{
		Program invalid;
		MochaParser parser;
		if (parser.parse("extends RefCounted\n\tvar value = 1\n", "res://native_overindent.mocha", invalid) != godot::ERR_PARSE_ERROR ||
				parser.get_diagnostics().empty() || parser.get_diagnostics()[0].code != "M1008") {
			failures.push_back("parser: stray top-level indentation did not terminate with M1008");
		}
	}

	{
		godot::String source = "func too_deep() -> Variant:\n\treturn ";
		for (int i = 0; i < 300; ++i) source += "(";
		source += "1";
		for (int i = 0; i < 300; ++i) source += ")";
		source += "\n";
		Program invalid;
		MochaParser parser;
		if (parser.parse(source, "res://native_deep_expression.mocha", invalid) != godot::ERR_PARSE_ERROR) {
			failures.push_back("parser: excessively nested expression was accepted");
		} else {
			bool found = false;
			for (const Diagnostic &diagnostic : parser.get_diagnostics()) found |= diagnostic.code == "M1009";
			if (!found) failures.push_back("parser: deep expression did not produce M1009");
		}
	}

	{
		godot::String source = "func too_wide() -> Variant:\n\treturn 1";
		for (int i = 0; i < 300; ++i) source += "+1";
		source += "\n";
		Program invalid;
		MochaParser parser;
		if (parser.parse(source, "res://native_deep_ast.mocha", invalid) != godot::OK) {
			failures.push_back("parser: long flat expression fixture failed before analysis");
		} else {
			MochaAnalyzer analyzer;
			if (analyzer.analyze(invalid, "res://native_deep_ast.mocha") != godot::ERR_COMPILATION_FAILED) {
				failures.push_back("analyzer: excessively deep expression tree was accepted");
			} else {
				bool found = false;
				for (const Diagnostic &diagnostic : analyzer.get_diagnostics()) found |= diagnostic.code == "M2028";
				if (!found) failures.push_back("analyzer: deep expression tree did not produce M2028");
			}
		}
	}

	{
		Program invalid;
		MochaParser invalid_parser;
		invalid_parser.parse("func bad(value: void) -> void:\n\tpass\n", "res://native_void_parameter.mocha", invalid);
		MochaAnalyzer analyzer;
		if (analyzer.analyze(invalid, "res://native_void_parameter.mocha") != godot::ERR_COMPILATION_FAILED ||
				analyzer.get_diagnostics().empty() || analyzer.get_diagnostics()[0].code != "M2026") {
			failures.push_back("analyzer: void parameter did not produce M2026");
		}
	}

	{
		Program invalid;
		MochaParser invalid_parser;
		invalid_parser.parse("func noop() -> void:\n\tpass\nfunc bad() -> Variant:\n\treturn noop()\n", "res://native_void_value.mocha", invalid);
		MochaAnalyzer analyzer;
		if (analyzer.analyze(invalid, "res://native_void_value.mocha") != godot::ERR_COMPILATION_FAILED) {
			failures.push_back("analyzer: void expression was accepted as a value");
		} else {
			bool found = false;
			for (const Diagnostic &diagnostic : analyzer.get_diagnostics()) found |= diagnostic.code == "M2008";
			if (!found) failures.push_back("analyzer: void expression did not produce M2008");
		}
	}

	{
		Program invalid;
		MochaParser invalid_parser;
		invalid_parser.parse("func _notification(what: String) -> int:\n\treturn 0\n", "res://native_bad_notification.mocha", invalid);
		MochaAnalyzer analyzer;
		if (analyzer.analyze(invalid, "res://native_bad_notification.mocha") != godot::ERR_COMPILATION_FAILED) {
			failures.push_back("analyzer: invalid _notification signature was accepted");
		} else {
			bool found = false;
			for (const Diagnostic &diagnostic : analyzer.get_diagnostics()) found |= diagnostic.code == "M2027";
			if (!found) failures.push_back("analyzer: invalid _notification signature did not produce M2027");
		}
	}

	{
		Program invalid;
		MochaParser invalid_parser;
		invalid_parser.parse("var value: DefinitelyNotAClass\n", "res://native_unknown_type.mocha", invalid);
		MochaAnalyzer analyzer;
		if (analyzer.analyze(invalid, "res://native_unknown_type.mocha") != godot::ERR_COMPILATION_FAILED ||
				analyzer.get_diagnostics().empty() || analyzer.get_diagnostics()[0].code != "M2024") {
			failures.push_back("analyzer: unknown object type did not produce M2024");
		}
	}

	{
		Program invalid;
		MochaParser invalid_parser;
		if (invalid_parser.parse("var label: String = 42\n", "res://native_type_error.mocha", invalid) != godot::OK) {
			failures.push_back("analyzer: type-error fixture failed before analysis");
		} else {
			MochaAnalyzer analyzer;
			if (analyzer.analyze(invalid, "res://native_type_error.mocha") != godot::ERR_COMPILATION_FAILED ||
					analyzer.get_diagnostics().empty() || analyzer.get_diagnostics()[0].code != "M2022") {
				failures.push_back("analyzer: expected stable M2022 diagnostic");
			}
		}
	}

	return failures;
}

} // namespace mocha_script
