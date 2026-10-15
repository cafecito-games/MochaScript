/**************************************************************************/
/*  mocha_analyzer.h                                                      */
/**************************************************************************/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "language/mocha_ast.h"

namespace mocha_script {

class MochaAnalyzer {
	struct Context {
		HashMap<StringName, TypeRef> locals;
		TypeRef return_type;
	};

	Program *program = nullptr;
	String path;
	HashMap<StringName, TypeRef> members;
	std::vector<Diagnostic> diagnostics;

	void report(const SourceSpan &p_span, const String &p_code, const String &p_message);
	const FunctionDecl *find_function(const StringName &p_name) const;
	TypeRef analyze_expression(ExprPtr &p_expression, Context &r_context, bool p_value_required = true);
	void analyze_statement(StmtPtr &p_statement, Context &r_context);
	void analyze_block(std::vector<StmtPtr> &p_block, Context &r_context);
	bool assignable(const TypeRef &p_target, const TypeRef &p_value) const;
	String type_name(const TypeRef &p_type) const;
	void validate_type(const TypeRef &p_type, const SourceSpan &p_span, const String &p_context, bool p_allow_void = false);

public:
	Error analyze(Program &p_program, const String &p_path);
	const std::vector<Diagnostic> &get_diagnostics() const { return diagnostics; }
};

} // namespace mocha_script
