/**************************************************************************/
/*  mocha_parser.h                                                        */
/**************************************************************************/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "language/mocha_ast.h"

namespace mocha_script {

class MochaParser {
	String path;
	std::vector<MochaTokenizer::Token> tokens;
	size_t current = 0;
	std::vector<Diagnostic> diagnostics;

	const MochaTokenizer::Token &peek(int p_offset = 0) const;
	const MochaTokenizer::Token &previous() const;
	bool at_end() const;
	bool check(MochaTokenizer::Token::Type p_type) const;
	bool match(MochaTokenizer::Token::Type p_type);
	const MochaTokenizer::Token &advance();
	const MochaTokenizer::Token &consume(MochaTokenizer::Token::Type p_type, const String &p_message);
	void error_at(const MochaTokenizer::Token &p_token, const String &p_code, const String &p_message);
	void synchronize_line();
	void skip_newlines();
	bool consume_line_end();
	bool consume_identifier(StringName &r_name, const String &p_context);

	TypeRef parse_type();
	bool parse_member(Program &r_program);
	bool parse_function(Program &r_program);
	std::vector<StmtPtr> parse_suite();
	StmtPtr parse_statement();
	StmtPtr parse_variable_statement();
	StmtPtr parse_if_statement();
	StmtPtr parse_while_statement();
	StmtPtr parse_return_statement();

	ExprPtr parse_expression(int p_min_precedence = 1);
	ExprPtr parse_unary();
	ExprPtr parse_postfix();
	ExprPtr parse_primary();
	int binary_precedence(MochaTokenizer::Token::Type p_type) const;
	bool is_assignment(MochaTokenizer::Token::Type p_type) const;

public:
	Error parse(const String &p_source, const String &p_path, Program &r_program);
	const std::vector<Diagnostic> &get_diagnostics() const { return diagnostics; }
	int64_t get_token_count() const { return int64_t(tokens.size()); }
};

} // namespace mocha_script
