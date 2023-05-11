#include <toycc.h>
#include <type.h>
#include <declarator.h>
#include <initializer.h>
#include <scope.h>
#include <parser.h>

struct Node *new_cast(struct Node *expr, struct Type *ty)
{
	add_type(expr);

	struct Node *n = malloc(sizeof(struct Node));

	n->kind = ND_CAST;
	n->tok = expr->tok;
	n->lhs = expr;
	// explicit conversion of type
	n->ty = copy_type(ty);
	return n;
}

// struct-members = (declspec declarator ("," declarator)* ";")*
static void struct_members(struct Token **rest, struct Token *tok, struct Type *ty)
{
	struct Member head = {};
	struct Member *cur = &head;
	int idx = 0;

	while (!equal(tok, "}")) {
		struct VarAttr attr = {};
		struct Type *basety = declspec(&tok, tok, &attr);
		bool first = true;

		while (!consume(&tok, tok, ";")) {
			if (!first)
				tok = skip(tok, ",");
			first = false;

			struct Member *mem = malloc(sizeof(struct Member));
			mem->ty = declarator(&tok, tok, basety);
			mem->name = mem->ty->name;
			mem->idx = idx++;
			// if _Alignas set, refer to attr.align,
			// otherwise refer to ty->align.
			mem->align = attr.align ? attr.align : mem->ty->align;
			cur->next = mem;
			cur = cur->next;
		}
	}

	// If the last element is an array of incomplete type, it's
	// called a "flexible array member". It should behave as it
	// were a zero-sized array.
	if (cur != &head && cur->ty->kind == TY_ARRAY && cur->ty->array_len < 0) {
		cur->ty = array_of(cur->ty->base, 0);
		ty->is_flexible = true;
	}

	// skip "}"
	*rest = tok->next;
	ty->members = head.next;
}

// struct-union-decl = ident? ("{" struct-members)?
static struct Type *struct_union_decl(struct Token **rest, struct Token *tok)
{
	// read a struct/union tag
	struct Token *tag = NULL;

	if (tok->kind == TK_IDENT) {
		tag = tok;
		tok = tok->next;
	}

	// not struct/union declaration but variable
	if (tag && !equal(tok, "{")) {
		*rest = tok;

		struct Type *ty = find_tag(tag);
		if (ty)
			return ty;

		// no struct/union declare yet
		ty = struct_type();
		ty->size = -1;
		push_tag_scope(tag, ty);
		return ty;
	}

	tok = skip(tok, "{");

	// Construct a struct object
	struct Type *ty = struct_type();
	struct_members(rest, tok, ty);

	// register the struct type if a name was given
	if (tag) {
		// If there is a redefinition, overwrite a previous type.
		struct Type *ret = overwrite_tag(tag, ty);
		if (ret)
			return ret;

		// Otherwise, register the struct type.
		push_tag_scope(tag, ty);
	}

	return ty;
}

// struct-decl = struct-union-decl
static struct Type *struct_decl(struct Token **rest, struct Token *tok)
{
	struct Type *ty = struct_union_decl(rest, tok);
	ty->kind = TY_STRUCT;

	if (ty->size < 0)
		return ty;

	// Assign offsets within the struct to members
	int offset = 0;
	for (struct Member *mem = ty->members; mem; mem = mem->next) {
		offset = align_to(offset, mem->align);
		mem->offset = offset;
		offset += mem->ty->size;

		// align of struct is max among each member's alignment
		if (ty->align < mem->align)
			ty->align = mem->align;
	}
	ty->size = align_to(offset, ty->align);

	return ty;
}

// union-decl = struct-union-decl
static struct Type *union_decl(struct Token **rest, struct Token *tok)
{
	struct Type *ty = struct_union_decl(rest, tok);
	ty->kind = TY_UNION;

	if (ty->size < 0)
		return ty;

	// If union, we don't have to assign offsets because they
	// are already initialized to 0.
	// We need to compute the alignment and the size though.
	for (struct Member *mem = ty->members; mem; mem = mem->next) {
		if (ty->align < mem->align)
			ty->align = mem->align;
		if (ty->size < mem->ty->size)
			ty->size = mem->ty->size;
	}
	ty->size = align_to(ty->size, ty->align);

	return ty;
}

// enum-specifier = ident? "{" enum-list? "}"
//		  | ident ("{" enum-list? "}")?
//
// enum-list      = ident ("=" num)? ("," ident ("=" num)?)*
static struct Type *enum_specifier(struct Token **rest, struct Token *tok)
{
	struct Type *ty = enum_type();

	// read a enum tag
	struct Token *tag = NULL;
	if (tok->kind == TK_IDENT) {
		tag = tok;
		tok = tok->next;
	}

	// enum variable
	if (tag && !equal(tok, "{")) {
		struct Type *ty = find_tag(tag);
		if (!ty)
			error_tok(tag, "unknown enum type");
		if (ty->kind != TY_ENUM)
			error_tok(tag, "not an enum tag");
		*rest = tok;
		return ty;
	}

	// enum definition
	tok = skip(tok, "{");

	// read an enum-list
	int i = 0, val = 0;
	while (!consume_end(rest, tok)) {
		if (i++ > 0)
			tok = skip(tok, ",");

		const char *name = get_ident(tok);
		tok = tok->next;

		if (equal(tok, "=")) {
			val = const_expr(&tok, tok->next);
		}

		struct VarScope *sc = push_scope(name);
		sc->enum_ty = ty;
		sc->enum_val = val++;
	}

	if (tag)
		push_tag_scope(tag, ty);
	return ty;
}

static struct Type *type_suffix(struct Token **rest, struct Token *tok,
				struct Type *ty);
// abstract_declarator = "*"* ("(" abstract-declarator ")")? type-suffix
static struct Type *abstract_declarator(struct Token **rest, struct Token *tok, struct Type *ty)
{
	// like "sizeof(char *)"
	while (equal(tok, "*")) {
		ty = pointer_to(ty);
		tok = tok->next;
	}

	// like "sizeof(char(*)[4])"
	if (equal(tok, "(")) {
		struct Token *start = tok->next;
		struct Type dummy = {};

		// get 'type' & update 'rest'
		abstract_declarator(&tok, start, &dummy);
		tok = skip(tok, ")");
		ty = type_suffix(rest, tok, ty);

		return abstract_declarator(&tok, start, ty);
	}

	// like "sizeof(char[4][4])"
	return type_suffix(rest, tok, ty);
}

struct Type *typename(struct Token **rest, struct Token *tok)
{
	struct Type *base = declspec(&tok, tok, NULL);
	return abstract_declarator(rest, tok, base);
}

static struct Type *find_typedef(struct Token *tok)
{
	if (tok->kind == TK_IDENT) {
		struct VarScope *sc = find_var(tok);
		if (sc)
			return sc->type_def;
	}
	return NULL;
}

// Returns true if a given token represents a type.
bool is_typename(struct Token *tok)
{
	static const char * const kw[] = {
		"void",
		"_Bool",
		"char",
		"short",
		"int",
		"long",
		"struct",
		"union",
		"typedef",
		"enum",
		"static",
		"extern",
		"_Alignas",
		"signed",
		"unsigned",
	};

	for (size_t i = 0; i < ARRAY_SIZE(kw); i++)
		if (equal(tok, kw[i]))
			return true;

	return find_typedef(tok);
}

// declspec = ("void" | "_Bool" | "char" | "short" | "int" | "long" |
//		"typedef" | "static" | "extern" |
//		"signed" | "unsigned" |
//		struct-decl | union-decl | typedef-name |
//		enum-specifier)+
//
// The order of typenames in a type-specifier doesn't matter. For
// example, `int long static` means the same as `static long int`.
// That can also be written as `static long` because you can omit
// `int` if `long` or `short` are specified. However, something like
// `char int` is not a valid type specifier. We have to accept only a
// limited combinations of the typenames.
//
// In this function, we count the number of occurrences of each typename
// while keeping the "current" type object that the typenames up
// until that point represent. When we reach a non-typename token,
// we returns the current type object.
struct Type *declspec(struct Token **rest, struct Token *tok,
                      struct VarAttr *attr)
{
	// We use a single integer as counters for all typenames.
	// For example, bits 0 and 1 represents how many times we saw the
	// keyword "void" so far. With this, we can use a switch statement
	// as you can see below.
	enum {
		VOID     = 1 << 0,
		BOOL     = 1 << 2,
		CHAR     = 1 << 4,
		SHORT    = 1 << 6,
		INT      = 1 << 8,
		LONG     = 1 << 10,
		OTHER    = 1 << 12,
		SIGNED   = 1 << 13,
		UNSIGNED = 1 << 14,
	};

	// "typedef t" means "typedef int t"
	struct Type *ty = p_ty_int();
	int counter = 0;

	while (is_typename(tok)) {
		// handle "typedef" keyword or handle storage class specifiers
		if (equal(tok, "typedef") || equal(tok, "static") || equal(tok, "extern")) {
			if (!attr)
				error_tok(tok, "storage class specifier is not allowed in this context");

			if (equal(tok, "typedef"))
				attr->is_typedef = true;
			else if (equal(tok, "static"))
				attr->is_static = true;
			else
				attr->is_extern = true;

			if (attr->is_typedef && (attr->is_static || attr->is_extern))
				error_tok(tok, "typedef may not be used together with static or extern");

			tok = tok->next;
			continue;
		}

		// if _Alignas then set attr->align
		if (equal(tok, "_Alignas")) {
			if (!attr)
				error_tok(tok, "_Alignas is not allowed in this context");
			tok = skip(tok->next, "(");

			if (is_typename(tok)) {
				attr->align = typename(&tok, tok)->align;
			} else {
				attr->align = const_expr(&tok, tok);
			}

			tok = skip(tok, ")");
			continue;
		}

		// Handle user-defined types.
		struct Type *ty2 = find_typedef(tok);
		if (equal(tok, "struct") || equal(tok, "union") ||
		    equal(tok, "enum") || ty2) {
			if (counter)
				break;

			if (equal(tok, "struct")) {
				ty = struct_decl(&tok, tok->next);

			} else if (equal(tok, "union")) {
				ty = union_decl(&tok, tok->next);

			} else if (equal(tok, "enum")) {
				ty = enum_specifier(&tok, tok->next);

			} else {
				ty = ty2;
				tok = tok->next;
			}
			counter += OTHER;
			continue;
		}

		// Handle built-in types.
		if (equal(tok, "void"))
			counter += VOID;
		else if (equal(tok, "_Bool"))
			counter += BOOL;
		else if (equal(tok, "char"))
			counter += CHAR;
		else if (equal(tok, "short"))
			counter += SHORT;
		else if (equal(tok, "int"))
			counter += INT;
		else if (equal(tok, "long"))
			counter += LONG;
		else if (equal(tok, "signed"))
			counter |= SIGNED;
		else if (equal(tok, "unsigned"))
			counter |= UNSIGNED;
		else
			unreachable();

		switch (counter) {
		case VOID:
			ty = p_ty_void();
			break;
		case BOOL:
			ty = p_ty_bool();
			break;
		case CHAR:
		case SIGNED + CHAR:
			ty = p_ty_char();
			break;
		case UNSIGNED + CHAR:
			ty = p_ty_uchar();
			break;
		case SHORT:
		case SHORT + INT:
		case SIGNED + SHORT:
		case SIGNED + SHORT + INT:
			ty = p_ty_short();
			break;
		case UNSIGNED + SHORT:
		case UNSIGNED + SHORT + INT:
			ty = p_ty_ushort();
			break;
		case INT:
		case SIGNED:
		case SIGNED + INT:
			ty = p_ty_int();
			break;
		case UNSIGNED:
		case UNSIGNED + INT:
			ty = p_ty_uint();
			break;
		case LONG:
		case LONG + INT:
		case LONG + LONG:
		case LONG + LONG + INT:
		case SIGNED + LONG:
		case SIGNED + LONG + INT:
		case SIGNED + LONG + LONG:
		case SIGNED + LONG + LONG + INT:
			ty = p_ty_long();
			break;
		case UNSIGNED + LONG:
		case UNSIGNED + LONG + INT:
		case UNSIGNED + LONG + LONG:
		case UNSIGNED + LONG + LONG + INT:
			ty = p_ty_ulong();
			break;
		default:
			error_tok(tok, "invalid type");
			break;
		}

		tok = tok->next;
	}

	*rest = tok;
	return ty;
}

// func-params = ("void" | param ("," param)* ("," "...")?)? ")"
// param = declspec declarator
static struct Type *func_params(struct Token **rest, struct Token *tok, struct Type *ty)
{
	if (equal(tok, "void") && equal(tok->next, ")")) {
		*rest = tok->next->next;
		return func_type(ty);
	}

	struct Type head = {};
	struct Type *cur = &head;
	bool is_variadic = false;

	while (!equal(tok, ")")) {
		if (cur != &head)
			tok = skip(tok, ",");

		if (equal(tok, "...")) {
			is_variadic = true;
			tok = tok->next;
			skip(tok, ")");
			break;
		}

		struct Type *basety = declspec(&tok, tok, NULL);
		struct Type *ty2 = declarator(&tok, tok, basety);

		if (ty2->kind == TY_ARRAY) {
			struct Token *name = ty2->name;
			ty2 = pointer_to(ty2->base);
			ty2->name = name;
		}

		cur->next = copy_type(ty2);
		cur = cur->next;
	}

	ty = func_type(ty);
	ty->params = head.next;
	ty->is_variadic = is_variadic;

	*rest = tok->next;
	return ty;
}

// array-dimension = const-expr? "]" type-suffix
static struct Type *array_dimension(struct Token **rest, struct Token *tok,
				    struct Type *ty)
{
	if (equal(tok, "]")) {
		ty = type_suffix(rest, tok->next, ty);
		// set flag for incomplete array
		return array_of(ty, -1);
	}

	int sz = const_expr(&tok, tok);
	// skip "]"
	tok = skip(tok, "]");
	ty = type_suffix(rest, tok, ty);
	return array_of(ty, sz);
}

// type-suffix = "(" func-params
// 		| "[" array_dimension
// 		| NULL
static struct Type *type_suffix(struct Token **rest, struct Token *tok,
				struct Type *ty)
{
	// deal with function identifier
	if (equal(tok, "("))
		return func_params(rest, tok->next, ty);

	if (equal(tok, "["))
		return array_dimension(rest, tok->next, ty);

	*rest = tok;
	return ty;
}

// declarator = "*"* ("(" ident ")" | "(" declarator ")" | ident) (type-suffix)?
struct Type *declarator(struct Token **rest, struct Token *tok, struct Type *ty)
{
	while (consume(&tok, tok, "*"))
		ty = pointer_to(ty);

	if (equal(tok, "(")) {
		struct Token *start = tok->next;
		struct Type dummy = {};

		// get the token of type_suffix firstly
		declarator(&tok, start, &dummy);
		tok = skip(tok, ")");

		// get the type of declarator, and update 'rest'
		ty = type_suffix(rest, tok, ty);

		// process 'start' token, and keep the 'rest' & return it
		return declarator(&tok, start, ty);
	}

	if (tok->kind != TK_IDENT)
		error_tok(tok, "expected a variable name");

	// deal with after identifier
	ty = type_suffix(rest, tok->next, ty);
	ty->name = tok;
	return ty;
}

// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
struct Node *declaration(struct Token **rest, struct Token *tok,
			 struct Type *basety, struct VarAttr *attr)
{
	struct Node head = {}; // memset 'head' with 0
	struct Node *cur = &head;
	int i = 0;

	while (!equal(tok, ";")) {
		if (i++ > 0)
			tok = skip(tok, ",");

		struct Token *start = tok;
		struct Type *ty = declarator(&tok, tok, basety);
		if (ty->kind == TY_VOID)
			error_tok(start, "variable declared void");

		if (attr && attr->is_static) {
			// static local variable
			struct Obj *var = new_anon_gvar(ty);
			push_scope(get_ident(ty->name))->var = var;

			if (equal(tok, "="))
				gvar_initializer(&tok, tok->next, var);
			continue;
		}

		struct Obj *var = new_lvar(get_ident(ty->name), ty);
		if (attr && attr->align)
			var->align = attr->align;

		if (equal(tok, "=")) {
			// local variable initializer
			struct Node *expr = lvar_initializer(&tok, tok->next, var);
			// a series of expressions & statements
			cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
		}

		if (var->ty->size < 0)
			error_tok(ty->name, "variable has incomplete type");
		if (var->ty->kind == TY_VOID)
			error_tok(ty->name, "variable declared void");
	}

	// might empty block here
	struct Node *node = new_node(ND_BLOCK, tok);
	node->body = head.next;

	*rest = tok->next;
	return node;
}

struct Token *parse_typedef(struct Token *tok, struct Type *basety)
{
	bool first = true;

	while (!consume(&tok, tok, ";")) {
		if (!first)
			tok = skip(tok, ",");
		first = false;

		struct Type *ty = declarator(&tok, tok, basety);
		push_scope(get_ident(ty->name))->type_def = ty;
	}

	return tok;
}
