#include <toycc.h>
#include <type.h>
#include <declarator.h>
#include <initializer.h>
#include <scope.h>
#include <parser.h>

struct Node *new_cast(struct Node *expr, struct Type *ty)
{
	add_type(expr);

	struct Node *n = calloc(1, sizeof(struct Node));

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

		// Anonymous struct member
		if ((basety->kind == TY_STRUCT || basety->kind == TY_UNION) &&
		     consume(&tok, tok, ";")) {
			struct Member *mem = calloc(1, sizeof(struct Member));
			mem->ty = basety;
			mem->idx = idx++;
			mem->align = attr.align ? attr.align : mem->ty->align;
			cur = cur->next = mem;
			continue;
		}

		// Regular struct members
		while (!consume(&tok, tok, ";")) {
			if (!first)
				tok = skip(tok, ",");
			first = false;

			struct Member *mem = calloc(1, sizeof(struct Member));

			mem->ty = declarator(&tok, tok, basety);
			mem->name = mem->ty->name;
			mem->idx = idx++;
			// if _Alignas set, refer to attr.align,
			// otherwise refer to ty->align.
			mem->align = attr.align ? attr.align : mem->ty->align;

			if (consume(&tok, tok, ":")) {
				mem->is_bitfield = true;
				mem->bit_width = const_expr(&tok, tok);
			}

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

static int align_down(int n, int align)
{
	return align_to(n - align + 1, align);
}

// struct-decl = struct-union-decl
static struct Type *struct_decl(struct Token **rest, struct Token *tok)
{
	struct Type *ty = struct_union_decl(rest, tok);
	ty->kind = TY_STRUCT;

	if (ty->size < 0)
		return ty;

	// Assign offsets within the struct to members
	int bits = 0;
	for (struct Member *mem = ty->members; mem; mem = mem->next) {
		if (mem->is_bitfield) {
			if (mem->bit_width == 0) {
				// Zero-width anonymous bitfield has a special meaning.
				// It affects only alignment.
				bits = align_to(bits, mem->ty->size * 8);

			} else {
				int sz = mem->ty->size;

				// `bits` corresponds to the lowest position of the member,
				// while `(bits+ mem->bit_width - 1)` corresponds to the
				// highest position of the member.
				//
				// If the two are not equal, it indicates that the
				// remaining space of this type is not enough to save
				// and new space needs to expand.
				if (bits / (sz * 8) != (bits + mem->bit_width - 1) / (sz * 8))
					bits = align_to(bits, sz * 8);

				mem->offset = align_down(bits / 8, sz);
				mem->bit_offset = bits % (sz * 8);
				bits += mem->bit_width;
			}
		} else {
			bits = align_to(bits, mem->align * 8);
			mem->offset = bits / 8;
			bits += mem->ty->size * 8;
		}

		// align of struct is max among each member's alignment
		if (ty->align < mem->align)
			ty->align = mem->align;
	}
	ty->size = align_to(bits, ty->align * 8) / 8;

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

// pointers = ("*" ("const" | "volatile" | "restrict")*)*
static struct Type *pointers(struct Token **rest, struct Token *tok,
			     struct Type *ty)
{
	while (consume(&tok, tok, "*")) {
		ty = pointer_to(ty);

		while (equal(tok, "const") ||
		       equal(tok, "volatile") ||
		       equal(tok, "restrict") ||
		       equal(tok, "__restrict") ||
		       equal(tok, "__restrict__")) {
			// ignore
			tok = tok->next;
		}
	}

	*rest = tok;
	return ty;
}

static struct Type *type_suffix(struct Token **rest, struct Token *tok,
				struct Type *ty);
// abstract_declarator = pointers ("(" abstract-declarator ")")? type-suffix
static struct Type *abstract_declarator(struct Token **rest, struct Token *tok, struct Type *ty)
{
	// like "sizeof(char *)"
	ty = pointers(&tok, tok, ty);

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
		"const",
		"volatile",
		"auto",
		"register",
		"restrict",
		"__restrict",
		"__restrict__",
		"_Noreturn",
		"float",
		"double",
		"typeof",
		"inline",
		"_Thread_local",
		"__thread",
	};

	for (size_t i = 0; i < ARRAY_SIZE(kw); i++)
		if (equal(tok, kw[i]))
			return true;

	return find_typedef(tok);
}

// typeof-specifier = "(" (expr | typename) ")"
static struct Type *typeof_specifier(struct Token **rest, struct Token *tok)
{
	tok = skip(tok, "(");

	struct Type *ty;
	if (is_typename(tok)) {
		ty = typename(&tok, tok);
	} else {
		struct Node *node = expr(&tok, tok);
		add_type(node);
		ty = node->ty;
	}

	*rest = skip(tok, ")");
	return ty;
}

// declspec = ("void" | "_Bool" | "char" | "short" | "int" | "long" |
//		"typedef" | "static" | "extern" | "inline" |
//		"_Thread_local" | "__thread" |
//		"signed" | "unsigned" |
//		struct-decl | union-decl |
//		typedef-name |
//		enum-specifier | typeof-specifier |
//		"const" | "volatile" | "auto" | "register" |
//		"restrict" | "__restrict" | "__restrict__" |
//		"_Noreturn")+
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
		FLOAT    = 1 << 12,
		DOUBLE   = 1 << 14,
		OTHER    = 1 << 16,
		SIGNED   = 1 << 17,
		UNSIGNED = 1 << 18,
	};

	// "typedef t" means "typedef int t"
	struct Type *ty = p_ty_int();
	int counter = 0;

	while (is_typename(tok)) {
		// handle "typedef" keyword or handle storage class specifiers
		if (equal(tok, "typedef") || equal(tok, "static") ||
		    equal(tok, "extern") || equal(tok, "inline") ||
		    equal(tok, "_Thread_local") || equal(tok, "__thread")) {
			if (!attr)
				error_tok(tok, "storage class specifier is not allowed in this context");

			if (equal(tok, "typedef"))
				attr->is_typedef = true;
			else if (equal(tok, "static"))
				attr->is_static = true;
			else if (equal(tok, "extern"))
				attr->is_extern = true;
			else if (equal(tok, "inline"))
				attr->is_inline = true;
			else
				attr->is_tls = true;

			if (attr->is_typedef &&
			    attr->is_static + attr->is_extern + attr->is_inline + attr->is_tls > 1)
				error_tok(tok, "typedef may not be used together with "
					       "static, extern, inline, __thread or _Thread_local");

			tok = tok->next;
			continue;
		}

		// These keywords are recognized but ignored
		if (consume(&tok, tok, "const") ||
		    consume(&tok, tok, "volatile") ||
		    consume(&tok, tok, "auto") ||
		    consume(&tok, tok, "register") ||
		    consume(&tok, tok, "restrict") ||
		    consume(&tok, tok, "__restrict") ||
		    consume(&tok, tok, "__restrict__") ||
		    consume(&tok, tok, "_Noreturn"))
			continue;

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
		if (equal(tok, "struct") ||
		    equal(tok, "union") ||
		    equal(tok, "enum") ||
		    equal(tok, "typeof") || ty2) {
			if (counter)
				break;

			if (equal(tok, "struct")) {
				ty = struct_decl(&tok, tok->next);

			} else if (equal(tok, "union")) {
				ty = union_decl(&tok, tok->next);

			} else if (equal(tok, "enum")) {
				ty = enum_specifier(&tok, tok->next);

			} else if (equal(tok, "typeof")) {
				ty = typeof_specifier(&tok, tok->next);

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
		else if (equal(tok, "float"))
			counter += FLOAT;
		else if (equal(tok, "double"))
			counter += DOUBLE;
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
		case FLOAT:
			ty = p_ty_float();
			break;
		case DOUBLE:
			ty = p_ty_double();
			break;
		case LONG + DOUBLE:
			ty = p_ty_ldouble();
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

		struct Token *name = ty2->name;

		if (ty2->kind == TY_ARRAY) {
			// "array of T" is converted to "pointer to T" only
			//  in the parameter context.
			// For example, *argv[] is converted to **argv by this.
			ty2 = pointer_to(ty2->base);
			ty2->name = name;

		} else if (ty2->kind == TY_FUNC) {
			// Likewise, a function is converted to a pointer to
			// a function only in the parameter context.
			ty2 = pointer_to(ty2);
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

static bool is_const_expr(struct Node *node)
{
	add_type(node);

	switch (node->kind) {
	case ND_ADD:
	case ND_SUB:
	case ND_MUL:
	case ND_DIV:
	case ND_BITAND:
	case ND_BITOR:
	case ND_BITXOR:
	case ND_SHL:
	case ND_SHR:
	case ND_EQ:
	case ND_NE:
	case ND_LT:
	case ND_LE:
	case ND_LOGAND:
	case ND_LOGOR:
		return is_const_expr(node->lhs) && is_const_expr(node->rhs);

	case ND_COND:
		if (!is_const_expr(node->cond))
			return false;

		return is_const_expr(eval(node->cond) ? node->then : node->els);

	case ND_COMMA:
		return is_const_expr(node->rhs);

	case ND_NEG:
	case ND_NOT:
	case ND_BITNOT:
	case ND_CAST:
		return is_const_expr(node->lhs);

	case ND_NUM:
		return true;

	default:
		break;
	}

	return false;
}

// array-dimension = ("static" | "restrict")* const-expr? "]" type-suffix
static struct Type *array_dimension(struct Token **rest, struct Token *tok,
				    struct Type *ty)
{
	while (equal(tok, "static") || equal(tok, "restrict"))
		tok = tok->next;

	if (equal(tok, "]")) {
		ty = type_suffix(rest, tok->next, ty);
		// set flag for incomplete array
		return array_of(ty, -1);
	}

	// allows conditional expression
	struct Node *expr = conditional(&tok, tok);

	// skip "]"
	tok = skip(tok, "]");
	ty = type_suffix(rest, tok, ty);

	if (ty->kind == TY_VLA || !is_const_expr(expr))
		return vla_of(ty, expr);

	return array_of(ty, eval(expr));
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
	ty = pointers(&tok, tok, ty);

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

	struct Token *name = NULL;
	struct Token *name_pos = tok;

	if (tok->kind == TK_IDENT) {
		name = tok;
		tok = tok->next;
	}

	// deal with after identifier
	ty = type_suffix(rest, tok, ty);
	ty->name = name;
	ty->name_pos = name_pos;

	return ty;
}

static struct Obj *builtin_alloca;

// Generate code for computing a VLA size.
struct Node *compute_vla_size(struct Type *ty, struct Token *tok)
{
	struct Node *node = new_node(ND_NULL_EXPR, tok);
	if (ty->base)
		node = new_binary(ND_COMMA, node,
				compute_vla_size(ty->base, tok), tok);

	if (ty->kind != TY_VLA)
		return node;

	struct Node *base_sz;
	if (ty->base->kind == TY_VLA)
		base_sz = new_var_node(ty->base->vla_size, tok);
	else
		base_sz = new_num(ty->base->size, tok);

	ty->vla_size = new_lvar("", p_ty_ulong());
	struct Node *expr = new_binary(ND_ASSIGN,
					new_var_node(ty->vla_size, tok),
					new_binary(ND_MUL, ty->vla_len, base_sz, tok),
					tok);
	return new_binary(ND_COMMA, node, expr, tok);
}

static struct Node *new_alloca(struct Node *sz)
{
	struct Node *node = new_unary(ND_FUNCALL,
				      new_var_node(builtin_alloca, sz->tok),
				      sz->tok);
	node->func_ty = builtin_alloca->ty;
	node->ty = builtin_alloca->ty->return_ty;
	node->args = sz;
	add_type(sz);
	return node;
}

static struct Node *new_vla_ptr(struct Obj *var, struct Token *tok)
{
	struct Node *node = new_node(ND_VLA_PTR, tok);
	node->var = var;
	return node;
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

		if (!ty->name)
			error_tok(ty->name_pos, "variable name omitted");

		if (attr && attr->is_static) {
			// static local variable
			struct Obj *var = new_anon_gvar(ty);
			push_scope(get_ident(ty->name))->var = var;

			if (equal(tok, "="))
				gvar_initializer(&tok, tok->next, var);
			continue;
		}

		// Generate code for computing a VLA size.
		// We need to do this even if ty is not VLA
		// because ty may be a pointer to VLA.
		// (e.g. int (*foo)[n][m] where n and m are variables.)
		cur = cur->next =
			new_unary(ND_EXPR_STMT, compute_vla_size(ty, tok), tok);

		if (ty->kind == TY_VLA) {
			if (equal(tok, "="))
				error_tok(tok, "variable-sized object may not be initialized");

			// Variable length arrays (VLAs) are translated to alloca() calls.
			// For example, `int x[n+2]` is translated to
			// `tmp = n + 2, x = alloca(tmp)`.
			struct Obj *var = new_lvar(get_ident(ty->name), ty);
			struct Token *tok = ty->name;
			// x = alloca(n + 2)
			struct Node *expr = new_binary(ND_ASSIGN, new_vla_ptr(var, tok),
							new_alloca(new_var_node(ty->vla_size, tok)),
							tok);

			cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
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
		if (!ty->name)
			error_tok(ty->name_pos, "typedef name omitted");

		push_scope(get_ident(ty->name))->type_def = ty;
	}

	return tok;
}

void declare_builtin_functions(void)
{
	// declare: void *alloca(int a)
	struct Type *ty = func_type(pointer_to(p_ty_void()));
	ty->params = copy_type(p_ty_int());

	builtin_alloca = new_gvar("alloca", ty);
	builtin_alloca->is_definition = false;
}
