/**************************************************************************/
/*  mocha_compiler.h                                                      */
/**************************************************************************/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "language/mocha_ast.h"
#include "runtime/mocha_bytecode.h"

namespace mocha_script {

class MochaCompiler {
	std::shared_ptr<CompiledModule> module;
	CompiledFunction *function = nullptr;
	HashMap<StringName, int64_t> locals;
	HashMap<StringName, int64_t> members;
	String path;
	std::vector<Diagnostic> diagnostics;

	void report(const SourceSpan &p_span, const String &p_code, const String &p_message);
	int64_t emit(OpCode p_opcode, const SourceSpan &p_span, int64_t p_argument = 0);
	void emit_default(const TypeRef &p_type, const SourceSpan &p_span);
	void compile_expression(const ExprPtr &p_expression);
	void compile_statement(const StmtPtr &p_statement);
	void compile_block(const std::vector<StmtPtr> &p_block);
	void compile_function(const FunctionDecl &p_function, CompiledFunction &r_function);
	MochaTokenizer::Token::Type compound_operation(MochaTokenizer::Token::Type p_operation) const;

public:
	Error compile(const Program &p_program, const String &p_path, std::shared_ptr<CompiledModule> &r_module);
	const std::vector<Diagnostic> &get_diagnostics() const { return diagnostics; }
};

} // namespace mocha_script
