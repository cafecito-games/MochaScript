/**************************************************************************/
/*  mocha_compiler.cpp                                                    */
/**************************************************************************/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "language/mocha_compiler.h"

namespace mocha_script {

namespace {

Variant default_value(const TypeRef &p_type) {
	switch (p_type.kind) {
		case TypeKind::BOOL: return false;
		case TypeKind::INT: return int64_t(0);
		case TypeKind::FLOAT: return 0.0;
		case TypeKind::STRING: return String();
		default: return Variant();
	}
}

HashMap<StringName, int64_t> copy_locals(const HashMap<StringName, int64_t> &p_source) {
	HashMap<StringName, int64_t> result;
	for (const KeyValue<StringName, int64_t> &entry : p_source) result.insert(entry.key, entry.value);
	return result;
}

} // namespace

void MochaCompiler::report(const SourceSpan &p_span, const String &p_code, const String &p_message) {
	diagnostics.push_back({ p_code, p_message, path, p_span });
}

int64_t MochaCompiler::emit(OpCode p_opcode, const SourceSpan &p_span, int64_t p_argument) {
	Instruction instruction;
	instruction.opcode = p_opcode;
	instruction.span = p_span;
	instruction.argument = p_argument;
	function->code.push_back(instruction);
	return int64_t(function->code.size() - 1);
}

void MochaCompiler::emit_default(const TypeRef &p_type, const SourceSpan &p_span) {
	if (p_type.kind == TypeKind::ARRAY) {
		emit(OpCode::MAKE_ARRAY, p_span, 0);
	} else if (p_type.kind == TypeKind::DICTIONARY) {
		emit(OpCode::MAKE_DICTIONARY, p_span, 0);
	} else {
		const int64_t value = emit(OpCode::PUSH_CONSTANT, p_span);
		function->code[size_t(value)].operand = default_value(p_type);
	}
}

MochaTokenizer::Token::Type MochaCompiler::compound_operation(MochaTokenizer::Token::Type p_operation) const {
	using Token = MochaTokenizer::Token;
	switch (p_operation) {
		case Token::PLUS_EQUAL: return Token::PLUS;
		case Token::MINUS_EQUAL: return Token::MINUS;
		case Token::STAR_EQUAL: return Token::STAR;
		case Token::SLASH_EQUAL: return Token::SLASH;
		case Token::PERCENT_EQUAL: return Token::PERCENT;
		default: return Token::EMPTY;
	}
}

void MochaCompiler::compile_expression(const ExprPtr &p_expression) {
	if (!p_expression) {
		emit(OpCode::PUSH_CONSTANT, {});
		return;
	}
	switch (p_expression->kind) {
		case ExprKind::LITERAL: {
			const int64_t index = emit(OpCode::PUSH_CONSTANT, p_expression->span);
			function->code[index].operand = p_expression->literal;
		} break;
		case ExprKind::SELF:
			emit(OpCode::PUSH_SELF, p_expression->span);
			break;
		case ExprKind::IDENTIFIER:
			if (const int64_t *local = locals.getptr(p_expression->name)) {
				emit(OpCode::LOAD_LOCAL, p_expression->span, *local);
			} else if (const int64_t *member = members.getptr(p_expression->name)) {
				emit(OpCode::LOAD_MEMBER, p_expression->span, *member);
			} else {
				report(p_expression->span, "M3001", vformat("Compiler cannot resolve identifier '%s'.", String(p_expression->name)));
				emit(OpCode::PUSH_CONSTANT, p_expression->span);
			}
			break;
		case ExprKind::ARRAY:
			for (const ExprPtr &item : p_expression->arguments) compile_expression(item);
			emit(OpCode::MAKE_ARRAY, p_expression->span, int64_t(p_expression->arguments.size()));
			break;
		case ExprKind::DICTIONARY:
			for (const auto &entry : p_expression->entries) {
				compile_expression(entry.first);
				compile_expression(entry.second);
			}
			emit(OpCode::MAKE_DICTIONARY, p_expression->span, int64_t(p_expression->entries.size()));
			break;
		case ExprKind::UNARY: {
			compile_expression(p_expression->right);
			const int64_t index = emit(OpCode::UNARY, p_expression->span);
			function->code[index].operation = p_expression->operation;
		} break;
		case ExprKind::BINARY: {
			const bool logical_and = p_expression->operation == MochaTokenizer::Token::AND ||
					p_expression->operation == MochaTokenizer::Token::AMPERSAND_AMPERSAND;
			const bool logical_or = p_expression->operation == MochaTokenizer::Token::OR ||
					p_expression->operation == MochaTokenizer::Token::PIPE_PIPE;
			if (logical_and || logical_or) {
				compile_expression(p_expression->left);
				const int64_t branch = emit(OpCode::JUMP_IF_FALSE, p_expression->left->span);
				if (logical_or) {
					emit(OpCode::PUSH_CONSTANT, p_expression->span);
					function->code.back().operand = true;
					const int64_t jump_end = emit(OpCode::JUMP, p_expression->span);
					function->code[branch].argument = int64_t(function->code.size());
					compile_expression(p_expression->right);
					emit(OpCode::BOOLEANIZE, p_expression->right->span);
					function->code[jump_end].argument = int64_t(function->code.size());
				} else {
					compile_expression(p_expression->right);
					emit(OpCode::BOOLEANIZE, p_expression->right->span);
					const int64_t jump_end = emit(OpCode::JUMP, p_expression->span);
					function->code[branch].argument = int64_t(function->code.size());
					emit(OpCode::PUSH_CONSTANT, p_expression->span);
					function->code.back().operand = false;
					function->code[jump_end].argument = int64_t(function->code.size());
				}
				break;
			}
			if (p_expression->operation == MochaTokenizer::Token::IS) {
				report(p_expression->span, "M3090", "The 'is' operator is recognized but not executable in this bootstrap.");
				emit(OpCode::PUSH_CONSTANT, p_expression->span);
				break;
			}
			compile_expression(p_expression->left);
			compile_expression(p_expression->right);
			const int64_t index = emit(OpCode::BINARY, p_expression->span);
			function->code[index].operation = p_expression->operation;
		} break;
		case ExprKind::MEMBER: {
			compile_expression(p_expression->left);
			const int64_t index = emit(OpCode::GET_NAMED, p_expression->span);
			function->code[index].name = p_expression->name;
		} break;
		case ExprKind::INDEX:
			compile_expression(p_expression->left);
			compile_expression(p_expression->right);
			emit(OpCode::GET_INDEX, p_expression->span);
			break;
		case ExprKind::CALL: {
			if (p_expression->left && p_expression->left->kind == ExprKind::IDENTIFIER) {
				for (const ExprPtr &argument : p_expression->arguments) compile_expression(argument);
				const int64_t index = emit(OpCode::CALL_SELF, p_expression->span, int64_t(p_expression->arguments.size()));
				function->code[index].name = p_expression->left->name;
			} else if (p_expression->left && p_expression->left->kind == ExprKind::MEMBER) {
				compile_expression(p_expression->left->left);
				for (const ExprPtr &argument : p_expression->arguments) compile_expression(argument);
				const int64_t index = emit(OpCode::CALL_METHOD, p_expression->span, int64_t(p_expression->arguments.size()));
				function->code[index].name = p_expression->left->name;
			} else {
				report(p_expression->span, "M3091", "Only named functions and object methods are callable in this bootstrap.");
				emit(OpCode::PUSH_CONSTANT, p_expression->span);
			}
		} break;
	}
}

void MochaCompiler::compile_statement(const StmtPtr &p_statement) {
	if (!p_statement) return;
	switch (p_statement->kind) {
		case StatementKind::VARIABLE: {
			const int64_t local = function->local_count++;
			function->local_types.push_back(p_statement->declared_type);
			if (p_statement->expression) {
				compile_expression(p_statement->expression);
			} else {
				emit_default(p_statement->declared_type, p_statement->span);
			}
			emit(OpCode::STORE_LOCAL, p_statement->span, local);
			locals.insert(p_statement->name, local);
		} break;
		case StatementKind::EXPRESSION:
			compile_expression(p_statement->expression);
			emit(OpCode::POP, p_statement->span);
			break;
		case StatementKind::ASSIGNMENT: {
			if (!p_statement->target || p_statement->target->kind != ExprKind::IDENTIFIER) {
				report(p_statement->span, "M3092", "Only local and member assignment is compiled in this bootstrap.");
				break;
			}
			const int64_t *local = locals.getptr(p_statement->target->name);
			const int64_t *member = members.getptr(p_statement->target->name);
			if (local == nullptr && member == nullptr) {
				report(p_statement->span, "M3002", vformat("Compiler cannot resolve assignment target '%s'.", String(p_statement->target->name)));
				break;
			}
			if (p_statement->operation != MochaTokenizer::Token::EQUAL) {
				emit(local ? OpCode::LOAD_LOCAL : OpCode::LOAD_MEMBER, p_statement->target->span, local ? *local : *member);
			}
			compile_expression(p_statement->expression);
			if (p_statement->operation != MochaTokenizer::Token::EQUAL) {
				const int64_t operation = emit(OpCode::BINARY, p_statement->span);
				function->code[operation].operation = compound_operation(p_statement->operation);
			}
			emit(local ? OpCode::STORE_LOCAL : OpCode::STORE_MEMBER, p_statement->span, local ? *local : *member);
		} break;
		case StatementKind::RETURN:
			compile_expression(p_statement->expression);
			emit(OpCode::RETURN, p_statement->span);
			break;
		case StatementKind::IF: {
			compile_expression(p_statement->expression);
			const int64_t jump_false = emit(OpCode::JUMP_IF_FALSE, p_statement->span);
			const HashMap<StringName, int64_t> outer_locals = copy_locals(locals);
			compile_block(p_statement->body);
			locals = copy_locals(outer_locals);
			const int64_t jump_end = emit(OpCode::JUMP, p_statement->span);
			function->code[jump_false].argument = int64_t(function->code.size());
			compile_block(p_statement->else_body);
			locals = copy_locals(outer_locals);
			function->code[jump_end].argument = int64_t(function->code.size());
		} break;
		case StatementKind::WHILE: {
			const int64_t loop_start = int64_t(function->code.size());
			compile_expression(p_statement->expression);
			const int64_t jump_end = emit(OpCode::JUMP_IF_FALSE, p_statement->span);
			const HashMap<StringName, int64_t> outer_locals = copy_locals(locals);
			compile_block(p_statement->body);
			locals = copy_locals(outer_locals);
			emit(OpCode::JUMP, p_statement->span, loop_start);
			function->code[jump_end].argument = int64_t(function->code.size());
		} break;
		case StatementKind::PASS:
			break;
	}
}

void MochaCompiler::compile_block(const std::vector<StmtPtr> &p_block) {
	for (const StmtPtr &statement : p_block) compile_statement(statement);
}

void MochaCompiler::compile_function(const FunctionDecl &p_function, CompiledFunction &r_function) {
	function = &r_function;
	locals.clear();
	r_function.name = p_function.name;
	r_function.span = p_function.span;
	r_function.return_type = p_function.return_type;
	for (const Parameter &parameter : p_function.parameters) {
		const int64_t local = r_function.local_count++;
		locals.insert(parameter.name, local);
		r_function.parameter_names.push_back(parameter.name);
		r_function.parameter_types.push_back(parameter.type);
		r_function.local_types.push_back(parameter.type);
	}
	compile_block(p_function.body);
	// Always append a checked implicit return. Branches emitted before this point
	// may jump to the old end even when another branch ended in RETURN.
	emit(OpCode::PUSH_CONSTANT, p_function.span);
	emit(OpCode::RETURN, p_function.span);
}

Error MochaCompiler::compile(const Program &p_program, const String &p_path, std::shared_ptr<CompiledModule> &r_module) {
	path = p_path.is_empty() ? String("<memory>") : p_path;
	diagnostics.clear();
	members.clear();
	module = std::make_shared<CompiledModule>();
	module->base_type = p_program.base_type;
	for (const MemberDecl &member : p_program.members) {
		members.insert(member.name, int64_t(module->members.size()));
		module->members.push_back({ member.name, member.type, member.span });
	}

	function = &module->initializer;
	function->name = "@init";
	function->span = p_program.members.empty() ? SourceSpan() : p_program.members.front().span;
	function->return_type = TypeRef::make(TypeKind::VOID, "void");
	locals.clear();
	for (size_t index = 0; index < p_program.members.size(); ++index) {
		if (p_program.members[index].initializer) {
			compile_expression(p_program.members[index].initializer);
		} else {
			emit_default(p_program.members[index].type, p_program.members[index].span);
		}
		emit(OpCode::STORE_MEMBER, p_program.members[index].span, int64_t(index));
	}
	emit(OpCode::PUSH_CONSTANT, {});
	emit(OpCode::RETURN, {});

	for (const FunctionDecl &declaration : p_program.functions) {
		module->functions.emplace_back();
		compile_function(declaration, module->functions.back());
	}
	if (!diagnostics.empty()) {
		r_module.reset();
		return ERR_COMPILATION_FAILED;
	}
	r_module = module;
	return OK;
}

} // namespace mocha_script
