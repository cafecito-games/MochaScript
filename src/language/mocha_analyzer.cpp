/**************************************************************************/
/*  mocha_analyzer.cpp                                                    */
/**************************************************************************/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "language/mocha_analyzer.h"

#include <godot_cpp/core/class_db.hpp>

namespace mocha_script {

namespace {

constexpr int MAX_ANALYSIS_EXPRESSION_DEPTH = 256;

struct AnalysisDepthScope {
	int &depth;
	~AnalysisDepthScope() { depth--; }
};

TypeRef type_of_literal(const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::NIL: return TypeRef::make(TypeKind::NIL, "null");
		case Variant::BOOL: return TypeRef::make(TypeKind::BOOL, "bool");
		case Variant::INT: return TypeRef::make(TypeKind::INT, "int");
		case Variant::FLOAT: return TypeRef::make(TypeKind::FLOAT, "float");
		case Variant::STRING:
		case Variant::STRING_NAME: return TypeRef::make(TypeKind::STRING, "String");
		case Variant::ARRAY: return TypeRef::make(TypeKind::ARRAY, "Array");
		case Variant::DICTIONARY: return TypeRef::make(TypeKind::DICTIONARY, "Dictionary");
		case Variant::OBJECT: return TypeRef::make(TypeKind::OBJECT, "Object");
		default: return TypeRef::any();
	}
}

bool numeric(const TypeRef &p_type) {
	return p_type.kind == TypeKind::INT || p_type.kind == TypeKind::FLOAT || p_type.kind == TypeKind::ANY;
}

} // namespace

void MochaAnalyzer::report(const SourceSpan &p_span, const String &p_code, const String &p_message) {
	diagnostics.push_back({ p_code, p_message, path, p_span });
}

const FunctionDecl *MochaAnalyzer::find_function(const StringName &p_name) const {
	for (const FunctionDecl &function : program->functions) {
		if (function.name == p_name) return &function;
	}
	return nullptr;
}

String MochaAnalyzer::type_name(const TypeRef &p_type) const {
	if (!p_type.name.is_empty()) return String(p_type.name);
	switch (p_type.kind) {
		case TypeKind::ANY: return "Variant";
		case TypeKind::NIL: return "null";
		case TypeKind::BOOL: return "bool";
		case TypeKind::INT: return "int";
		case TypeKind::FLOAT: return "float";
		case TypeKind::STRING: return "String";
		case TypeKind::ARRAY: return "Array";
		case TypeKind::DICTIONARY: return "Dictionary";
		case TypeKind::OBJECT: return "Object";
		case TypeKind::VOID: return "void";
	}
	return "Variant";
}

bool MochaAnalyzer::assignable(const TypeRef &p_target, const TypeRef &p_value) const {
	if (p_target.kind == TypeKind::ANY || p_value.kind == TypeKind::ANY) return true;
	if (p_value.kind == TypeKind::NIL) return p_target.kind == TypeKind::OBJECT || p_target.kind == TypeKind::NIL;
	if (p_target.kind == TypeKind::FLOAT && p_value.kind == TypeKind::INT) return true;
	return p_target.kind == p_value.kind;
}

TypeRef MochaAnalyzer::analyze_binary_types(MochaTokenizer::Token::Type p_operation,
		const TypeRef &p_left, const TypeRef &p_right, const SourceSpan &p_span, bool *r_valid) {
	using Token = MochaTokenizer::Token;
	if (r_valid != nullptr) *r_valid = true;
	const bool dynamic = p_left.kind == TypeKind::ANY || p_right.kind == TypeKind::ANY;
	auto invalid = [&]() {
		if (r_valid != nullptr) {
			*r_valid = false;
		} else {
			report(p_span, "M2003", vformat("Operator '%s' does not accept %s and %s.",
					MochaTokenizer::get_token_name(p_operation), type_name(p_left), type_name(p_right)));
		}
		return TypeRef::any();
	};
	switch (p_operation) {
		case Token::EQUAL_EQUAL:
		case Token::BANG_EQUAL:
			if (dynamic || p_left.kind == p_right.kind || (numeric(p_left) && numeric(p_right)) ||
					p_left.kind == TypeKind::NIL || p_right.kind == TypeKind::NIL ||
					(p_left.kind == TypeKind::OBJECT && p_right.kind == TypeKind::OBJECT)) {
				return TypeRef::make(TypeKind::BOOL, "bool");
			}
			return invalid();
		case Token::TK_IN:
			if (dynamic || p_right.kind == TypeKind::ARRAY || p_right.kind == TypeKind::DICTIONARY ||
					((p_right.kind == TypeKind::STRING || p_right.kind == TypeKind::OBJECT) &&
							p_left.kind == TypeKind::STRING)) {
				return TypeRef::make(TypeKind::BOOL, "bool");
			}
			return invalid();
		case Token::IS:
			return TypeRef::make(TypeKind::BOOL, "bool");
		case Token::LESS:
		case Token::LESS_EQUAL:
		case Token::GREATER:
		case Token::GREATER_EQUAL:
			if (dynamic || (numeric(p_left) && numeric(p_right)) ||
					(p_left.kind == TypeKind::STRING && p_right.kind == TypeKind::STRING)) {
				return TypeRef::make(TypeKind::BOOL, "bool");
			}
			return invalid();
		case Token::AND:
		case Token::OR:
		case Token::AMPERSAND_AMPERSAND:
		case Token::PIPE_PIPE:
			if (dynamic || (p_left.kind == TypeKind::BOOL && p_right.kind == TypeKind::BOOL)) {
				return TypeRef::make(TypeKind::BOOL, "bool");
			}
			return invalid();
		case Token::PLUS:
			if (p_left.kind == TypeKind::STRING && p_right.kind == TypeKind::STRING) {
				return TypeRef::make(TypeKind::STRING, "String");
			}
			[[fallthrough]];
		case Token::MINUS:
		case Token::STAR:
			if (dynamic) return TypeRef::any();
			if (!numeric(p_left) || !numeric(p_right)) return invalid();
			return p_left.kind == TypeKind::FLOAT || p_right.kind == TypeKind::FLOAT
					? TypeRef::make(TypeKind::FLOAT, "float")
					: TypeRef::make(TypeKind::INT, "int");
		case Token::STAR_STAR:
			if (dynamic) return TypeRef::any();
			if (!numeric(p_left) || !numeric(p_right)) return invalid();
			return p_left.kind == TypeKind::FLOAT || p_right.kind == TypeKind::FLOAT
					? TypeRef::make(TypeKind::FLOAT, "float")
					: TypeRef::make(TypeKind::INT, "int");
		case Token::SLASH:
			if (dynamic) return TypeRef::any();
			if (!numeric(p_left) || !numeric(p_right)) return invalid();
			return p_left.kind == TypeKind::FLOAT || p_right.kind == TypeKind::FLOAT
					? TypeRef::make(TypeKind::FLOAT, "float")
					: TypeRef::make(TypeKind::INT, "int");
		case Token::PERCENT:
		case Token::LESS_LESS:
		case Token::GREATER_GREATER:
		case Token::AMPERSAND:
		case Token::PIPE:
		case Token::CARET:
			if (dynamic) return TypeRef::any();
			if (p_left.kind != TypeKind::INT || p_right.kind != TypeKind::INT) return invalid();
			return TypeRef::make(TypeKind::INT, "int");
		default:
			return invalid();
	}
}

void MochaAnalyzer::validate_type(const TypeRef &p_type, const SourceSpan &p_span, const String &p_context, bool p_allow_void) {
	if (p_type.kind == TypeKind::VOID && !p_allow_void) {
		report(p_span, "M2026", vformat("Type 'void' is only valid as a function return type, not %s.", p_context));
		return;
	}
	if (p_type.kind == TypeKind::OBJECT &&
			(p_type.name.is_empty() || !ClassDB::class_exists(p_type.name))) {
		report(p_span, "M2024", vformat("Unknown or unsupported object type '%s' %s.", String(p_type.name), p_context));
	}
}

TypeRef MochaAnalyzer::analyze_expression(ExprPtr &p_expression, Context &r_context, bool p_value_required) {
	if (!p_expression) return TypeRef::any();
	if (++expression_depth > MAX_ANALYSIS_EXPRESSION_DEPTH) {
		expression_depth--;
		report(p_expression->span, "M2028", vformat("Expression tree exceeds the analysis limit of %d.", MAX_ANALYSIS_EXPRESSION_DEPTH));
		p_expression->inferred_type = TypeRef::any();
		return TypeRef::any();
	}
	AnalysisDepthScope depth_scope{ expression_depth };
	TypeRef result = TypeRef::any();
	switch (p_expression->kind) {
		case ExprKind::LITERAL:
			result = type_of_literal(p_expression->literal);
			break;
		case ExprKind::SELF:
			result = TypeRef::make(TypeKind::OBJECT, program->base_type);
			break;
		case ExprKind::IDENTIFIER: {
			if (const TypeRef *local = r_context.locals.getptr(p_expression->name)) result = *local;
			else if (const TypeRef *member = members.getptr(p_expression->name)) result = *member;
			else {
				report(p_expression->span, "M2001", vformat("Unknown identifier '%s'.", String(p_expression->name)));
			}
		} break;
		case ExprKind::ARRAY:
			for (ExprPtr &item : p_expression->arguments) analyze_expression(item, r_context);
			result = TypeRef::make(TypeKind::ARRAY, "Array");
			break;
		case ExprKind::DICTIONARY:
			for (auto &entry : p_expression->entries) {
				analyze_expression(entry.first, r_context);
				analyze_expression(entry.second, r_context);
			}
			result = TypeRef::make(TypeKind::DICTIONARY, "Dictionary");
			break;
		case ExprKind::UNARY: {
			const TypeRef operand = analyze_expression(p_expression->right, r_context);
			if (p_expression->operation == MochaTokenizer::Token::NOT || p_expression->operation == MochaTokenizer::Token::BANG) {
				result = TypeRef::make(TypeKind::BOOL, "bool");
			} else if (p_expression->operation == MochaTokenizer::Token::TILDE) {
				if (operand.kind != TypeKind::INT && operand.kind != TypeKind::ANY) {
					report(p_expression->span, "M2002", "Bitwise negation requires int.");
				}
				result = operand.kind == TypeKind::ANY ? TypeRef::any() : TypeRef::make(TypeKind::INT, "int");
			} else {
				if (!numeric(operand)) report(p_expression->span, "M2002", "A numeric unary operator requires int or float.");
				result = operand;
			}
		} break;
		case ExprKind::BINARY: {
			const TypeRef left = analyze_expression(p_expression->left, r_context);
			const TypeRef right = analyze_expression(p_expression->right, r_context);
			result = analyze_binary_types(p_expression->operation, left, right, p_expression->span);
		} break;
		case ExprKind::MEMBER:
			analyze_expression(p_expression->left, r_context);
			result = TypeRef::any();
			break;
		case ExprKind::INDEX: {
			const TypeRef container = analyze_expression(p_expression->left, r_context);
			analyze_expression(p_expression->right, r_context);
			if (container.kind != TypeKind::ARRAY && container.kind != TypeKind::DICTIONARY && container.kind != TypeKind::STRING && container.kind != TypeKind::ANY) {
				report(p_expression->span, "M2004", vformat("Values of type %s cannot be indexed.", type_name(container)));
			}
			result = TypeRef::any();
		} break;
		case ExprKind::CALL: {
			for (ExprPtr &argument : p_expression->arguments) analyze_expression(argument, r_context);
			if (p_expression->left && p_expression->left->kind == ExprKind::IDENTIFIER) {
				if (p_expression->left->name == StringName("mocha_language_version")) {
					if (!p_expression->arguments.empty()) {
						report(p_expression->span, "M2007", "mocha_language_version() takes no arguments.");
					}
					result = TypeRef::make(TypeKind::STRING, "String");
				} else if (const FunctionDecl *function = find_function(p_expression->left->name)) {
					if (function->parameters.size() != p_expression->arguments.size()) {
						report(p_expression->span, "M2005", vformat("Function '%s' expects %d arguments but received %d.", String(function->name), function->parameters.size(), p_expression->arguments.size()));
					}
					for (size_t i = 0; i < MIN(function->parameters.size(), p_expression->arguments.size()); ++i) {
						if (!assignable(function->parameters[i].type, p_expression->arguments[i]->inferred_type)) {
							report(p_expression->arguments[i]->span, "M2006", vformat("Argument %d of '%s' requires %s, received %s.", i + 1, String(function->name), type_name(function->parameters[i].type), type_name(p_expression->arguments[i]->inferred_type)));
						}
					}
					result = function->return_type;
				} else {
					// Unqualified calls may target the Godot owner. Their public signature
					// is checked by Godot at runtime; no private ClassDB ABI is used here.
					result = TypeRef::any();
				}
			} else {
				analyze_expression(p_expression->left, r_context);
				result = TypeRef::any();
			}
		} break;
	}
	p_expression->inferred_type = result;
	if (p_value_required && result.kind == TypeKind::VOID) {
		report(p_expression->span, "M2008", "A void expression cannot be used as a value.");
	}
	return result;
}

void MochaAnalyzer::analyze_statement(StmtPtr &p_statement, Context &r_context) {
	if (!p_statement) return;
	switch (p_statement->kind) {
		case StatementKind::VARIABLE: {
			if (r_context.locals.has(p_statement->name)) {
				report(p_statement->span, "M2010", vformat("Local '%s' is already declared.", String(p_statement->name)));
				break;
			}
			TypeRef value = p_statement->expression ? analyze_expression(p_statement->expression, r_context) : TypeRef::any();
			TypeRef type = p_statement->declared_type;
			if (type.kind == TypeKind::ANY && value.kind != TypeKind::NIL && value.kind != TypeKind::VOID) type = value;
			p_statement->declared_type = type;
			validate_type(type, p_statement->span, vformat("for local '%s'", String(p_statement->name)));
			if (p_statement->expression && !assignable(type, value)) {
				report(p_statement->span, "M2011", vformat("Cannot initialize '%s' of type %s with %s.", String(p_statement->name), type_name(type), type_name(value)));
			}
			r_context.locals.insert(p_statement->name, type);
		} break;
		case StatementKind::EXPRESSION:
			analyze_expression(p_statement->expression, r_context, false);
			break;
		case StatementKind::ASSIGNMENT: {
			TypeRef target = TypeRef::any();
			if (p_statement->target && p_statement->target->kind == ExprKind::IDENTIFIER) {
				if (const TypeRef *local = r_context.locals.getptr(p_statement->target->name)) target = *local;
				else if (const TypeRef *member = members.getptr(p_statement->target->name)) target = *member;
				else report(p_statement->target->span, "M2012", vformat("Cannot assign unknown identifier '%s'.", String(p_statement->target->name)));
			} else {
				report(p_statement->span, "M2090", "This bootstrap currently supports assignment only to local or member names.");
			}
			const TypeRef value = analyze_expression(p_statement->expression, r_context);
			TypeRef stored = value;
			bool operation_valid = true;
			if (p_statement->operation != MochaTokenizer::Token::EQUAL) {
				MochaTokenizer::Token::Type binary_operation = MochaTokenizer::Token::EMPTY;
				switch (p_statement->operation) {
					case MochaTokenizer::Token::PLUS_EQUAL: binary_operation = MochaTokenizer::Token::PLUS; break;
					case MochaTokenizer::Token::MINUS_EQUAL: binary_operation = MochaTokenizer::Token::MINUS; break;
					case MochaTokenizer::Token::STAR_EQUAL: binary_operation = MochaTokenizer::Token::STAR; break;
					case MochaTokenizer::Token::SLASH_EQUAL: binary_operation = MochaTokenizer::Token::SLASH; break;
					case MochaTokenizer::Token::PERCENT_EQUAL: binary_operation = MochaTokenizer::Token::PERCENT; break;
					default: break;
				}
				stored = analyze_binary_types(binary_operation, target, value, p_statement->span, &operation_valid);
			}
			if (!operation_valid || !assignable(target, stored)) {
				report(p_statement->span, "M2013", vformat("Cannot assign %s to %s.", type_name(stored), type_name(target)));
			}
		} break;
		case StatementKind::RETURN: {
			const TypeRef value = p_statement->expression ? analyze_expression(p_statement->expression, r_context) : TypeRef::make(TypeKind::VOID, "void");
			if (!assignable(r_context.return_type, value)) {
				report(p_statement->span, "M2014", vformat("Function returns %s but this return produces %s.", type_name(r_context.return_type), type_name(value)));
			}
		} break;
		case StatementKind::IF:
		case StatementKind::WHILE: {
			const TypeRef condition = analyze_expression(p_statement->expression, r_context);
			if (condition.kind != TypeKind::BOOL && condition.kind != TypeKind::ANY) {
				report(p_statement->expression->span, "M2015", "A control-flow condition must be bool.");
			}
			Context nested = r_context;
			analyze_block(p_statement->body, nested);
			if (p_statement->kind == StatementKind::IF) {
				Context alternative = r_context;
				analyze_block(p_statement->else_body, alternative);
			}
		} break;
		case StatementKind::PASS:
			break;
	}
}

void MochaAnalyzer::analyze_block(std::vector<StmtPtr> &p_block, Context &r_context) {
	for (StmtPtr &statement : p_block) analyze_statement(statement, r_context);
}

Error MochaAnalyzer::analyze(Program &p_program, const String &p_path) {
	program = &p_program;
	path = p_path.is_empty() ? String("<memory>") : p_path;
	members.clear();
	diagnostics.clear();
	expression_depth = 0;
	if (!ClassDB::class_exists(p_program.base_type)) {
		report({}, "M2025", vformat("Unknown or unsupported base class '%s'.", String(p_program.base_type)));
	}

	HashMap<StringName, bool> functions;
	for (MemberDecl &member : p_program.members) {
		validate_type(member.type, member.span, vformat("for member '%s'", String(member.name)));
		if (members.has(member.name)) {
			report(member.span, "M2020", vformat("Member '%s' is already declared.", String(member.name)));
		} else {
			members.insert(member.name, member.type);
		}
	}
	for (FunctionDecl &function : p_program.functions) {
		if (functions.has(function.name)) report(function.span, "M2021", vformat("Function '%s' is already declared.", String(function.name)));
		else functions.insert(function.name, true);
	}

	Context member_context;
	for (MemberDecl &member : p_program.members) {
		if (!member.initializer) continue;
		const TypeRef value = analyze_expression(member.initializer, member_context);
		if (member.type.kind == TypeKind::ANY && value.kind != TypeKind::NIL && value.kind != TypeKind::VOID) {
			member.type = value;
			members[member.name] = value;
		} else if (!assignable(member.type, value)) {
			report(member.span, "M2022", vformat("Cannot initialize member '%s' of type %s with %s.", String(member.name), type_name(member.type), type_name(value)));
		}
	}

	for (FunctionDecl &function : p_program.functions) {
		Context context;
		context.return_type = function.return_type;
		validate_type(function.return_type, function.span, vformat("as the return type of '%s'", String(function.name)), true);
		if (function.name == StringName("_notification") &&
				(function.parameters.size() < 1 || function.parameters.size() > 2)) {
			report(function.span, "M2027", "_notification must declare one or two parameters.");
		} else if (function.name == StringName("_notification")) {
			const bool first_valid = function.parameters[0].type.kind == TypeKind::INT || function.parameters[0].type.kind == TypeKind::ANY;
			const bool second_valid = function.parameters.size() == 1 || function.parameters[1].type.kind == TypeKind::BOOL || function.parameters[1].type.kind == TypeKind::ANY;
			if (!first_valid || !second_valid || function.return_type.kind != TypeKind::VOID) {
				report(function.span, "M2027", "_notification requires (int[, bool]) parameters and a void return type.");
			}
		}
		for (const Parameter &parameter : function.parameters) {
			validate_type(parameter.type, parameter.span, vformat("for parameter '%s'", String(parameter.name)));
			if (context.locals.has(parameter.name)) report(parameter.span, "M2023", vformat("Parameter '%s' is declared more than once.", String(parameter.name)));
			else context.locals.insert(parameter.name, parameter.type);
		}
		analyze_block(function.body, context);
	}
	return diagnostics.empty() ? OK : ERR_COMPILATION_FAILED;
}

} // namespace mocha_script
