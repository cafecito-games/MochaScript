/**************************************************************************/
/*  mocha_ast.h                                                           */
/**************************************************************************/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "language/mocha_tokenizer.h"

#include <memory>
#include <utility>
#include <vector>

namespace mocha_script {

struct SourceSpan {
	int line = 1;
	int column = 1;
	int end_line = 1;
	int end_column = 1;
};

struct Diagnostic {
	String code;
	String message;
	String path;
	SourceSpan span;
};

enum class TypeKind {
	ANY,
	NIL,
	BOOL,
	INT,
	FLOAT,
	STRING,
	ARRAY,
	DICTIONARY,
	OBJECT,
	VOID,
};

struct TypeRef {
	TypeKind kind = TypeKind::ANY;
	StringName name;
	SourceSpan span;
	// Collection parameters are retained for validation and future language
	// evolution even though the bootstrap runtime currently erases them.
	std::vector<TypeRef> parameters;

	static TypeRef any() { return {}; }
	static TypeRef make(TypeKind p_kind, const StringName &p_name = {}) {
		TypeRef type;
		type.kind = p_kind;
		type.name = p_name;
		return type;
	}
	bool operator==(const TypeRef &p_other) const {
		return kind == p_other.kind && name == p_other.name && parameters == p_other.parameters;
	}
	bool operator!=(const TypeRef &p_other) const { return !(*this == p_other); }
};

enum class ExprKind {
	LITERAL,
	IDENTIFIER,
	SELF,
	ARRAY,
	DICTIONARY,
	UNARY,
	BINARY,
	CALL,
	MEMBER,
	INDEX,
};

struct Expression;
using ExprPtr = std::shared_ptr<Expression>;

struct Expression {
	ExprKind kind = ExprKind::LITERAL;
	SourceSpan span;
	int tree_depth = 1;
	Variant literal;
	StringName name;
	MochaTokenizer::Token::Type operation = MochaTokenizer::Token::EMPTY;
	ExprPtr left;
	ExprPtr right;
	std::vector<ExprPtr> arguments;
	std::vector<std::pair<ExprPtr, ExprPtr>> entries;
	TypeRef inferred_type;
};

enum class StatementKind {
	VARIABLE,
	EXPRESSION,
	ASSIGNMENT,
	RETURN,
	IF,
	WHILE,
	PASS,
};

struct Statement;
using StmtPtr = std::shared_ptr<Statement>;

struct Statement {
	StatementKind kind = StatementKind::PASS;
	SourceSpan span;
	StringName name;
	TypeRef declared_type;
	MochaTokenizer::Token::Type operation = MochaTokenizer::Token::EMPTY;
	ExprPtr target;
	ExprPtr expression;
	std::vector<StmtPtr> body;
	std::vector<StmtPtr> else_body;
};

struct Parameter {
	StringName name;
	TypeRef type;
	SourceSpan span;
};

struct FunctionDecl {
	StringName name;
	std::vector<Parameter> parameters;
	TypeRef return_type = TypeRef::any();
	std::vector<StmtPtr> body;
	SourceSpan span;
};

struct MemberDecl {
	StringName name;
	TypeRef type;
	ExprPtr initializer;
	SourceSpan span;
};

struct Program {
	StringName base_type = "RefCounted";
	std::vector<MemberDecl> members;
	std::vector<FunctionDecl> functions;
};

inline SourceSpan span_from_token(const MochaTokenizer::Token &p_token) {
	return { p_token.start_line, p_token.start_column, p_token.end_line, p_token.end_column };
}

} // namespace mocha_script
