/**************************************************************************/
/*  mocha_parser.cpp                                                      */
/**************************************************************************/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "language/mocha_parser.h"

#include <limits>

namespace mocha_script {

namespace {

using Token = MochaTokenizer::Token;
constexpr int MAX_EXPRESSION_DEPTH = 256;
constexpr int MAX_STATEMENT_DEPTH = 256;
constexpr int MAX_TYPE_DEPTH = 256;

struct ExpressionDepthScope {
	int &depth;
	~ExpressionDepthScope() { depth--; }
};

struct StatementDepthScope {
	int &depth;
	~StatementDepthScope() { depth--; }
};

struct TypeDepthScope {
	int &depth;
	~TypeDepthScope() { depth--; }
};

SourceSpan merge_span(const SourceSpan &p_first, const SourceSpan &p_last) {
	return { p_first.line, p_first.column, p_last.end_line, p_last.end_column };
}

ExprPtr expression(ExprKind p_kind, const SourceSpan &p_span) {
	auto result = std::make_shared<Expression>();
	result->kind = p_kind;
	result->span = p_span;
	return result;
}

StmtPtr statement(StatementKind p_kind, const SourceSpan &p_span) {
	auto result = std::make_shared<Statement>();
	result->kind = p_kind;
	result->span = p_span;
	return result;
}

String token_text(const Token &p_token) {
	return p_token.source.is_empty() ? String(p_token.get_name()) : p_token.source;
}

} // namespace

const Token &MochaParser::peek(int p_offset) const {
	const size_t index = current + size_t(MAX(p_offset, 0));
	return tokens[index < tokens.size() ? index : tokens.size() - 1];
}

const Token &MochaParser::previous() const {
	return tokens[current == 0 ? 0 : current - 1];
}

bool MochaParser::at_end() const { return check(Token::TK_EOF); }
bool MochaParser::check(Token::Type p_type) const { return peek().type == p_type; }

bool MochaParser::match(Token::Type p_type) {
	if (!check(p_type)) {
		return false;
	}
	advance();
	return true;
}

const Token &MochaParser::advance() {
	if (!at_end()) {
		current++;
	}
	return previous();
}

const Token &MochaParser::consume(Token::Type p_type, const String &p_message) {
	if (check(p_type)) {
		return advance();
	}
	error_at(peek(), "M1001", p_message);
	return peek();
}

void MochaParser::error_at(const Token &p_token, const String &p_code, const String &p_message) {
	diagnostics.push_back({ p_code, p_message, path, span_from_token(p_token) });
}

void MochaParser::synchronize_line() {
	while (!at_end() && !check(Token::NEWLINE) && !check(Token::DEDENT)) {
		advance();
	}
	match(Token::NEWLINE);
}

void MochaParser::skip_newlines() {
	while (match(Token::NEWLINE)) {
	}
}

bool MochaParser::consume_line_end() {
	if (match(Token::NEWLINE) || at_end() || check(Token::DEDENT)) {
		return true;
	}
	error_at(peek(), "M1002", vformat("Expected the end of the statement, found %s.", token_text(peek())));
	synchronize_line();
	return false;
}

bool MochaParser::consume_identifier(StringName &r_name, const String &p_context) {
	if (!peek().is_identifier()) {
		error_at(peek(), "M1003", vformat("Expected an identifier %s.", p_context));
		return false;
	}
	const Token token = advance();
	r_name = token.literal.get_type() == Variant::STRING_NAME ? StringName(token.literal) : StringName(token.source);
	return true;
}

TypeRef MochaParser::parse_type() {
	if (++type_depth > MAX_TYPE_DEPTH) {
		type_depth--;
		error_at(peek(), "M1011", vformat("Type nesting exceeds the limit of %d.", MAX_TYPE_DEPTH));
		synchronize_line();
		return TypeRef::any();
	}
	TypeDepthScope depth_scope{ type_depth };
	const Token token = peek();
	String name;
	if (match(Token::TK_VOID)) {
		name = "void";
	} else if (peek().is_identifier()) {
		advance();
		name = token_text(token);
	} else {
		error_at(peek(), "M1004", "Expected a type name.");
		return TypeRef::any();
	}

	TypeRef result;
	if (name == "Variant") result = TypeRef::any();
	else if (name == "void") result = TypeRef::make(TypeKind::VOID, name);
	else if (name == "bool") result = TypeRef::make(TypeKind::BOOL, name);
	else if (name == "int") result = TypeRef::make(TypeKind::INT, name);
	else if (name == "float") result = TypeRef::make(TypeKind::FLOAT, name);
	else if (name == "String") result = TypeRef::make(TypeKind::STRING, name);
	else if (name == "Array") result = TypeRef::make(TypeKind::ARRAY, name);
	else if (name == "Dictionary") result = TypeRef::make(TypeKind::DICTIONARY, name);
	else result = TypeRef::make(TypeKind::OBJECT, name);

	// Collection parameters are parsed so their syntax has a stable boundary. The
	// bootstrap TypeRef deliberately erases them until element checking is added.
	if (match(Token::BRACKET_OPEN)) {
		const Token bracket = previous();
		if (name != "Array" && name != "Dictionary") {
			error_at(bracket, "M1012", vformat("Type '%s' does not accept element parameters.", name));
		}
		parse_type();
		if (name == "Dictionary") {
			consume(Token::COMMA, "Expected ',' between Dictionary key and value types.");
			parse_type();
		} else if (match(Token::COMMA)) {
			error_at(previous(), "M1012", "Array accepts exactly one element type.");
			parse_type();
		}
		consume(Token::BRACKET_CLOSE, "Expected ']' after the collection element type.");
	}
	return result;
}

bool MochaParser::parse_member(Program &r_program) {
	const Token start = previous();
	MemberDecl member;
	member.span = span_from_token(start);
	if (!consume_identifier(member.name, "after 'var'")) {
		synchronize_line();
		return false;
	}
	member.type = TypeRef::any();
	if (match(Token::COLON)) {
		member.type = parse_type();
	}
	if (match(Token::EQUAL)) {
		member.initializer = parse_expression();
	}
	consume_line_end();
	r_program.members.push_back(std::move(member));
	return true;
}

bool MochaParser::parse_function(Program &r_program) {
	const Token start = previous();
	FunctionDecl function;
	function.span = span_from_token(start);
	if (!consume_identifier(function.name, "after 'func'")) {
		synchronize_line();
		return false;
	}
	consume(Token::PARENTHESIS_OPEN, "Expected '(' after the function name.");
	if (!check(Token::PARENTHESIS_CLOSE)) {
		do {
			Parameter parameter;
			parameter.span = span_from_token(peek());
			if (!consume_identifier(parameter.name, "for the parameter")) {
				synchronize_line();
				return false;
			}
			parameter.type = TypeRef::any();
			if (match(Token::COLON)) {
				parameter.type = parse_type();
			}
			function.parameters.push_back(parameter);
		} while (match(Token::COMMA));
	}
	consume(Token::PARENTHESIS_CLOSE, "Expected ')' after the parameters.");
	if (match(Token::FORWARD_ARROW)) {
		function.return_type = parse_type();
	}
	consume(Token::COLON, "Expected ':' after the function signature.");
	function.body = parse_suite();
	r_program.functions.push_back(std::move(function));
	return true;
}

std::vector<StmtPtr> MochaParser::parse_suite() {
	std::vector<StmtPtr> body;
	if (++statement_depth > MAX_STATEMENT_DEPTH) {
		statement_depth--;
		error_at(peek(), "M1010", vformat("Statement nesting exceeds the limit of %d.", MAX_STATEMENT_DEPTH));
		synchronize_line();
		return body;
	}
	StatementDepthScope depth_scope{ statement_depth };
	consume(Token::NEWLINE, "Expected a newline before the indented block.");
	consume(Token::INDENT, "Expected an indented block.");
	while (!at_end() && !check(Token::DEDENT)) {
		if (match(Token::NEWLINE)) {
			continue;
		}
		StmtPtr next = parse_statement();
		if (next) {
			body.push_back(std::move(next));
		} else {
			synchronize_line();
		}
	}
	consume(Token::DEDENT, "Expected the end of the indented block.");
	if (body.empty()) {
		error_at(previous(), "M1005", "A block must contain at least one statement; use 'pass' explicitly.");
	}
	return body;
}

StmtPtr MochaParser::parse_variable_statement() {
	const Token start = previous();
	auto result = statement(StatementKind::VARIABLE, span_from_token(start));
	if (!consume_identifier(result->name, "after 'var'")) {
		return {};
	}
	result->declared_type = TypeRef::any();
	if (match(Token::COLON)) {
		result->declared_type = parse_type();
	}
	if (match(Token::EQUAL)) {
		result->expression = parse_expression();
	}
	consume_line_end();
	return result;
}

StmtPtr MochaParser::parse_if_statement() {
	if (++statement_depth > MAX_STATEMENT_DEPTH) {
		statement_depth--;
		error_at(peek(), "M1010", vformat("Conditional nesting exceeds the limit of %d.", MAX_STATEMENT_DEPTH));
		synchronize_line();
		return {};
	}
	StatementDepthScope depth_scope{ statement_depth };
	const Token start = previous();
	auto result = statement(StatementKind::IF, span_from_token(start));
	result->expression = parse_expression();
	consume(Token::COLON, "Expected ':' after the if condition.");
	result->body = parse_suite();
	if (match(Token::ELSE)) {
		consume(Token::COLON, "Expected ':' after 'else'.");
		result->else_body = parse_suite();
	} else if (match(Token::ELIF)) {
		StmtPtr nested = parse_if_statement();
		if (nested) result->else_body.push_back(nested);
	}
	return result;
}

StmtPtr MochaParser::parse_while_statement() {
	const Token start = previous();
	auto result = statement(StatementKind::WHILE, span_from_token(start));
	result->expression = parse_expression();
	consume(Token::COLON, "Expected ':' after the while condition.");
	result->body = parse_suite();
	return result;
}

StmtPtr MochaParser::parse_return_statement() {
	const Token start = previous();
	auto result = statement(StatementKind::RETURN, span_from_token(start));
	if (!check(Token::NEWLINE) && !check(Token::DEDENT) && !at_end()) {
		result->expression = parse_expression();
	}
	consume_line_end();
	return result;
}

StmtPtr MochaParser::parse_statement() {
	if (match(Token::VAR)) return parse_variable_statement();
	if (match(Token::IF)) return parse_if_statement();
	if (match(Token::WHILE)) return parse_while_statement();
	if (match(Token::RETURN)) return parse_return_statement();
	if (match(Token::PASS)) {
		auto result = statement(StatementKind::PASS, span_from_token(previous()));
		consume_line_end();
		return result;
	}
	if (match(Token::FOR) || match(Token::MATCH) || match(Token::AWAIT) || match(Token::YIELD) ||
			match(Token::BREAK) || match(Token::CONTINUE) || match(Token::ASSERT) || match(Token::BREAKPOINT)) {
		error_at(previous(), "M1090", vformat("'%s' is recognized but not supported by this MochaScript bootstrap.", token_text(previous())));
		return {};
	}

	ExprPtr first = parse_expression();
	if (!first) return {};
	if (is_assignment(peek().type)) {
		const Token operation = advance();
		auto result = statement(StatementKind::ASSIGNMENT, first->span);
		result->target = first;
		result->operation = operation.type;
		result->expression = parse_expression();
		consume_line_end();
		return result;
	}
	auto result = statement(StatementKind::EXPRESSION, first->span);
	result->expression = first;
	consume_line_end();
	return result;
}

int MochaParser::binary_precedence(Token::Type p_type) const {
	switch (p_type) {
		case Token::OR:
		case Token::PIPE_PIPE: return 1;
		case Token::AND:
		case Token::AMPERSAND_AMPERSAND: return 2;
		case Token::EQUAL_EQUAL:
		case Token::BANG_EQUAL: return 3;
		case Token::LESS:
		case Token::LESS_EQUAL:
		case Token::GREATER:
		case Token::GREATER_EQUAL:
		case Token::TK_IN:
		case Token::IS: return 4;
		case Token::PIPE: return 5;
		case Token::CARET: return 6;
		case Token::AMPERSAND: return 7;
		case Token::LESS_LESS:
		case Token::GREATER_GREATER: return 8;
		case Token::PLUS:
		case Token::MINUS: return 9;
		case Token::STAR:
		case Token::SLASH:
		case Token::PERCENT: return 10;
		case Token::STAR_STAR: return 11;
		default: return 0;
	}
}

bool MochaParser::is_assignment(Token::Type p_type) const {
	return p_type == Token::EQUAL || p_type == Token::PLUS_EQUAL || p_type == Token::MINUS_EQUAL ||
			p_type == Token::STAR_EQUAL || p_type == Token::SLASH_EQUAL || p_type == Token::PERCENT_EQUAL;
}

ExprPtr MochaParser::parse_expression(int p_min_precedence) {
	if (++expression_depth > MAX_EXPRESSION_DEPTH) {
		expression_depth--;
		error_at(peek(), "M1009", vformat("Expression nesting exceeds the limit of %d.", MAX_EXPRESSION_DEPTH));
		if (!at_end()) advance();
		return {};
	}
	ExpressionDepthScope depth_scope{ expression_depth };
	ExprPtr left = parse_unary();
	if (!left) return {};
	while (true) {
		const int precedence = binary_precedence(peek().type);
		if (precedence < p_min_precedence) break;
		const Token operation = advance();
		const int next_precedence = precedence + 1;
		ExprPtr right = parse_expression(next_precedence);
		if (!right) return left;
		const int combined_depth = MAX(left->tree_depth, right->tree_depth) + 1;
		if (combined_depth > MAX_EXPRESSION_DEPTH) {
			error_at(operation, "M1009", vformat("Expression tree exceeds the limit of %d.", MAX_EXPRESSION_DEPTH));
			synchronize_line();
			return left;
		}
		auto combined = expression(ExprKind::BINARY, merge_span(left->span, right->span));
		combined->tree_depth = combined_depth;
		combined->left = left;
		combined->right = right;
		combined->operation = operation.type;
		left = combined;
	}
	return left;
}

ExprPtr MochaParser::parse_unary() {
	if (++expression_depth > MAX_EXPRESSION_DEPTH) {
		expression_depth--;
		error_at(peek(), "M1009", vformat("Expression nesting exceeds the limit of %d.", MAX_EXPRESSION_DEPTH));
		if (!at_end()) advance();
		return {};
	}
	ExpressionDepthScope depth_scope{ expression_depth };
	if (match(Token::NOT)) {
		const Token operation = previous();
		// Word-form `not` binds below equality/comparison/containment but above
		// `and`; symbolic `!` remains a conventional high-precedence unary.
		ExprPtr operand = parse_expression(3);
		if (!operand) return {};
		if (operand->tree_depth >= MAX_EXPRESSION_DEPTH) {
			error_at(operation, "M1009", vformat("Expression tree exceeds the limit of %d.", MAX_EXPRESSION_DEPTH));
			return operand;
		}
		auto result = expression(ExprKind::UNARY, merge_span(span_from_token(operation), operand->span));
		result->tree_depth = operand->tree_depth + 1;
		result->operation = operation.type;
		result->right = operand;
		return result;
	}
	if (match(Token::BANG) || match(Token::MINUS) || match(Token::PLUS) || match(Token::TILDE)) {
		const Token operation = previous();
		// Numeric/bitwise unary operators bind less tightly than exponentiation.
		ExprPtr operand = operation.type == Token::BANG ? parse_unary() : parse_expression(11);
		if (!operand) return {};
		if (operand->tree_depth >= MAX_EXPRESSION_DEPTH) {
			error_at(operation, "M1009", vformat("Expression tree exceeds the limit of %d.", MAX_EXPRESSION_DEPTH));
			return operand;
		}
		auto result = expression(ExprKind::UNARY, merge_span(span_from_token(operation), operand->span));
		result->tree_depth = operand->tree_depth + 1;
		result->operation = operation.type;
		result->right = operand;
		return result;
	}
	return parse_postfix();
}

ExprPtr MochaParser::parse_postfix() {
	ExprPtr result = parse_primary();
	if (!result) return {};
	while (true) {
		if (match(Token::PARENTHESIS_OPEN)) {
			auto call = expression(ExprKind::CALL, result->span);
			call->left = result;
			if (!check(Token::PARENTHESIS_CLOSE)) {
				do {
					call->arguments.push_back(parse_expression());
				} while (match(Token::COMMA));
			}
			const Token end = consume(Token::PARENTHESIS_CLOSE, "Expected ')' after the arguments.");
			int call_depth = result->tree_depth + 1;
			for (const ExprPtr &argument : call->arguments) call_depth = MAX(call_depth, argument ? argument->tree_depth + 1 : 1);
			if (call_depth > MAX_EXPRESSION_DEPTH) {
				error_at(end, "M1009", vformat("Expression tree exceeds the limit of %d.", MAX_EXPRESSION_DEPTH));
				return result;
			}
			call->tree_depth = call_depth;
			call->span = merge_span(result->span, span_from_token(end));
			result = call;
		} else if (match(Token::PERIOD)) {
			StringName name;
			if (!consume_identifier(name, "after '.'")) return result;
			auto member = expression(ExprKind::MEMBER, merge_span(result->span, span_from_token(previous())));
			if (result->tree_depth >= MAX_EXPRESSION_DEPTH) {
				error_at(previous(), "M1009", vformat("Expression tree exceeds the limit of %d.", MAX_EXPRESSION_DEPTH));
				return result;
			}
			member->tree_depth = result->tree_depth + 1;
			member->left = result;
			member->name = name;
			result = member;
		} else if (match(Token::BRACKET_OPEN)) {
			ExprPtr index = parse_expression();
			const Token end = consume(Token::BRACKET_CLOSE, "Expected ']' after the index.");
			auto indexed = expression(ExprKind::INDEX, merge_span(result->span, span_from_token(end)));
			const int index_depth = MAX(result->tree_depth, index ? index->tree_depth : 1) + 1;
			if (index_depth > MAX_EXPRESSION_DEPTH) {
				error_at(end, "M1009", vformat("Expression tree exceeds the limit of %d.", MAX_EXPRESSION_DEPTH));
				return result;
			}
			indexed->tree_depth = index_depth;
			indexed->left = result;
			indexed->right = index;
			result = indexed;
		} else {
			break;
		}
	}
	return result;
}

ExprPtr MochaParser::parse_primary() {
	const Token token = peek();
	if (match(Token::LITERAL)) {
		auto result = expression(ExprKind::LITERAL, span_from_token(token));
		result->literal = token.literal;
		return result;
	}
	if (match(Token::CONST_PI) || match(Token::CONST_TAU) || match(Token::CONST_INF) || match(Token::CONST_NAN)) {
		auto result = expression(ExprKind::LITERAL, span_from_token(token));
		if (token.type == Token::CONST_PI) result->literal = 3.14159265358979323846;
		else if (token.type == Token::CONST_TAU) result->literal = 6.28318530717958647692;
		else if (token.type == Token::CONST_INF) result->literal = std::numeric_limits<double>::infinity();
		else result->literal = std::numeric_limits<double>::quiet_NaN();
		return result;
	}
	if (match(Token::SELF)) return expression(ExprKind::SELF, span_from_token(token));
	if (peek().is_identifier()) {
		advance();
		auto result = expression(ExprKind::IDENTIFIER, span_from_token(token));
		result->name = token.literal.get_type() == Variant::STRING_NAME ? StringName(token.literal) : StringName(token.source);
		return result;
	}
	if (match(Token::PARENTHESIS_OPEN)) {
		ExprPtr result = parse_expression();
		consume(Token::PARENTHESIS_CLOSE, "Expected ')' after the expression.");
		return result;
	}
	if (match(Token::BRACKET_OPEN)) {
		auto result = expression(ExprKind::ARRAY, span_from_token(token));
		int literal_depth = 1;
		if (!check(Token::BRACKET_CLOSE)) {
			do {
				ExprPtr item = parse_expression();
				if (item) literal_depth = MAX(literal_depth, item->tree_depth + 1);
				result->arguments.push_back(std::move(item));
			} while (match(Token::COMMA));
		}
		const Token end = consume(Token::BRACKET_CLOSE, "Expected ']' after the array elements.");
		if (literal_depth > MAX_EXPRESSION_DEPTH) {
			error_at(end, "M1009", vformat("Expression tree exceeds the limit of %d.", MAX_EXPRESSION_DEPTH));
			result->arguments.clear();
			return result;
		}
		result->tree_depth = literal_depth;
		result->span = merge_span(result->span, span_from_token(end));
		return result;
	}
	if (match(Token::BRACE_OPEN)) {
		auto result = expression(ExprKind::DICTIONARY, span_from_token(token));
		int literal_depth = 1;
		if (!check(Token::BRACE_CLOSE)) {
			do {
				ExprPtr key = parse_expression();
				consume(Token::COLON, "Expected ':' between the dictionary key and value.");
				ExprPtr value = parse_expression();
				if (key) literal_depth = MAX(literal_depth, key->tree_depth + 1);
				if (value) literal_depth = MAX(literal_depth, value->tree_depth + 1);
				result->entries.push_back({ key, value });
			} while (match(Token::COMMA));
		}
		const Token end = consume(Token::BRACE_CLOSE, "Expected '}' after the dictionary entries.");
		if (literal_depth > MAX_EXPRESSION_DEPTH) {
			error_at(end, "M1009", vformat("Expression tree exceeds the limit of %d.", MAX_EXPRESSION_DEPTH));
			result->entries.clear();
			return result;
		}
		result->tree_depth = literal_depth;
		result->span = merge_span(result->span, span_from_token(end));
		return result;
	}

	error_at(token, "M1006", vformat("Expected an expression, found %s.", token_text(token)));
	if (!at_end()) advance();
	return {};
}

Error MochaParser::parse(const String &p_source, const String &p_path, Program &r_program) {
	path = p_path.is_empty() ? String("<memory>") : p_path;
	tokens.clear();
	diagnostics.clear();
	current = 0;
	expression_depth = 0;
	statement_depth = 0;
	type_depth = 0;
	r_program = Program();

	MochaTokenizerText tokenizer;
	tokenizer.set_source_code(p_source);
	const int64_t limit = p_source.length() * 4 + 1024;
	for (int64_t count = 0; count < limit; ++count) {
		Token token = tokenizer.scan();
		if (token.type == Token::ERROR) {
			error_at(token, "M1000", String(token.literal));
		} else {
			tokens.push_back(token);
		}
		if (token.type == Token::TK_EOF) break;
	}
	if (tokens.empty() || tokens.back().type != Token::TK_EOF) {
		Token eof(Token::TK_EOF);
		tokens.push_back(eof);
		diagnostics.push_back({ "M1007", "Tokenizer did not reach end of source.", path, {} });
	}

	skip_newlines();
	if (match(Token::EXTENDS)) {
		StringName base;
		if (consume_identifier(base, "after 'extends'")) r_program.base_type = base;
		consume_line_end();
	}

	while (!at_end()) {
		skip_newlines();
		if (at_end()) break;
		if (check(Token::INDENT) || check(Token::DEDENT)) {
			error_at(peek(), "M1008", vformat("Unexpected top-level indentation token %s.", token_text(peek())));
			advance();
		} else if (match(Token::VAR)) {
			parse_member(r_program);
		} else if (match(Token::FUNC)) {
			parse_function(r_program);
		} else {
			error_at(peek(), "M1008", vformat("Unsupported top-level declaration beginning with %s.", token_text(peek())));
			synchronize_line();
		}
	}
	return diagnostics.empty() ? OK : ERR_PARSE_ERROR;
}

} // namespace mocha_script
