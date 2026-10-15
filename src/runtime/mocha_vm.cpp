/**************************************************************************/
/*  mocha_vm.cpp                                                          */
/**************************************************************************/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "runtime/mocha_vm.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <cstdint>
#include <limits>

namespace mocha_script {

namespace {

thread_local int vm_entry_count = 0;
thread_local int vm_frame_depth = 0;
thread_local int64_t vm_operations_remaining = 0;
struct NestedFailureCapture;
thread_local std::vector<NestedFailureCapture *> vm_failure_captures;

struct VMEntry {
	bool root = false;
	VMEntry() {
		root = vm_entry_count++ == 0;
		if (root) {
			vm_frame_depth = 0;
			vm_operations_remaining = 1000000;
		}
	}
	~VMEntry() { vm_entry_count--; }
};

struct VMFrame {
	bool valid = false;
	VMFrame() { valid = ++vm_frame_depth <= 128; }
	~VMFrame() { vm_frame_depth--; }
};

void fail(RuntimeFailure &r_failure, const SourceSpan &p_span, const String &p_message) {
	r_failure.code = "M4001";
	r_failure.message = p_message;
	r_failure.span = p_span;
}

Variant runtime_default(const TypeRef &p_type) {
	switch (p_type.kind) {
		case TypeKind::BOOL: return false;
		case TypeKind::INT: return int64_t(0);
		case TypeKind::FLOAT: return 0.0;
		case TypeKind::STRING: return String();
		case TypeKind::ARRAY: return Array();
		case TypeKind::DICTIONARY: return Dictionary();
		default: return Variant();
	}
}

struct NestedFailureCapture {
	RuntimeFailure failure;
	uint64_t owner_id = 0;
	StringName method;
	NestedFailureCapture(uint64_t p_owner_id, const StringName &p_method) : owner_id(p_owner_id), method(p_method) {
		vm_failure_captures.push_back(this);
	}
	~NestedFailureCapture() { vm_failure_captures.pop_back(); }
};

bool take_nested_failure(const RuntimeFailure &p_captured, RuntimeFailure &r_failure) {
	if (p_captured.message.is_empty()) return false;
	r_failure = p_captured;
	return true;
}

bool requires_integer_operands(MochaTokenizer::Token::Type p_operation) {
	using Token = MochaTokenizer::Token;
	switch (p_operation) {
		case Token::PERCENT:
		case Token::LESS_LESS:
		case Token::GREATER_GREATER:
		case Token::AMPERSAND:
		case Token::PIPE:
		case Token::CARET:
			return true;
		default:
			return false;
	}
}

int64_t integer_from_bits(uint64_t p_bits) {
	if (p_bits <= uint64_t(std::numeric_limits<int64_t>::max())) return int64_t(p_bits);
	return -1 - int64_t(~p_bits);
}

bool evaluate_integer_binary(MochaTokenizer::Token::Type p_operation, int64_t p_left,
		int64_t p_right, int64_t &r_result, String &r_error) {
	using Token = MochaTokenizer::Token;
	switch (p_operation) {
		case Token::PERCENT:
			if (p_right == 0) {
				r_error = "Modulo by zero is not allowed.";
				return false;
			}
			// Avoid the one signed remainder operation that is undefined in C++.
			r_result = p_left == std::numeric_limits<int64_t>::min() && p_right == -1
					? 0
					: p_left % p_right;
			return true;
		case Token::LESS_LESS:
		case Token::GREATER_GREATER: {
			if (p_right < 0 || p_right >= 64) {
				r_error = "Shift count must be between 0 and 63.";
				return false;
			}
			const uint64_t bits = uint64_t(p_left);
			const uint32_t count = uint32_t(p_right);
			uint64_t shifted = 0;
			if (p_operation == Token::LESS_LESS) {
				shifted = bits << count;
			} else {
				shifted = bits >> count;
				if (p_left < 0 && count > 0) shifted |= std::numeric_limits<uint64_t>::max() << (64 - count);
			}
			r_result = integer_from_bits(shifted);
			return true;
		}
		case Token::AMPERSAND:
			r_result = integer_from_bits(uint64_t(p_left) & uint64_t(p_right));
			return true;
		case Token::PIPE:
			r_result = integer_from_bits(uint64_t(p_left) | uint64_t(p_right));
			return true;
		case Token::CARET:
			r_result = integer_from_bits(uint64_t(p_left) ^ uint64_t(p_right));
			return true;
		default:
			return false;
	}
}

} // namespace

bool MochaVM::convert_value(const Variant &p_value, const TypeRef &p_type, Variant &r_converted) {
	if (p_type.kind == TypeKind::ANY) {
		r_converted = p_value;
		return true;
	}
	if (p_value.get_type() == Variant::NIL) {
		if (p_type.kind == TypeKind::NIL || p_type.kind == TypeKind::OBJECT || p_type.kind == TypeKind::VOID) {
			r_converted = p_value;
			return true;
		}
		return false;
	}
	switch (p_type.kind) {
		case TypeKind::BOOL:
			if (p_value.get_type() != Variant::BOOL) return false;
			break;
		case TypeKind::INT:
			if (p_value.get_type() != Variant::INT) return false;
			break;
		case TypeKind::FLOAT:
			if (p_value.get_type() == Variant::INT) {
				r_converted = double(int64_t(p_value));
				return true;
			}
			if (p_value.get_type() != Variant::FLOAT) return false;
			break;
		case TypeKind::STRING:
			if (p_value.get_type() == Variant::STRING_NAME) {
				r_converted = String(StringName(p_value));
				return true;
			}
			if (p_value.get_type() != Variant::STRING) return false;
			break;
		case TypeKind::ARRAY:
			if (p_value.get_type() != Variant::ARRAY) return false;
			break;
		case TypeKind::DICTIONARY:
			if (p_value.get_type() != Variant::DICTIONARY) return false;
			break;
		case TypeKind::OBJECT: {
			if (p_value.get_type() != Variant::OBJECT) return false;
			Object *object = p_value.get_validated_object();
			if (object == nullptr && ObjectID(p_value).is_valid()) return false;
			if (object != nullptr && !p_type.name.is_empty() && p_type.name != StringName("Object") &&
					!ClassDB::is_parent_class(object->get_class(), p_type.name)) return false;
		} break;
		case TypeKind::VOID:
			if (p_value.get_type() != Variant::NIL) return false;
			break;
		case TypeKind::NIL:
			return false;
		case TypeKind::ANY:
			break;
	}
	r_converted = p_value;
	return true;
}

bool MochaVM::variant_operator(MochaTokenizer::Token::Type p_operation, Variant::Operator &r_operator) {
	using Token = MochaTokenizer::Token;
	switch (p_operation) {
		case Token::EQUAL_EQUAL: r_operator = Variant::OP_EQUAL; return true;
		case Token::BANG_EQUAL: r_operator = Variant::OP_NOT_EQUAL; return true;
		case Token::LESS: r_operator = Variant::OP_LESS; return true;
		case Token::LESS_EQUAL: r_operator = Variant::OP_LESS_EQUAL; return true;
		case Token::GREATER: r_operator = Variant::OP_GREATER; return true;
		case Token::GREATER_EQUAL: r_operator = Variant::OP_GREATER_EQUAL; return true;
		case Token::PLUS: r_operator = Variant::OP_ADD; return true;
		case Token::MINUS: r_operator = Variant::OP_SUBTRACT; return true;
		case Token::STAR: r_operator = Variant::OP_MULTIPLY; return true;
		case Token::SLASH: r_operator = Variant::OP_DIVIDE; return true;
		case Token::PERCENT: r_operator = Variant::OP_MODULE; return true;
		case Token::STAR_STAR: r_operator = Variant::OP_POWER; return true;
		case Token::LESS_LESS: r_operator = Variant::OP_SHIFT_LEFT; return true;
		case Token::GREATER_GREATER: r_operator = Variant::OP_SHIFT_RIGHT; return true;
		case Token::AMPERSAND: r_operator = Variant::OP_BIT_AND; return true;
		case Token::PIPE: r_operator = Variant::OP_BIT_OR; return true;
		case Token::CARET: r_operator = Variant::OP_BIT_XOR; return true;
		case Token::AND:
		case Token::AMPERSAND_AMPERSAND: r_operator = Variant::OP_AND; return true;
		case Token::OR:
		case Token::PIPE_PIPE: r_operator = Variant::OP_OR; return true;
		case Token::TK_IN: r_operator = Variant::OP_IN; return true;
		case Token::NOT:
		case Token::BANG: r_operator = Variant::OP_NOT; return true;
		case Token::TILDE: r_operator = Variant::OP_BIT_NEGATE; return true;
		default: return false;
	}
}

Variant MochaVM::execute(const CompiledModule &p_module, const CompiledFunction &p_function,
		Object *p_owner, Vector<Variant> &r_members, const Variant **p_arguments,
		int p_argument_count, GDExtensionCallError &r_call_error,
		RuntimeFailure &r_failure, const SourceSpan &p_entry_span) {
	r_call_error = {};
	r_call_error.error = GDEXTENSION_CALL_OK;
	const uint64_t owner_id = p_owner != nullptr ? p_owner->get_instance_id() : 0;
	auto owner_is_alive = [&]() {
		return owner_id == 0 || ObjectDB::get_instance(owner_id) == p_owner;
	};
	VMFrame frame;
	if (!frame.valid) {
		r_call_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		fail(r_failure, p_entry_span, "MochaScript call depth exceeded 128 frames.");
		return {};
	}
	if (p_argument_count != int(p_function.parameter_types.size())) {
		r_call_error.error = p_argument_count < int(p_function.parameter_types.size())
				? GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS
				: GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
		r_call_error.expected = int(p_function.parameter_types.size());
		return {};
	}

	if (p_function.local_types.size() != size_t(p_function.local_count)) {
		r_call_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		fail(r_failure, p_entry_span, "MochaScript bytecode local-type table is inconsistent.");
		return {};
	}
	std::vector<Variant> locals(size_t(p_function.local_count));
	for (int index = 0; index < p_argument_count; ++index) {
		Variant converted;
		if (!convert_value(*p_arguments[index], p_function.parameter_types[size_t(index)], converted)) {
			r_call_error.error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
			r_call_error.argument = index;
			fail(r_failure, p_entry_span, vformat("Argument %d to '%s' has the wrong type.", index + 1, String(p_function.name)));
			return {};
		}
		locals[size_t(index)] = converted;
	}

	std::vector<Variant> stack;
	auto pop = [&](const Instruction &p_instruction, Variant &r_value) -> bool {
		if (stack.empty()) {
			fail(r_failure, p_instruction.span, "MochaScript bytecode stack underflow.");
			r_call_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
			return false;
		}
		r_value = stack.back();
		stack.pop_back();
		return true;
	};
	auto collect_arguments = [&](const Instruction &p_instruction, std::vector<Variant> &r_values,
			std::vector<const Variant *> &r_pointers) -> bool {
		const size_t count = size_t(p_instruction.argument);
		if (stack.size() < count) {
			fail(r_failure, p_instruction.span, "MochaScript bytecode call stack is incomplete.");
			r_call_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
			return false;
		}
		r_values.resize(count);
		for (size_t index = count; index > 0; --index) {
			r_values[index - 1] = stack.back();
			stack.pop_back();
		}
		r_pointers.reserve(count);
		for (Variant &value : r_values) r_pointers.push_back(&value);
		return true;
	};

	int64_t ip = 0;
	while (ip >= 0 && ip < int64_t(p_function.code.size())) {
		if (--vm_operations_remaining < 0) {
			fail(r_failure, p_function.code[size_t(ip)].span, "MochaScript execution exceeded one million bytecode operations.");
			r_call_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
			return {};
		}
		const Instruction &instruction = p_function.code[size_t(ip++)];
		switch (instruction.opcode) {
			case OpCode::PUSH_CONSTANT:
				stack.push_back(instruction.operand);
				break;
			case OpCode::PUSH_SELF:
				if (!owner_is_alive()) {
					fail(r_failure, instruction.span, "The MochaScript owner was freed during execution.");
					return {};
				}
				stack.push_back(Variant(p_owner));
				break;
			case OpCode::LOAD_LOCAL:
				if (instruction.argument < 0 || instruction.argument >= int64_t(locals.size())) {
					fail(r_failure, instruction.span, "Invalid local slot in MochaScript bytecode.");
					return {};
				}
				stack.push_back(locals[size_t(instruction.argument)]);
				break;
			case OpCode::STORE_LOCAL: {
				Variant value;
				if (!pop(instruction, value)) return {};
				if (instruction.argument < 0 || instruction.argument >= int64_t(locals.size())) {
					fail(r_failure, instruction.span, "Invalid local slot in MochaScript bytecode.");
					return {};
				}
				Variant converted;
				if (!convert_value(value, p_function.local_types[size_t(instruction.argument)], converted)) {
					fail(r_failure, instruction.span, "Value stored in a typed local has the wrong runtime type.");
					return {};
				}
				locals[size_t(instruction.argument)] = converted;
			} break;
			case OpCode::LOAD_MEMBER:
				if (instruction.argument < 0 || instruction.argument >= int64_t(p_module.members.size()) ||
						instruction.argument >= r_members.size()) {
					fail(r_failure, instruction.span, "Invalid member slot in MochaScript bytecode.");
					return {};
				}
				stack.push_back(r_members[instruction.argument]);
				break;
			case OpCode::STORE_MEMBER: {
				Variant value;
				if (!pop(instruction, value)) return {};
				if (instruction.argument < 0 || instruction.argument >= int64_t(p_module.members.size()) ||
						instruction.argument >= r_members.size()) {
					fail(r_failure, instruction.span, "Invalid member slot in MochaScript bytecode.");
					return {};
				}
				Variant converted;
				if (!convert_value(value, p_module.members[size_t(instruction.argument)].type, converted)) {
					fail(r_failure, instruction.span, vformat("Value assigned to '%s' has the wrong runtime type.", String(p_module.members[size_t(instruction.argument)].name)));
					return {};
				}
				r_members.write[instruction.argument] = converted;
			} break;
			case OpCode::MAKE_ARRAY: {
				if (stack.size() < size_t(instruction.argument)) { fail(r_failure, instruction.span, "Incomplete array bytecode operands."); return {}; }
				Array array;
				const size_t start = stack.size() - size_t(instruction.argument);
				for (size_t index = start; index < stack.size(); ++index) array.push_back(stack[index]);
				stack.resize(start);
				stack.push_back(array);
			} break;
			case OpCode::MAKE_DICTIONARY: {
				const size_t count = size_t(instruction.argument) * 2;
				if (stack.size() < count) { fail(r_failure, instruction.span, "Incomplete dictionary bytecode operands."); return {}; }
				Dictionary dictionary;
				const size_t start = stack.size() - count;
				for (size_t index = start; index < stack.size(); index += 2) dictionary[stack[index]] = stack[index + 1];
				stack.resize(start);
				stack.push_back(dictionary);
			} break;
			case OpCode::GET_INDEX: {
				Variant key;
				Variant container;
				if (!pop(instruction, key) || !pop(instruction, container)) return {};
				bool valid = false;
				Variant value;
				{
					// Property access may execute arbitrary engine callbacks. This shield
					// prevents their failures from leaking into an enclosing direct call.
					NestedFailureCapture capture(0, StringName());
					value = container.get(key, &valid);
				}
				if (!owner_is_alive()) { fail(r_failure, instruction.span, "The MochaScript owner was freed during an index operation."); return {}; }
				if (!valid) { fail(r_failure, instruction.span, "Invalid index operation in MochaScript."); return {}; }
				stack.push_back(value);
			} break;
			case OpCode::GET_NAMED: {
				Variant receiver;
				if (!pop(instruction, receiver)) return {};
				bool valid = false;
				Variant value;
				{
					// This is a callback shield, not a direct Mocha call capture.
					NestedFailureCapture capture(0, StringName());
					value = receiver.get_named(instruction.name, valid);
				}
				if (!owner_is_alive()) { fail(r_failure, instruction.span, "The MochaScript owner was freed during a member read."); return {}; }
				if (!valid) { fail(r_failure, instruction.span, vformat("Value has no member '%s'.", String(instruction.name))); return {}; }
				stack.push_back(value);
			} break;
			case OpCode::BOOLEANIZE: {
				Variant value;
				if (!pop(instruction, value)) return {};
				stack.push_back(value.booleanize());
			} break;
			case OpCode::UNARY: {
				Variant value;
				if (!pop(instruction, value)) return {};
				if (instruction.operation == MochaTokenizer::Token::TILDE) {
					if (value.get_type() != Variant::INT) {
						fail(r_failure, instruction.span, "Bitwise negation requires an int operand at runtime.");
						return {};
					}
					stack.push_back(integer_from_bits(~uint64_t(int64_t(value))));
					break;
				}
				Variant::Operator operation;
				if (instruction.operation == MochaTokenizer::Token::MINUS) operation = Variant::OP_NEGATE;
				else if (instruction.operation == MochaTokenizer::Token::PLUS) operation = Variant::OP_POSITIVE;
				else if (!variant_operator(instruction.operation, operation)) { fail(r_failure, instruction.span, "Unsupported unary bytecode operator."); return {}; }
				Variant result;
				bool valid = false;
				{
					NestedFailureCapture shield(0, StringName());
					Variant::evaluate(operation, value, Variant(), result, valid);
				}
				if (!owner_is_alive()) { fail(r_failure, instruction.span, "The MochaScript owner was freed during a unary operation."); return {}; }
				if (!valid) { fail(r_failure, instruction.span, "Invalid unary operation in MochaScript."); return {}; }
				stack.push_back(result);
			} break;
			case OpCode::BINARY: {
				Variant right;
				Variant left;
				if (!pop(instruction, right) || !pop(instruction, left)) return {};
				if (requires_integer_operands(instruction.operation) &&
						(left.get_type() != Variant::INT || right.get_type() != Variant::INT)) {
					fail(r_failure, instruction.span, vformat("Operator '%s' requires int operands at runtime.",
							MochaTokenizer::get_token_name(instruction.operation)));
					return {};
				}
				if (requires_integer_operands(instruction.operation)) {
					int64_t result = 0;
					String error;
					if (!evaluate_integer_binary(instruction.operation, int64_t(left), int64_t(right), result, error)) {
						fail(r_failure, instruction.span, error);
						return {};
					}
					stack.push_back(result);
					break;
				}
				Variant::Operator operation;
				if (!variant_operator(instruction.operation, operation)) { fail(r_failure, instruction.span, "Unsupported binary bytecode operator."); return {}; }
				Variant result;
				bool valid = false;
				{
					NestedFailureCapture shield(0, StringName());
					Variant::evaluate(operation, left, right, result, valid);
				}
				if (!owner_is_alive()) { fail(r_failure, instruction.span, "The MochaScript owner was freed during a binary operation."); return {}; }
				if (!valid) { fail(r_failure, instruction.span, "Invalid binary operation in MochaScript."); return {}; }
				stack.push_back(result);
			} break;
			case OpCode::CALL_SELF:
			case OpCode::CALL_METHOD: {
				std::vector<Variant> values;
				std::vector<const Variant *> pointers;
				if (!collect_arguments(instruction, values, pointers)) return {};
				Variant receiver;
				if (instruction.opcode == OpCode::CALL_METHOD) {
					if (!pop(instruction, receiver)) return {};
				} else {
					if (!owner_is_alive()) { fail(r_failure, instruction.span, "The MochaScript owner was freed before a call."); return {}; }
					receiver = Variant(p_owner);
				}
				Variant result;
				GDExtensionCallError nested_error = {};
				nested_error.error = GDEXTENSION_CALL_OK;
				RuntimeFailure nested_failure;
				const uint64_t receiver_id = receiver.get_type() == Variant::OBJECT ? uint64_t(ObjectID(receiver)) : 0;
				if (instruction.opcode == OpCode::CALL_SELF) {
					if (instruction.name == StringName("mocha_language_version")) {
						if (!pointers.empty()) {
							nested_error.error = GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
							nested_error.expected = 0;
						} else {
							result = "MochaScript/bootstrap-1";
						}
					} else if (const CompiledFunction *callee = p_module.find_function(instruction.name)) {
						result = execute(p_module, *callee, p_owner, r_members, pointers.data(), int(pointers.size()), nested_error, r_failure, instruction.span);
					} else {
						NestedFailureCapture capture(receiver_id, instruction.name);
						receiver.callp(instruction.name, pointers.data(), int(pointers.size()), result, nested_error);
						nested_failure = capture.failure;
					}
				} else {
					NestedFailureCapture capture(receiver_id, instruction.name);
					receiver.callp(instruction.name, pointers.data(), int(pointers.size()), result, nested_error);
					nested_failure = capture.failure;
				}
				if (take_nested_failure(nested_failure, r_failure)) {
					r_call_error.error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
					return {};
				}
				if (owner_id != 0 && ObjectDB::get_instance(owner_id) != p_owner) {
					fail(r_failure, instruction.span, "The MochaScript owner was freed during a host call.");
					r_call_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
					return {};
				}
				if (nested_error.error != GDEXTENSION_CALL_OK || !r_failure.message.is_empty()) {
					r_call_error = nested_error;
					if (r_call_error.error == GDEXTENSION_CALL_OK) r_call_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
					if (r_failure.message.is_empty()) fail(r_failure, instruction.span, vformat("Call to '%s' failed.", String(instruction.name)));
					return {};
				}
				stack.push_back(result);
			} break;
			case OpCode::POP: {
				Variant discarded;
				if (!pop(instruction, discarded)) return {};
			} break;
			case OpCode::JUMP:
				ip = instruction.argument;
				break;
			case OpCode::JUMP_IF_FALSE: {
				Variant condition;
				if (!pop(instruction, condition)) return {};
				if (!condition.booleanize()) ip = instruction.argument;
			} break;
			case OpCode::RETURN: {
				Variant result;
				if (!pop(instruction, result)) return {};
				Variant converted;
				if (!convert_value(result, p_function.return_type, converted)) {
					fail(r_failure, instruction.span, vformat("Function '%s' returned the wrong runtime type.", String(p_function.name)));
					r_call_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
					return {};
				}
				return converted;
			}
		}
	}
	return {};
}

Error MochaVM::initialize(const CompiledModule &p_module, Object *p_owner,
		Vector<Variant> &r_members, RuntimeFailure &r_failure) {
	r_members.resize(int64_t(p_module.members.size()));
	for (size_t index = 0; index < p_module.members.size(); ++index) {
		r_members.write[int64_t(index)] = runtime_default(p_module.members[index].type);
	}
	GDExtensionCallError error;
	error.error = GDEXTENSION_CALL_OK;
	VMEntry entry;
	execute(p_module, p_module.initializer, p_owner, r_members, nullptr, 0, error, r_failure, p_module.initializer.span);
	return error.error == GDEXTENSION_CALL_OK && r_failure.message.is_empty() ? OK : ERR_SCRIPT_FAILED;
}

Variant MochaVM::call(const CompiledModule &p_module, const CompiledFunction &p_function,
		Object *p_owner, Vector<Variant> &r_members, const Variant **p_arguments,
		int p_argument_count, GDExtensionCallError &r_call_error,
		RuntimeFailure &r_failure) {
	r_failure = {};
	VMEntry entry;
	Variant result = execute(p_module, p_function, p_owner, r_members, p_arguments, p_argument_count, r_call_error, r_failure, p_function.span);
	if (!r_failure.message.is_empty()) {
		// Internal script failures are already reported with their Mocha location.
		// Returning an argument error here is unsafe when there is no corresponding
		// external argument for Godot's formatter to inspect.
		r_call_error = {};
		r_call_error.error = GDEXTENSION_CALL_OK;
	}
	return result;
}

void MochaVM::publish_nested_failure(const RuntimeFailure &p_failure, uint64_t p_owner_id,
		const StringName &p_method) {
	if (p_failure.message.is_empty() || vm_failure_captures.empty()) return;
	NestedFailureCapture *capture = vm_failure_captures.back();
	if (capture->owner_id != p_owner_id || (!p_method.is_empty() && capture->method != p_method)) return;
	capture->failure = p_failure;
}

} // namespace mocha_script
