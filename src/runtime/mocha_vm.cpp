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
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <cstdint>
#include <limits>

namespace mocha_script {

namespace {

thread_local int vm_entry_count = 0;
thread_local int vm_frame_depth = 0;
thread_local int64_t vm_operations_remaining = 0;

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

Variant::Type variant_type_for(const TypeRef &p_type) {
	switch (p_type.kind) {
		case TypeKind::BOOL: return Variant::BOOL;
		case TypeKind::INT: return Variant::INT;
		case TypeKind::FLOAT: return Variant::FLOAT;
		case TypeKind::STRING: return Variant::STRING;
		case TypeKind::ARRAY: return Variant::ARRAY;
		case TypeKind::DICTIONARY: return Variant::DICTIONARY;
		case TypeKind::OBJECT: return Variant::OBJECT;
		case TypeKind::ANY:
		case TypeKind::NIL:
		case TypeKind::VOID: return Variant::NIL;
	}
	return Variant::NIL;
}

struct NestedFailureCapture {
	RuntimeFailure *failure = nullptr;
	uint64_t owner_id = 0;
	const StringName *expected_method = nullptr;
	const StringName *alternate_method = nullptr;
};

thread_local std::vector<NestedFailureCapture> vm_failure_captures;

struct NestedFailureCaptureScope {
	RuntimeFailure captured_failure;
	NestedFailureCaptureScope(uint64_t p_owner_id, const StringName &p_expected_method,
			const StringName &p_alternate_method) {
		vm_failure_captures.push_back({ &captured_failure, p_owner_id, &p_expected_method, &p_alternate_method });
	}
	~NestedFailureCaptureScope() { vm_failure_captures.pop_back(); }
	RuntimeFailure failure() const { return captured_failure; }
};

struct VMNames {
	StringName call = "call";
	StringName callv = "callv";
	StringName set_script = "set_script";
	StringName language_version = "mocha_language_version";
	StringName object = "Object";
};

const VMNames &vm_names() {
	static const VMNames names;
	return names;
}

bool resolve_forwarded_dispatch(const StringName &p_method,
		const std::vector<Variant> &p_arguments, StringName &r_method) {
	const VMNames &names = vm_names();
	StringName method = p_method;
	std::vector<Variant> arguments = p_arguments;
	for (int depth = 0; depth < 64; ++depth) {
		if (method == names.call) {
			if (arguments.empty() || (arguments[0].get_type() != Variant::STRING &&
					arguments[0].get_type() != Variant::STRING_NAME)) {
				return false;
			}
			method = StringName(arguments[0]);
			arguments.erase(arguments.begin());
		} else if (method == names.callv) {
			if (arguments.size() < 2 || (arguments[0].get_type() != Variant::STRING &&
					arguments[0].get_type() != Variant::STRING_NAME) ||
					arguments[1].get_type() != Variant::ARRAY) {
				return false;
			}
			method = StringName(arguments[0]);
			const Array packed_arguments = arguments[1];
			arguments.clear();
			arguments.reserve(size_t(packed_arguments.size()));
			for (int64_t index = 0; index < packed_arguments.size(); ++index) {
				arguments.push_back(packed_arguments[index]);
			}
		} else {
			r_method = method;
			return !r_method.is_empty();
		}
	}
	return false;
}

bool take_nested_failure(const RuntimeFailure &p_captured, const String &p_call_path,
		const SourceSpan &p_call_span,
		RuntimeFailure &r_failure) {
	if (p_captured.message.is_empty()) return false;
	r_failure = p_captured;
	// The captured error belongs to an engine-mediated inner call, not to the
	// arguments of the outer host entry. Keeping INVALID_ARGUMENT metadata here
	// can make Godot inspect an argument that the outer call does not have.
	r_failure.preserve_call_error = false;
	if (r_failure.call_path.is_empty()) {
		r_failure.call_path = p_call_path;
		r_failure.call_span = p_call_span;
	}
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

MochaNestedFailureShield::MochaNestedFailureShield() {
	vm_failure_captures.push_back({ nullptr, 0, nullptr, nullptr });
}
MochaNestedFailureShield::~MochaNestedFailureShield() {
	vm_failure_captures.pop_back();
}

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
			if (object != nullptr && !p_type.name.is_empty() && p_type.name != vm_names().object &&
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
		RuntimeFailure &r_failure, const SourceSpan &p_entry_span, bool p_external_entry) {
	if (r_failure.path.is_empty()) r_failure.path = p_module.path;
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
			r_call_error.expected = int32_t(variant_type_for(p_function.parameter_types[size_t(index)]));
			r_failure.preserve_call_error = p_external_entry;
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
				if (container.get_type() == Variant::OBJECT) {
					// Property access may execute arbitrary engine callbacks. This shield
					// prevents their failures from leaking into an enclosing direct call.
					MochaNestedFailureShield shield;
					value = container.get(key, &valid);
				} else {
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
				if (receiver.get_type() == Variant::OBJECT) {
					// This is a callback shield, not a direct Mocha call capture.
					MochaNestedFailureShield shield;
					value = receiver.get_named(instruction.name, valid);
				} else {
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
				if (value.get_type() == Variant::OBJECT) {
					MochaNestedFailureShield shield;
					Variant::evaluate(operation, value, Variant(), result, valid);
				} else {
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
				if (left.get_type() == Variant::OBJECT || right.get_type() == Variant::OBJECT) {
					MochaNestedFailureShield shield;
					Variant::evaluate(operation, left, right, result, valid);
				} else {
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
				uint64_t receiver_id = receiver.get_type() == Variant::OBJECT ? uint64_t(ObjectID(receiver)) : 0;
				StringName expected_method = instruction.name;
				StringName alternate_method;
				bool capture_host_failure = true;
				const VMNames &names = vm_names();
				if (receiver.get_type() == Variant::CALLABLE &&
						(instruction.name == names.call || instruction.name == names.callv)) {
					const Callable callable = receiver;
					if (callable.is_standard()) {
						receiver_id = uint64_t(callable.get_object_id());
						expected_method = callable.get_method();
						capture_host_failure = !expected_method.is_empty();
						std::vector<Variant> forwarded_arguments;
						if (instruction.name == names.call && !values.empty()) {
							forwarded_arguments = values;
						} else if (instruction.name == names.callv && values.size() == 1 &&
								values[0].get_type() == Variant::ARRAY) {
							const Array packed_arguments = values[0];
							forwarded_arguments.reserve(size_t(packed_arguments.size()));
							for (int64_t index = 0; index < packed_arguments.size(); ++index) {
								forwarded_arguments.push_back(packed_arguments[index]);
							}
						}
						resolve_forwarded_dispatch(expected_method, forwarded_arguments, alternate_method);
					} else {
						// GDExtension cannot safely recover the ultimate target method from
						// custom wrappers, including bound and unbound callables. Shield them
						// so a reentrant callback cannot claim this dispatch.
						capture_host_failure = false;
					}
				} else if (instruction.name == names.call || instruction.name == names.callv) {
					resolve_forwarded_dispatch(instruction.name, values, alternate_method);
				}
				auto invoke_host_call = [&]() {
					if (capture_host_failure) {
						NestedFailureCaptureScope capture(receiver_id, expected_method, alternate_method);
						receiver.callp(instruction.name, pointers.data(), int(pointers.size()), result, nested_error);
						nested_failure = capture.failure();
					} else {
						MochaNestedFailureShield shield;
						receiver.callp(instruction.name, pointers.data(), int(pointers.size()), result, nested_error);
					}
				};
				if (instruction.opcode == OpCode::CALL_SELF) {
					if (instruction.name == names.language_version) {
						if (!pointers.empty()) {
							nested_error.error = GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
							nested_error.expected = 0;
						} else {
							result = "MochaScript/bootstrap-1";
						}
					} else if (const CompiledFunction *callee = p_module.find_function(instruction.name)) {
						result = execute(p_module, *callee, p_owner, r_members, pointers.data(), int(pointers.size()), nested_error, r_failure, instruction.span, false);
					} else {
						invoke_host_call();
					}
				} else {
					invoke_host_call();
				}
				if (take_nested_failure(nested_failure, p_module.path, instruction.span, r_failure)) {
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
	r_failure = {};
	r_members.resize(int64_t(p_module.members.size()));
	for (size_t index = 0; index < p_module.members.size(); ++index) {
		r_members.write[int64_t(index)] = runtime_default(p_module.members[index].type);
	}
	GDExtensionCallError error;
	error.error = GDEXTENSION_CALL_OK;
	VMEntry entry;
	execute(p_module, p_module.initializer, p_owner, r_members, nullptr, 0, error, r_failure, p_module.initializer.span, false);
	return error.error == GDEXTENSION_CALL_OK && r_failure.message.is_empty() ? OK : ERR_SCRIPT_FAILED;
}

Variant MochaVM::call(const CompiledModule &p_module, const CompiledFunction &p_function,
		Object *p_owner, Vector<Variant> &r_members, const Variant **p_arguments,
		int p_argument_count, GDExtensionCallError &r_call_error,
		RuntimeFailure &r_failure) {
	r_failure = {};
	VMEntry entry;
	Variant result = execute(p_module, p_function, p_owner, r_members, p_arguments, p_argument_count, r_call_error, r_failure, p_function.span, true);
	if (!r_failure.message.is_empty() && !r_failure.preserve_call_error) {
		// Internal script failures are already reported with their Mocha location.
		// Returning an argument error here is unsafe when there is no corresponding
		// external argument for Godot's formatter to inspect.
		r_call_error = {};
		r_call_error.error = GDEXTENSION_CALL_OK;
	}
	return result;
}

String MochaVM::format_failure(const RuntimeFailure &p_failure, const String &p_fallback_path) {
	const String failure_path = p_failure.path.is_empty() ? p_fallback_path : p_failure.path;
	String message = vformat("%s:%d:%d: %s: %s", failure_path, p_failure.span.line,
			p_failure.span.column, p_failure.code, p_failure.message);
	if (!p_failure.call_path.is_empty()) {
		message += vformat(" (called from %s:%d:%d)", p_failure.call_path,
				p_failure.call_span.line, p_failure.call_span.column);
	}
	return message;
}

bool MochaVM::publish_nested_failure(const RuntimeFailure &p_failure, uint64_t p_owner_id,
		const StringName &p_method) {
	if (p_failure.message.is_empty() || vm_failure_captures.empty()) return false;
	NestedFailureCapture &capture = vm_failure_captures.back();
	if (capture.failure == nullptr || capture.expected_method == nullptr) return false;
	const StringName &expected_method = *capture.expected_method;
	const StringName &alternate_method = *capture.alternate_method;
	bool method_matches = false;
	if (p_method.is_empty()) {
		method_matches = expected_method == vm_names().set_script || alternate_method == vm_names().set_script;
	} else {
		method_matches = expected_method == p_method ||
				(!alternate_method.is_empty() && alternate_method == p_method);
	}
	if (capture.owner_id != p_owner_id || !capture.failure->message.is_empty() || !method_matches) return false;
	*capture.failure = p_failure;
	capture.failure->preserve_call_error = false;
	return true;
}

#ifdef DEBUG_ENABLED
bool MochaVM::test_failure_capture_match(uint64_t p_capture_owner, const StringName &p_expected_method,
		const StringName &p_alternate_method, uint64_t p_publication_owner,
		const StringName &p_publication_method) {
	NestedFailureCaptureScope capture(p_capture_owner, p_expected_method, p_alternate_method);
	RuntimeFailure failure;
	failure.code = "M4001";
	failure.message = "capture test";
	return publish_nested_failure(failure, p_publication_owner, p_publication_method);
}
#endif

} // namespace mocha_script
