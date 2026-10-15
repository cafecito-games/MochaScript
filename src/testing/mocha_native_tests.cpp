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
#include "runtime/mocha_vm.h"

#include <godot_cpp/core/class_db.hpp>

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

	const godot::String executable =
			"extends RefCounted\n"
			"var seed: int = 2\n"
			"func add(value: int) -> int:\n"
			"\tvar numbers: Array = [value, seed]\n"
			"\tif numbers[0] > 0:\n"
			"\t\treturn numbers[0] + numbers[1]\n"
			"\treturn 0\n";
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
					const CompiledFunction *function = module->find_function("add");
					const godot::Variant argument = int64_t(40);
					const godot::Variant *arguments[] = { &argument };
					GDExtensionCallError call_error;
					const godot::Variant result = MochaVM::call(*module, *function, owner.ptr(), members, arguments, 1, call_error, failure);
					if (call_error.error != GDEXTENSION_CALL_OK || result != godot::Variant(int64_t(42))) {
						failures.push_back(godot::vformat("vm: expected 42, received %s", result));
					}
				}
			}
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
