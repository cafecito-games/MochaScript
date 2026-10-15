/**************************************************************************/
/*  mocha_bytecode.h                                                      */
/**************************************************************************/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "language/mocha_ast.h"

namespace mocha_script {

enum class OpCode {
	PUSH_CONSTANT,
	PUSH_SELF,
	LOAD_LOCAL,
	STORE_LOCAL,
	LOAD_MEMBER,
	STORE_MEMBER,
	MAKE_ARRAY,
	MAKE_DICTIONARY,
	GET_INDEX,
	GET_NAMED,
	UNARY,
	BINARY,
	CALL_SELF,
	CALL_METHOD,
	POP,
	JUMP,
	JUMP_IF_FALSE,
	RETURN,
};

struct Instruction {
	OpCode opcode = OpCode::PUSH_CONSTANT;
	Variant operand;
	StringName name;
	int64_t argument = 0;
	MochaTokenizer::Token::Type operation = MochaTokenizer::Token::EMPTY;
	SourceSpan span;
};

struct CompiledFunction {
	StringName name;
	std::vector<StringName> parameter_names;
	std::vector<TypeRef> parameter_types;
	TypeRef return_type;
	int64_t local_count = 0;
	std::vector<Instruction> code;
};

struct CompiledMember {
	StringName name;
	TypeRef type;
	SourceSpan span;
};

struct CompiledModule {
	StringName base_type;
	std::vector<CompiledMember> members;
	CompiledFunction initializer;
	std::vector<CompiledFunction> functions;

	const CompiledFunction *find_function(const StringName &p_name) const {
		for (const CompiledFunction &function : functions) {
			if (function.name == p_name) return &function;
		}
		return nullptr;
	}
	int64_t find_member(const StringName &p_name) const {
		for (size_t index = 0; index < members.size(); ++index) {
			if (members[index].name == p_name) return int64_t(index);
		}
		return -1;
	}
};

} // namespace mocha_script
