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
#include "integration/mocha_script.h"
#include "integration/mocha_script_language.h"
#include "integration/mocha_script_resource_loader.h"
#include "runtime/mocha_vm.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

#include <limits>

namespace mocha_script {

void MochaNativeTests::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("run"), &MochaNativeTests::run);
	godot::ClassDB::bind_method(godot::D_METHOD("mismatched_failure_probe"), &MochaNativeTests::mismatched_failure_probe);
	godot::ClassDB::bind_method(godot::D_METHOD("multiple_failure_probe"), &MochaNativeTests::multiple_failure_probe);
	godot::ClassDB::bind_method(godot::D_METHOD("shielded_failure_probe"), &MochaNativeTests::shielded_failure_probe);
	godot::ClassDB::bind_method(godot::D_METHOD("initializer_mismatch_probe"), &MochaNativeTests::initializer_mismatch_probe);
}

bool MochaNativeTests::mismatched_failure_probe() const {
	RuntimeFailure failure;
	failure.code = "M4001";
	failure.message = "unrelated callback failure";
	failure.path = "res://native_callback_probe.mocha";
	failure.span = { 3, 4, 3, 8 };
	return MochaVM::publish_nested_failure(failure, get_instance_id(), "_notification");
}

bool MochaNativeTests::multiple_failure_probe() const {
	RuntimeFailure first;
	first.code = "M4001";
	first.message = "first direct failure";
	first.path = "res://native_callback_probe.mocha";
	first.span = { 5, 6, 5, 10 };
	RuntimeFailure second = first;
	second.message = "second direct failure";
	const bool captured_first = MochaVM::publish_nested_failure(first, get_instance_id(), "multiple_failure_probe");
	const bool captured_second = MochaVM::publish_nested_failure(second, get_instance_id(), "multiple_failure_probe");
	return captured_first && !captured_second;
}

bool MochaNativeTests::shielded_failure_probe() const {
	MochaNestedFailureShield shield;
	RuntimeFailure failure;
	failure.code = "M4001";
	failure.message = "shielded callback failure";
	return MochaVM::publish_nested_failure(failure, get_instance_id(), "shielded_failure_probe");
}

bool MochaNativeTests::initializer_mismatch_probe() const {
	RuntimeFailure failure;
	failure.code = "M4001";
	failure.message = "unrelated initializer failure";
	return MochaVM::publish_nested_failure(failure, get_instance_id(), StringName());
}

godot::PackedStringArray MochaNativeTests::run() const {
	godot::PackedStringArray failures;

	{
		RuntimeFailure failure;
		failure.code = "M4001";
		failure.message = "nested failure";
		failure.path = "res://origin.mocha";
		failure.span = { 3, 4, 3, 8 };
		failure.call_path = "res://caller.mocha";
		failure.call_span = { 9, 10, 9, 14 };
		if (MochaVM::format_failure(failure, "res://fallback.mocha") !=
				"res://origin.mocha:3:4: M4001: nested failure (called from res://caller.mocha:9:10)") {
			failures.push_back("vm: shared runtime failure formatting lost its origin or call site");
		}
	}

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
		MochaTokenizerText tokenizer;
		tokenizer.set_source_code("var café = 1\n");
		bool rejected = false;
		for (;;) {
			const MochaTokenizer::Token token = tokenizer.scan();
			if (token.type == MochaTokenizer::Token::ERROR) {
				rejected = godot::String(token.literal).contains("ASCII-only");
				break;
			}
			if (token.type == MochaTokenizer::Token::TK_EOF) break;
		}
		if (!rejected) failures.push_back("tokenizer: non-ASCII identifier was not deterministically rejected");
	}

	{
		MochaTokenizerText tokenizer;
		tokenizer.set_source_code("@café\n");
		const MochaTokenizer::Token token = tokenizer.scan();
		if (token.type != MochaTokenizer::Token::ERROR ||
				!godot::String(token.literal).contains("ASCII-only")) {
			failures.push_back("tokenizer: non-ASCII annotation identifier was not lexically rejected");
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

	{
		const godot::String path = "user://mocha_native_live_replace.mocha";
		godot::Ref<godot::FileAccess> file = godot::FileAccess::open(path, godot::FileAccess::WRITE);
		if (file.is_null()) {
			failures.push_back("loader: could not create live-replacement fixture");
		} else {
			file->store_string("extends Node\nfunc value() -> int:\n\treturn 1\n");
			file.unref();
			godot::Ref<godot::Resource> resource = godot::ResourceLoader::get_singleton()->load(path);
			godot::Ref<MochaScript> script(resource);
			godot::Node *host = memnew(godot::Node);
			if (script.is_null()) {
				failures.push_back("loader: could not cache live-replacement fixture");
			} else {
				host->set_script(script);
				file = godot::FileAccess::open(path, godot::FileAccess::WRITE);
				if (file.is_null()) {
					failures.push_back("loader: could not update live-replacement fixture");
				} else {
					file->store_string("extends Node\nfunc value() -> int:\n\treturn 2\n");
					file.unref();
					godot::Ref<MochaScriptResourceLoader> loader;
					loader.instantiate();
					const godot::Variant result = loader->_load(path, {}, false, godot::ResourceLoader::CACHE_MODE_REPLACE);
					const godot::TypedArray<godot::Dictionary> diagnostics = script->get_diagnostics();
					const godot::Dictionary source_error = script->get_last_source_error();
					if (result.get_type() != godot::Variant::INT || int64_t(result) != godot::ERR_ALREADY_IN_USE ||
							!script->_get_source_code().contains("return 1") || !script->is_compilation_valid() ||
							!diagnostics.is_empty() || source_error.get("code", "") != "M0003" ||
							source_error.get("path", "") != path) {
						failures.push_back("loader: live CACHE_MODE_REPLACE did not report ERR_ALREADY_IN_USE while preserving compiled source");
					}
				}
			}
			memdelete(host);
		}
	}

	{
		godot::Ref<MochaScript> moved;
		moved.instantiate();
		moved->set_diagnostic_path_for_validation("res://old_diagnostic_path.mocha");
		moved->_set_source_code("extends RefCounted\n");
		moved->_reload(false);
		moved->set_path("user://mocha_native_moved_path.mocha");
		moved->_set_source_code("extends RefCounted\nfunc broken(\n");
		const godot::TypedArray<godot::Dictionary> diagnostics = moved->get_diagnostics();
		const godot::Dictionary first = diagnostics.is_empty() ? godot::Dictionary() : godot::Dictionary(diagnostics[0]);
		if (first.get("path", "") != "user://mocha_native_moved_path.mocha") {
			failures.push_back("script: recompilation after a move retained its old diagnostic path");
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
			"func dynamic_modulo(value: Variant) -> Variant:\n"
			"\treturn value % 5\n"
			"func dynamic_modulo_assign(value: Variant) -> Variant:\n"
			"\tvalue %= 5\n"
			"\treturn value\n"
			"func dynamic_shift_left(value: Variant) -> Variant:\n"
			"\treturn 1 << value\n"
			"func dynamic_shift_right(value: Variant) -> Variant:\n"
			"\treturn 8 >> value\n"
			"func dynamic_bit_and(value: Variant) -> Variant:\n"
			"\treturn 3 & value\n"
			"func dynamic_bit_or(value: Variant) -> Variant:\n"
			"\treturn 3 | value\n"
			"func dynamic_bit_xor(value: Variant) -> Variant:\n"
			"\treturn 3 ^ value\n"
			"func dynamic_bit_not(value: Variant) -> Variant:\n"
			"\treturn ~value\n"
			"func negative_shift_left() -> int:\n"
			"\treturn -1 << 1\n"
			"func negative_shift_right() -> int:\n"
			"\treturn -4 >> 1\n"
			"func wrapping_shift_left() -> int:\n"
			"\treturn 1 << 63\n"
			"func public_match() -> bool:\n"
			"\treturn \"abc\".match(\"a*\")\n"
			"func forward_wrong(value: Variant) -> int:\n"
			"\treturn add(value)\n"
			"func power_associativity() -> int:\n"
			"\treturn 2 ** 2 ** 3\n"
			"func power_unary_precedence() -> int:\n"
			"\treturn -2 ** 2\n"
			"func power_explicit_unary() -> int:\n"
			"\treturn - 2 ** 2\n"
			"func power_integer_division() -> int:\n"
			"\treturn 2 ** 3 / 3\n"
			"func power_negative_exponent() -> int:\n"
			"\treturn 2 ** -1\n"
			"func power_modulo() -> int:\n"
			"\tvar value: int = 2 ** 3\n"
			"\treturn value % 3\n"
			"func omitted_return_type():\n"
			"\treturn 7\n"
			"func exceed_operation_budget() -> void:\n"
			"\twhile touched < 200000:\n"
			"\t\ttouched += 1\n"
			"func recurse() -> int:\n"
			"\treturn recurse()\n"
			"func explicit_forward_wrong() -> int:\n"
			"\treturn self.add(\"wrong\")\n"
			"func indirect_failure() -> Variant:\n"
			"\treturn self.call(\"dynamic_modulo\", \"%d\")\n"
			"func callback_mismatch() -> bool:\n"
			"\treturn mismatched_failure_probe()\n"
			"func callback_first_failure() -> int:\n"
			"\tmultiple_failure_probe()\n"
			"\treturn 1\n"
			"func callback_shielded() -> bool:\n"
			"\treturn shielded_failure_probe()\n"
			"func indirect_failure_callv() -> Variant:\n"
			"\treturn self.callv(\"dynamic_modulo\", [\"%d\"])\n"
			"func indirect_set_script(script: Variant) -> Variant:\n"
			"\treturn self.call(\"set_script\", script)\n"
			"func indirect_set_script_callv(script: Variant) -> Variant:\n"
			"\treturn self.callv(\"set_script\", [script])\n"
			"func callback_initializer_mismatch() -> bool:\n"
			"\treturn initializer_mismatch_probe()\n";
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
					auto failure_at = [](const RuntimeFailure &p_failure, int p_line, int p_column) {
						return p_failure.span.line == p_line && p_failure.span.column == p_column;
					};
					auto called_at = [](const RuntimeFailure &p_failure, const godot::String &p_path,
							int p_line, int p_column) {
						return p_failure.call_path == p_path && p_failure.call_span.line == p_line &&
								p_failure.call_span.column == p_column;
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
					invoke("dynamic_modulo", { godot::String("%d") }, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || failure.message.is_empty() || !failure_at(failure, 58, 9)) failures.push_back("vm: dynamic String modulo did not fail at 58:9");
					invoke("dynamic_modulo_assign", { godot::String("%d") }, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || failure.message.is_empty() || !failure_at(failure, 60, 2)) failures.push_back("vm: dynamic String modulo assignment did not fail at 60:2");
					struct DynamicIntegerFailure {
						godot::StringName method;
						int line;
					};
					for (const DynamicIntegerFailure &expected : {
							DynamicIntegerFailure{ "dynamic_shift_left", 63 }, DynamicIntegerFailure{ "dynamic_shift_right", 65 },
							DynamicIntegerFailure{ "dynamic_bit_and", 67 }, DynamicIntegerFailure{ "dynamic_bit_or", 69 },
							DynamicIntegerFailure{ "dynamic_bit_xor", 71 }, DynamicIntegerFailure{ "dynamic_bit_not", 73 } }) {
						for (const godot::Variant &invalid_operand : { godot::Variant("not an int"), godot::Variant(1.5) }) {
							invoke(expected.method, { invalid_operand }, call_error, failure);
							if (call_error.error != GDEXTENSION_CALL_OK || failure.code != "M4001" ||
									failure.message.is_empty() || !failure_at(failure, expected.line, 9)) {
								failures.push_back("vm: dynamic integer operator had the wrong M4001 span: " + godot::String(expected.method));
							}
						}
					}
					for (const int64_t invalid_count : { int64_t(-1), int64_t(64) }) {
						invoke("dynamic_shift_left", { invalid_count }, call_error, failure);
						if (call_error.error != GDEXTENSION_CALL_OK || failure.code != "M4001" ||
								!failure.message.contains("between 0 and 63") || !failure_at(failure, 63, 9)) {
							failures.push_back("vm: invalid left-shift count did not fail at 63:9");
						}
						invoke("dynamic_shift_right", { invalid_count }, call_error, failure);
						if (call_error.error != GDEXTENSION_CALL_OK || failure.code != "M4001" ||
								!failure.message.contains("between 0 and 63") || !failure_at(failure, 65, 9)) {
							failures.push_back("vm: invalid right-shift count did not fail at 65:9");
						}
					}
					result = invoke("negative_shift_left", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(-2))) failures.push_back("vm: negative left shift was not defined as a two's-complement bit shift");
					result = invoke("negative_shift_right", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(-2))) failures.push_back("vm: negative right shift was not defined as arithmetic shift");
					result = invoke("wrapping_shift_left", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(std::numeric_limits<int64_t>::min())) failures.push_back("vm: left shift did not use defined 64-bit wrapping semantics");
					result = invoke("public_match", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(true)) failures.push_back("parser: contextual member name 'match' was rejected");
					result = invoke("power_associativity", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(64))) failures.push_back("parser: exponentiation was not left-associative");
					result = invoke("power_unary_precedence", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(4))) failures.push_back("parser: signed numeric literal precedence diverged from the baseline");
					result = invoke("power_explicit_unary", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(-4))) failures.push_back("parser: explicit unary minus did not bind below exponentiation");
					result = invoke("power_integer_division", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(2))) failures.push_back("analyzer: integer power did not preserve integer division");
					result = invoke("power_negative_exponent", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(0))) failures.push_back("analyzer: integer negative power did not preserve its runtime carrier");
					result = invoke("power_modulo", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(2))) failures.push_back("analyzer: integer power could not flow into int/modulo");
					result = invoke("omitted_return_type", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(7))) failures.push_back("analyzer: omitted return type was not Variant");
					invoke("add", { godot::String("wrong") }, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT || call_error.argument != 0 ||
							call_error.expected != GDEXTENSION_VARIANT_TYPE_INT || failure.code != "M4001" ||
							!failure_at(failure, 11, 1)) {
						failures.push_back("vm: wrong external argument did not preserve its typed Godot call error and Mocha location");
					}
					invoke("forward_wrong", { godot::String("wrong") }, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || failure.code != "M4001" || !failure_at(failure, 83, 9)) failures.push_back("vm: nested wrong argument did not fail at 83:9");
					godot::Ref<MochaScript> nested_script;
					nested_script.instantiate();
					nested_script->set_diagnostic_path_for_validation("res://native_nested_runtime.mocha");
					nested_script->_set_source_code(executable);
					godot::Ref<godot::RefCounted> nested_owner;
					nested_owner.instantiate();
					if (nested_script->_reload(false) != godot::OK) {
						failures.push_back("vm: explicit nested-call fixture did not compile");
					} else {
						nested_owner->set_script(nested_script);
						godot::Vector<godot::Variant> nested_members;
						RuntimeFailure nested_initialization_failure;
						if (nested_owner->get_script() != nested_script ||
								MochaVM::initialize(*module, nested_owner.ptr(), nested_members, nested_initialization_failure) != godot::OK) {
							failures.push_back("vm: explicit nested-call fixture did not initialize");
						} else {
							MochaVM::call(*module, *module->find_function("explicit_forward_wrong"), nested_owner.ptr(), nested_members,
									nullptr, 0, call_error, failure);
							if (call_error.error != GDEXTENSION_CALL_OK || failure.code != "M4001" ||
									failure.preserve_call_error || failure.path != "res://native_nested_runtime.mocha" ||
									!failure_at(failure, 11, 1) ||
									!called_at(failure, "res://native_pipeline.mocha", 105, 9)) {
								failures.push_back("vm: explicit nested wrong argument leaked unsafe host call metadata");
							}
							MochaVM::call(*module, *module->find_function("indirect_failure"), nested_owner.ptr(), nested_members,
									nullptr, 0, call_error, failure);
							if (call_error.error != GDEXTENSION_CALL_OK || failure.code != "M4001" ||
									failure.path != "res://native_nested_runtime.mocha" || !failure_at(failure, 58, 9) ||
									!called_at(failure, "res://native_pipeline.mocha", 107, 9)) {
								failures.push_back("vm: indirect nested call treated a script failure as a successful nil result");
							}
							MochaVM::call(*module, *module->find_function("indirect_failure_callv"), nested_owner.ptr(), nested_members,
									nullptr, 0, call_error, failure);
							if (call_error.error != GDEXTENSION_CALL_OK || failure.code != "M4001" ||
									failure.path != "res://native_nested_runtime.mocha" || !failure_at(failure, 58, 9) ||
									!called_at(failure, "res://native_pipeline.mocha", 116, 9)) {
								failures.push_back("vm: callv nested failure was treated as a successful nil result");
							}
							godot::Ref<MochaScript> broken_initializer_script;
							broken_initializer_script.instantiate();
							broken_initializer_script->set_diagnostic_path_for_validation("res://native_broken_initializer.mocha");
							broken_initializer_script->_set_source_code(
									"extends RefCounted\nvar dynamic: Variant = null\nvar typed: String = dynamic\n");
							if (broken_initializer_script->_reload(false) != godot::OK) {
								failures.push_back("vm: broken-initializer runtime fixture did not compile");
							} else {
								const godot::Variant broken_script_value = broken_initializer_script;
								const godot::Variant *broken_script_arguments[1] = { &broken_script_value };
								MochaVM::call(*module, *module->find_function("indirect_set_script"), nested_owner.ptr(), nested_members,
										broken_script_arguments, 1, call_error, failure);
								if (call_error.error != GDEXTENSION_CALL_OK || failure.code != "M4001" ||
										failure.path != "res://native_broken_initializer.mocha" || !failure_at(failure, 3, 1) ||
										!called_at(failure, "res://native_pipeline.mocha", 118, 9)) {
									failures.push_back("vm: call(set_script) did not propagate an initializer failure");
								}
								nested_owner->set_script(nested_script);
								MochaVM::call(*module, *module->find_function("indirect_set_script_callv"), nested_owner.ptr(), nested_members,
										broken_script_arguments, 1, call_error, failure);
								if (call_error.error != GDEXTENSION_CALL_OK || failure.code != "M4001" ||
										failure.path != "res://native_broken_initializer.mocha" || !failure_at(failure, 3, 1) ||
										!called_at(failure, "res://native_pipeline.mocha", 120, 9)) {
									failures.push_back("vm: callv(set_script) did not propagate an initializer failure");
								}
							}
						}
						nested_owner->set_script(godot::Variant());
					}
					godot::Vector<godot::Variant> callback_members;
					RuntimeFailure callback_initialization_failure;
					auto *callback_owner = const_cast<MochaNativeTests *>(this);
					if (MochaVM::initialize(*module, callback_owner, callback_members, callback_initialization_failure) != godot::OK) {
						failures.push_back("vm: callback-capture fixture did not initialize");
					} else {
						result = MochaVM::call(*module, *module->find_function("callback_mismatch"), callback_owner,
								callback_members, nullptr, 0, call_error, failure);
						if (call_error.error != GDEXTENSION_CALL_OK || !failure.message.is_empty() || result != godot::Variant(false)) {
							failures.push_back("vm: an unrelated callback claimed a direct method failure capture");
						}
						MochaVM::call(*module, *module->find_function("callback_first_failure"), callback_owner,
								callback_members, nullptr, 0, call_error, failure);
						if (call_error.error != GDEXTENSION_CALL_OK || failure.message != "first direct failure" ||
								failure.path != "res://native_callback_probe.mocha" || !failure_at(failure, 5, 6) ||
								!called_at(failure, "res://native_pipeline.mocha", 111, 2)) {
							failures.push_back("vm: direct failure capture did not preserve the first matching failure");
						}
						result = MochaVM::call(*module, *module->find_function("callback_shielded"), callback_owner,
								callback_members, nullptr, 0, call_error, failure);
						if (call_error.error != GDEXTENSION_CALL_OK || !failure.message.is_empty() || result != godot::Variant(false)) {
							failures.push_back("vm: a shielded engine callback claimed an outer failure capture");
						}
						result = MochaVM::call(*module, *module->find_function("callback_initializer_mismatch"), callback_owner,
								callback_members, nullptr, 0, call_error, failure);
						if (call_error.error != GDEXTENSION_CALL_OK || !failure.message.is_empty() || result != godot::Variant(false)) {
							failures.push_back("vm: an unrelated initializer failure claimed a non-set_script capture");
						}
					}
					invoke("dynamic_store", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || failure.message.is_empty() || !failure_at(failure, 46, 2)) failures.push_back("vm: dynamic store failure exposed an unsafe Godot call error or wrong span");
					invoke("missing", { false }, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || failure.message.is_empty() || !failure_at(failure, 40, 1)) failures.push_back("vm: typed fallthrough was not reported at 40:1");
					invoke("exceed_operation_budget", {}, call_error, failure);
					const int64_t touched_after_budget = members[module->find_member("touched")];
					if (call_error.error != GDEXTENSION_CALL_OK || failure.code != "M4001" ||
							!failure.message.contains("one million") || !failure_at(failure, 100, 18) ||
							touched_after_budget <= 0 || touched_after_budget >= 200000) {
						failures.push_back("vm: operation budget lacked its deterministic failure or mutation semantics");
					}
					invoke("recurse", {}, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || !failure.message.contains("depth") || !failure_at(failure, 103, 9)) failures.push_back("vm: recursion limit did not fail at 103:9");
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
		Program invalid;
		MochaParser invalid_parser;
		invalid_parser.parse("var items = [items]\n", "res://native_self_inferred_member.mocha", invalid);
		MochaAnalyzer analyzer;
		if (analyzer.analyze(invalid, "res://native_self_inferred_member.mocha") != godot::ERR_COMPILATION_FAILED ||
				analyzer.get_diagnostics().empty() || analyzer.get_diagnostics()[0].code != "M2032" ||
				analyzer.get_diagnostics()[0].span.line != 1 || analyzer.get_diagnostics()[0].span.column != 14) {
			failures.push_back("analyzer: self-read of an uninferred member did not produce M2032 at 1:14");
		}
	}

	{
		Program invalid;
		MochaParser invalid_parser;
		invalid_parser.parse("var value: int = read()\nfunc read() -> int:\n\treturn self.value\n", "res://native_indirect_self_initializer.mocha", invalid);
		MochaAnalyzer analyzer;
		if (analyzer.analyze(invalid, "res://native_indirect_self_initializer.mocha") != godot::ERR_COMPILATION_FAILED) {
			failures.push_back("analyzer: implicit-self call in a member initializer was accepted");
		} else {
			bool found = false;
			for (const Diagnostic &diagnostic : analyzer.get_diagnostics()) found |= diagnostic.code == "M2030";
			if (!found) failures.push_back("analyzer: implicit-self initializer call did not produce M2030");
		}
	}

	{
		Program invalid;
		MochaParser invalid_parser;
		invalid_parser.parse("var first: String = later\nvar later = 1\n", "res://native_forward_inferred_member.mocha", invalid);
		MochaAnalyzer analyzer;
		if (analyzer.analyze(invalid, "res://native_forward_inferred_member.mocha") != godot::ERR_COMPILATION_FAILED) {
			failures.push_back("analyzer: forward read of an uninferred member was accepted");
		} else {
			bool found = false;
			for (const Diagnostic &diagnostic : analyzer.get_diagnostics()) {
				found |= diagnostic.code == "M2032" && diagnostic.span.line == 1 && diagnostic.span.column == 21;
			}
			if (!found) failures.push_back("analyzer: forward uninferred member read did not produce M2032 at 1:21");
		}
	}

	{
		Program valid;
		MochaParser parser;
		const godot::String source =
				"var flexible: Variant = 1\n"
				"func change() -> Variant:\n"
				"\tflexible = \"member\"\n"
				"\tvar local: Variant = 2\n"
				"\tlocal = \"local\"\n"
				"\treturn local\n";
		MochaAnalyzer analyzer;
		if (parser.parse(source, "res://native_explicit_variant.mocha", valid) != godot::OK ||
				analyzer.analyze(valid, "res://native_explicit_variant.mocha") != godot::OK ||
				valid.members.empty() || valid.members[0].type.kind != TypeKind::ANY ||
				valid.functions.empty() || valid.functions[0].body.size() < 2 ||
				valid.functions[0].body[1]->declared_type.kind != TypeKind::ANY) {
			failures.push_back("analyzer: explicit Variant declaration was narrowed by inference");
		}
	}

	{
		for (const godot::String &keyword : { godot::String("match"), godot::String("when") }) {
			Program invalid;
			MochaParser parser;
			const godot::String source = "func bad(" + keyword + ": Variant) -> Variant:\n\treturn " + keyword + "\n";
			if (parser.parse(source, "res://native_reserved_keyword.mocha", invalid) != godot::ERR_PARSE_ERROR ||
					parser.get_diagnostics().empty() || parser.get_diagnostics()[0].code != "M1014") {
				failures.push_back("parser: contextual keyword '" + keyword + "' did not produce M1014");
			}
		}
	}

	{
		const godot::String collection_types =
				"var values: Array[int]\n"
				"var lookup: Dictionary[String, int]\n";
		Program valid;
		MochaParser parser;
		if (parser.parse(collection_types, "res://native_collection_types.mocha", valid) != godot::OK) {
			failures.push_back("parser: valid erased collection element syntax was rejected");
		} else {
			MochaAnalyzer analyzer;
			if (analyzer.analyze(valid, "res://native_collection_types.mocha") != godot::OK) {
				failures.push_back("analyzer: valid erased collection element types were rejected");
			}
		}

		Program invalid;
		MochaParser invalid_parser;
		if (invalid_parser.parse("var value: int[String]\n", "res://native_noncollection_type.mocha", invalid) != godot::ERR_PARSE_ERROR ||
				invalid_parser.get_diagnostics().empty() || invalid_parser.get_diagnostics()[0].code != "M1012") {
			failures.push_back("parser: non-collection element syntax did not produce M1012");
		}
	}

	{
		struct InvalidElementTypeCase { const char *source; const char *code; const char *label; };
		const InvalidElementTypeCase cases[] = {
			{ "var values: Array[void]\n", "M2026", "void Array element" },
			{ "var values: Dictionary[String, DefinitelyNotAClass]\n", "M2024", "unknown Dictionary value" },
		};
		for (const InvalidElementTypeCase &test_case : cases) {
			Program invalid;
			MochaParser parser;
			if (parser.parse(test_case.source, "res://native_invalid_element_type.mocha", invalid) != godot::OK) {
				failures.push_back(godot::String("parser: ") + test_case.label + " failed before analysis");
				continue;
			}
			MochaAnalyzer analyzer;
			if (analyzer.analyze(invalid, "res://native_invalid_element_type.mocha") != godot::ERR_COMPILATION_FAILED) {
				failures.push_back(godot::String("analyzer: ") + test_case.label + " was accepted");
				continue;
			}
			bool found = false;
			for (const Diagnostic &diagnostic : analyzer.get_diagnostics()) found |= diagnostic.code == test_case.code;
			if (!found) failures.push_back(godot::String("analyzer: ") + test_case.label + " did not produce " + test_case.code);
		}
	}

	{
		for (const godot::String &operation : { godot::String("**="), godot::String("<<="), godot::String(">>="),
				godot::String("&="), godot::String("|="), godot::String("^=") }) {
			Program invalid;
			MochaParser parser;
			const godot::String source = "func bad() -> void:\n\tvar value: int = 1\n\tvalue " + operation + " 2\n";
			if (parser.parse(source, "res://native_unsupported_compound.mocha", invalid) != godot::ERR_PARSE_ERROR) {
				failures.push_back("parser: unsupported compound assignment was accepted: " + operation);
				continue;
			}
			bool found = false;
			for (const Diagnostic &diagnostic : parser.get_diagnostics()) found |= diagnostic.code == "M1090";
			if (!found) failures.push_back("parser: unsupported compound assignment lacked M1090: " + operation);
		}
	}

	{
		auto expect_analyzer_error = [&](const godot::String &p_source, const godot::String &p_code, const godot::String &p_case) {
			Program invalid;
			MochaParser parser;
			if (parser.parse(p_source, "res://native_static_rule.mocha", invalid) != godot::OK) {
				failures.push_back("analyzer: " + p_case + " fixture failed before analysis");
				return;
			}
			MochaAnalyzer analyzer;
			if (analyzer.analyze(invalid, "res://native_static_rule.mocha") != godot::ERR_COMPILATION_FAILED) {
				failures.push_back("analyzer: " + p_case + " was accepted");
				return;
			}
			bool found = false;
			for (const Diagnostic &diagnostic : analyzer.get_diagnostics()) found |= diagnostic.code == p_code;
			if (!found) failures.push_back("analyzer: " + p_case + " did not produce " + p_code);
		};
		expect_analyzer_error("func bad() -> void:\n\tvar value = 1\n\tvalue = \"text\"\n", "M2013", "inferred local reassignment");
		expect_analyzer_error("func bad() -> void:\n\tif 1:\n\t\tpass\n", "M2015", "non-bool condition");
		expect_analyzer_error("func bad() -> bool:\n\treturn 1 and true\n", "M2003", "non-bool logical operands");
		expect_analyzer_error("func bad() -> bool:\n\treturn not 1\n", "M2002", "non-bool logical negation");
		expect_analyzer_error("func bad() -> Variant:\n\treturn \"%s\" % \"text\"\n", "M2003", "String percent formatting");
		expect_analyzer_error("func bad() -> void:\n\tvar text: String = \"a\"\n\ttext %= \"b\"\n", "M2003", "String compound percent formatting");
		expect_analyzer_error("func mocha_language_version() -> String:\n\treturn \"shadow\"\n", "M2029", "reserved intrinsic declaration");
		expect_analyzer_error("func bad(value: Variant) -> bool:\n\treturn value is Node\n", "M2091", "unsupported is operator");
		expect_analyzer_error("extends Node\nfunc bad() -> Resource:\n\treturn self\n", "M2014", "incompatible object return");
	}

	{
		Program invalid;
		MochaParser parser;
		if (parser.parse("func bad(PI: float) -> float:\n\treturn PI\n", "res://native_reserved_constant.mocha", invalid) != godot::ERR_PARSE_ERROR ||
				parser.get_diagnostics().empty() || parser.get_diagnostics()[0].code != "M1013") {
			failures.push_back("parser: built-in constant declaration did not produce M1013");
		}
	}

	{
		Program invalid;
		MochaParser invalid_parser;
		invalid_parser.parse("extends Node\nvar name: String\n", "res://native_property_collision.mocha", invalid);
		MochaAnalyzer analyzer;
		if (analyzer.analyze(invalid, "res://native_property_collision.mocha") != godot::ERR_COMPILATION_FAILED) {
			failures.push_back("analyzer: native base property collision was accepted");
		} else {
			bool found = false;
			for (const Diagnostic &diagnostic : analyzer.get_diagnostics()) found |= diagnostic.code == "M2031";
			if (!found) failures.push_back("analyzer: native base property collision did not produce M2031");
		}
	}

	{
		for (const godot::String &keyword : { godot::String("break"), godot::String("continue"), godot::String("assert"), godot::String("breakpoint") }) {
			Program invalid;
			MochaParser parser;
			const godot::String source = "func bad() -> void:\n\t" + keyword + "\n";
			if (parser.parse(source, "res://native_unsupported_statement.mocha", invalid) != godot::ERR_PARSE_ERROR ||
					parser.get_diagnostics().empty() || parser.get_diagnostics()[0].code != "M1090") {
				failures.push_back("parser: unsupported '" + keyword + "' statement did not produce M1090");
			}
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
		if (parser.parse(source, "res://native_deep_ast.mocha", invalid) != godot::ERR_PARSE_ERROR) {
			failures.push_back("parser: excessively deep flat expression tree was accepted");
		} else {
			bool found = false;
			bool spurious_line_end = false;
			for (const Diagnostic &diagnostic : parser.get_diagnostics()) {
				found |= diagnostic.code == "M1009";
				spurious_line_end |= diagnostic.code == "M1002";
			}
			if (!found) failures.push_back("parser: flat expression tree did not produce M1009");
			if (spurious_line_end) failures.push_back("parser: flat expression recovery produced a spurious M1002");
		}
	}

	{
		godot::String source = "var value: ";
		for (int i = 0; i < 300; ++i) source += "Array[";
		source += "int";
		for (int i = 0; i < 300; ++i) source += "]";
		source += "\n";
		Program invalid;
		MochaParser parser;
		if (parser.parse(source, "res://native_deep_type.mocha", invalid) != godot::ERR_PARSE_ERROR) {
			failures.push_back("parser: excessively nested type was accepted");
		} else {
			bool found = false;
			for (const Diagnostic &diagnostic : parser.get_diagnostics()) found |= diagnostic.code == "M1011";
			if (!found) failures.push_back("parser: nested type did not produce M1011");
		}
	}

	{
		godot::String source = "func deep_container() -> Variant:\n\treturn ";
		for (int i = 0; i < 80; ++i) source += "[";
		source += "1";
		for (int i = 0; i < 200; ++i) source += "+1";
		for (int i = 0; i < 80; ++i) source += "]";
		source += "\n";
		Program invalid;
		MochaParser parser;
		if (parser.parse(source, "res://native_deep_container.mocha", invalid) != godot::ERR_PARSE_ERROR) {
			failures.push_back("parser: container-wrapped deep expression was accepted");
		} else {
			bool found = false;
			for (const Diagnostic &diagnostic : parser.get_diagnostics()) found |= diagnostic.code == "M1009";
			if (!found) failures.push_back("parser: deep container expression did not produce M1009");
		}
	}

	{
		godot::String source = "func too_nested() -> void:\n";
		godot::String indent = "\t";
		for (int i = 0; i < 300; ++i) {
			source += indent + "if true:\n";
			indent += "\t";
		}
		source += indent + "pass\n";
		Program invalid;
		MochaParser parser;
		if (parser.parse(source, "res://native_deep_statements.mocha", invalid) != godot::ERR_PARSE_ERROR) {
			failures.push_back("parser: excessively nested statements were accepted");
		} else {
			bool found = false;
			for (const Diagnostic &diagnostic : parser.get_diagnostics()) found |= diagnostic.code == "M1010";
			if (!found) failures.push_back("parser: nested statements did not produce M1010");
		}
	}

	{
		godot::String source = "func too_many_branches() -> void:\n\tif false:\n\t\tpass\n";
		for (int i = 0; i < 300; ++i) source += "\telif false:\n\t\tpass\n";
		Program invalid;
		MochaParser parser;
		if (parser.parse(source, "res://native_deep_elif.mocha", invalid) != godot::ERR_PARSE_ERROR) {
			failures.push_back("parser: excessively long elif ladder was accepted");
		} else {
			bool found = false;
			for (const Diagnostic &diagnostic : parser.get_diagnostics()) found |= diagnostic.code == "M1010";
			if (!found) failures.push_back("parser: elif ladder did not produce M1010");
		}
	}

	{
		godot::String source = "func combined_depth() -> void:\n";
		godot::String indent = "\t";
		for (int i = 0; i < 200; ++i) {
			source += indent + "if true:\n";
			indent += "\t";
		}
		source += indent + "if false:\n" + indent + "\tpass\n";
		for (int i = 0; i < 100; ++i) source += indent + "elif false:\n" + indent + "\tpass\n";
		Program invalid;
		MochaParser parser;
		if (parser.parse(source, "res://native_combined_statement_depth.mocha", invalid) != godot::ERR_PARSE_ERROR) {
			failures.push_back("parser: combined suite and elif depth was accepted");
		} else {
			bool found = false;
			for (const Diagnostic &diagnostic : parser.get_diagnostics()) found |= diagnostic.code == "M1010";
			if (!found) failures.push_back("parser: combined suite and elif depth did not produce M1010");
		}
	}

	{
		struct InvalidOperatorCase { const char *source; const char *code; const char *label; };
		const InvalidOperatorCase cases[] = {
			{ "func bad() -> Variant:\n\treturn 1.0 % 1.0\n", "M2003", "float modulo" },
			{ "func bad() -> Variant:\n\treturn 1.0 << 1\n", "M2003", "float shift" },
			{ "func bad() -> bool:\n\treturn \"a\" < 1\n", "M2003", "mixed ordering" },
			{ "func bad() -> bool:\n\treturn 1 == \"1\"\n", "M2003", "mixed equality" },
			{ "func bad() -> bool:\n\treturn 1 in 2\n", "M2003", "numeric membership" },
			{ "func bad() -> bool:\n\treturn 1 in \"abc\"\n", "M2003", "non-string String membership" },
			{ "func bad() -> Variant:\n\treturn ~1.0\n", "M2002", "float bitwise negation" },
			{ "func bad() -> void:\n\tvar text: String = \"a\"\n\ttext -= \"b\"\n", "M2003", "invalid compound operation" },
		};
		for (const InvalidOperatorCase &test_case : cases) {
			Program invalid;
			MochaParser parser;
			if (parser.parse(test_case.source, "res://native_invalid_operator.mocha", invalid) != godot::OK) {
				failures.push_back(godot::String("analyzer: ") + test_case.label + " failed before analysis");
				continue;
			}
			MochaAnalyzer analyzer;
			if (analyzer.analyze(invalid, "res://native_invalid_operator.mocha") != godot::ERR_COMPILATION_FAILED ||
					analyzer.get_diagnostics().size() != 1 || analyzer.get_diagnostics()[0].code != test_case.code) {
				failures.push_back(godot::String("analyzer: ") + test_case.label + " did not produce only " + test_case.code);
			}
		}
	}

	{
		godot::Node *freed = memnew(godot::Node);
		const godot::Variant stale = freed;
		memdelete(freed);
		godot::Variant converted;
		if (MochaVM::accepts(stale, TypeRef::make(TypeKind::OBJECT, "Node"), converted)) {
			failures.push_back("vm: freed object was accepted at a typed boundary");
		}
	}

	{
		Program invalid;
		MochaParser invalid_parser;
		invalid_parser.parse("var first: int = 1\nvar second: Variant = self.first\n", "res://native_self_initializer.mocha", invalid);
		MochaAnalyzer analyzer;
		if (analyzer.analyze(invalid, "res://native_self_initializer.mocha") != godot::ERR_COMPILATION_FAILED) {
			failures.push_back("analyzer: self-qualified member initializer was accepted");
		} else {
			bool found = false;
			for (const Diagnostic &diagnostic : analyzer.get_diagnostics()) found |= diagnostic.code == "M2030";
			if (!found) failures.push_back("analyzer: self-qualified member initializer did not produce M2030");
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
