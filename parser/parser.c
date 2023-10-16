// This file contains a recursive descent parser for C.
//
// Most functions in this file are named after the symbols they are
// supposed to read from an input token list. For example, stmt() is
// responsible for reading a statement from a token list. The function
// then construct an AST node representing a statement.
//
// Each function conceptually returns two values, an AST node and
// remaining part of the input tokens. Since C doesn't support
// multiple return values, the remaining tokens are returned to the
// caller via a pointer argument.

#include <toycc.h>
#include <initializer.h>
#include <declarator.h>
#include <parser.h>
#include <scope.h>
#include <type.h>

// Points to the function object the parser is currently parsing.
static struct Obj *current_fn;

// funcall = (assign ("," assign)*)? ")"
static struct Node *funcall(struct Token **rest, struct Token *tok, struct Node *fn)
{
	add_type(fn);

	if (fn->ty->kind != TY_FUNC &&
		(fn->ty->kind != TY_PTR || fn->ty->base->kind != TY_FUNC))
		error_tok(fn->tok, "not a function");

	struct Type *ty = (fn->ty->kind == TY_FUNC) ? fn->ty : fn->ty->base;
	struct Type *param_ty = ty->params;

	struct Node head = {};
	struct Node *cur = &head;

	while (!equal(tok, ")")) {
		if (cur != &head)
			tok = skip(tok, ",");

		struct Node *arg = assign(&tok, tok);
		add_type(arg);

		if (!param_ty && !ty->is_variadic)
			error_tok(tok, "too many arguments");

		if (param_ty) {
			if (param_ty->kind != TY_STRUCT && param_ty->kind != TY_UNION)
				arg = new_cast(arg, param_ty);
			param_ty = param_ty->next;

		} else if (arg->ty->kind == TY_FLOAT) {
			// The case: variadic args
			// If parameter type is omitted (e.g. in "..."),
			// float arguments are promoted to double.
			arg = new_cast(arg, p_ty_double());
		}

		cur->next = arg;
		cur = cur->next;
	}

	if (param_ty)
		error_tok(tok, "too few arguments");

	*rest = skip(tok, ")");

	struct Node *node = new_unary(ND_FUNCALL, fn, tok);
	node->func_ty = ty;
	node->ty = ty->return_ty;
	node->args = head.next;

	// If a function returns a struct, it is caller's responsibility
	// to allocate a space for the return value.
	if (is_struct_union(node->ty))
		node->ret_buffer = new_lvar("", node->ty);

	return node;
}

// parse AST(abstract syntax tree)
// expr -> assign -> equality -> relational -> add -> mul ->
// unary -> postfix -> primary(num -> identifier -> bracket) ->
// expr -> ...
// expr:
// 	tok: current tok pointer
// 	rest: return current tok pointer
static struct Node *expr(struct Token **rest, struct Token *tok);
static struct Node *compound_stmt(struct Token **rest, struct Token *tok);
static struct Node *unary(struct Token **rest, struct Token *tok);
// primary = "(" "{" stmt+ "}" ")"
// 	| "(" expr ")"
//	| "sizeof" "(" type-name ")"
// 	| "sizeof" unary
//	| "_Alignof" "(" type-name ")"
//	| "_Alignof" unary
// 	| ident
// 	| str
// 	| num
static struct Node *primary(struct Token **rest, struct Token *tok)
{
	struct Token *start = tok;

	if (equal(tok, "(") && equal(tok->next, "{")) {
		// This is a GNU statement expression.
		struct Node *n = new_node(ND_STMT_EXPR, tok);

		n->body = compound_stmt(&tok, tok->next->next)->body;
		*rest = skip(tok, ")");
		return n;
	}

	if (equal(tok, "(")) {
		struct Node *node = expr(&tok, tok->next);
		*rest = skip(tok, ")");
		return node;
	}

	if (equal(tok, "sizeof") && equal(tok->next, "(") && is_typename(tok->next->next)) {
		struct Type *ty = typename(&tok, tok->next->next);
		// update rest
		*rest = skip(tok, ")");
		return new_ulong(ty->size, start);
	}

	if (equal(tok, "sizeof")) {
		// update rest pointer
		struct Node *node = unary(rest, tok->next);

		add_type(node);
		return new_ulong(node->ty->size, tok);
	}

	if (equal(tok, "_Alignof") && equal(tok->next, "(") &&
	    is_typename(tok->next->next)) {

		struct Type *ty = typename(&tok, tok->next->next);
		*rest = skip(tok, ")");
		return new_ulong(ty->align, tok);
	}

	if (equal(tok, "_Alignof")) {
		struct Node *node = unary(rest, tok->next);
		add_type(node);

		return new_ulong(node->ty->align, tok);
	}

	if (tok->kind == TK_IDENT) {
		// variable or enum constant
		struct VarScope *sc = find_var(tok);
		*rest = tok->next;

		if (sc) {
			if (sc->var)
				// variable
				return new_var_node(sc->var, tok);
			if (sc->enum_ty)
				// enum constant
				return new_num(sc->enum_val, tok);
		}

		if (equal(tok->next, "("))
			error_tok(tok, "implicit declaration of a function");
		error_tok(tok, "undefined variable");
	}

	if (tok->kind == TK_STR) {
		struct Obj *var = new_string_literal(tok->str, tok->ty);
		*rest = tok->next;
		return new_var_node(var, tok);
	}

	if (tok->kind == TK_NUM) {
		struct Node *node;

		if (is_float(tok->ty))
			node = new_float(tok);
		else
			node = new_num(tok->val, tok);

		node->ty = tok->ty;
		*rest = tok->next;
		return node;
	}

	error_tok(tok, "expected an expression");
}

// Find a struct member by name.
struct Member *get_struct_member(struct Type *ty, struct Token *tok)
{
	for (struct Member *mem = ty->members; mem; mem = mem->next) {
		// Anonymous struct member
		if ((mem->ty->kind == TY_STRUCT || mem->ty->kind == TY_UNION) &&
		    !mem->name) {
			// Try to get struct member from the children of this member
			if (get_struct_member(mem->ty, tok))
				return mem;
			continue;
		}

		// Regular struct member
		if (mem->name->len == tok->len &&
		   !strncmp(mem->name->loc, tok->loc, tok->len))
			return mem;
	}
	return NULL;
}

// Create a node representing a struct member access, such as foo.bar
// where foo is a struct and bar is a member name.
//
// C has a feature called "anonymous struct" which allows a struct to
// have another unnamed struct as a member like this:
//
//   struct { struct { int a; }; int b; } x;
//
// The members of an anonymous struct belong to the outer struct's
// member namespace. Therefore, in the above example, you can access
// member "a" of the anonymous struct as "x.a".
//
// This function takes care of anonymous structs.
static struct Node *struct_ref(struct Node *node, struct Token *tok)
{
	add_type(node);
	struct Type *ty = node->ty;
	if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
		error_tok(node->tok, "not a struct nor a union");

	for (;;) {
		struct Member *mem = get_struct_member(ty, tok);
		if (!mem)
			error_tok(tok, "no such member");

		node = new_unary(ND_MEMBER, node, tok);
		node->member = mem;
		if (mem->name)
			break;

		// Anonymous struct member,
		// search until the named member
		ty = mem->ty;
	}

	return node;
}

// Convert op= operators to expressions containing an assignment.
//
// In general, `A op= C` is converted to ``tmp = &A, *tmp = *tmp op B`.
// However, if a given expression is of form `A.x op= C`, the input is
// converted to `tmp = &A, (*tmp).x = (*tmp).x op C` to handle assignments
// to bitfields.
static struct Node *to_assign(struct Node *binary)
{
	add_type(binary->lhs);
	add_type(binary->rhs);

	struct Token *tok = binary->tok;

	// Convert `A.x op= C` to `tmp = &A, (*tmp).x = (*tmp).x op C`.
	if (binary->lhs->kind == ND_MEMBER) {
		// var tmp
		struct Obj *var = new_lvar("", pointer_to(binary->lhs->lhs->ty));

		// tmp = &A
		struct Node *expr1 = new_binary(ND_ASSIGN, new_var_node(var, tok),
					new_unary(ND_ADDR, binary->lhs->lhs, tok), tok);

		// (*tmp).x
		struct Node *expr2 = new_unary(ND_MEMBER,
					new_unary(ND_DEREF, new_var_node(var, tok), tok),
					tok);
		expr2->member = binary->lhs->member;

		// the same as expr2
		struct Node *expr3 = new_unary(ND_MEMBER,
					new_unary(ND_DEREF, new_var_node(var, tok), tok),
					tok);
		expr3->member = binary->lhs->member;

		// expr2 = expr3 op C
		struct Node *expr4 = new_binary(ND_ASSIGN, expr2,
					new_binary(binary->kind, expr3, binary->rhs, tok),
					tok);

		// expr1, expr4
		return new_binary(ND_COMMA, expr1, expr4, tok);
	}

	// Convert `A op= C` to `tmp = &A, *tmp = *tmp op B`.
	// var tmp
	struct Obj *var = new_lvar("", pointer_to(binary->lhs->ty));
	// tmp = &A
	struct Node *expr1 = new_binary(ND_ASSIGN, new_var_node(var, tok),
					new_unary(ND_ADDR, binary->lhs, tok),
					tok);
	// *tmp = *tmp op B
	struct Node *expr2 = new_binary(ND_ASSIGN,
					new_unary(ND_DEREF, new_var_node(var, tok), tok),
					new_binary(binary->kind,
						   new_unary(ND_DEREF, new_var_node(var, tok), tok),
						   binary->rhs, tok),
					tok);

	return new_binary(ND_COMMA, expr1, expr2, tok);
}

// Convert A++ to `(typeof A)((A += 1) - 1)`
static struct Node *new_inc_dec(struct Node *node, struct Token *tok, int addend)
{
	add_type(node);

	struct Node *node_add = new_add(node, new_num(addend, tok), tok);
	struct Node *node_to_assign = to_assign(node_add);
	struct Node *node_minus = new_add(node_to_assign, new_num(-addend, tok), tok);

	return new_cast(node_minus, node->ty);
}

// postfix = "(" type-name ")" "{" initializer-list "}"
//         | ident "(" func-args ")" postfix-tail*
//         | primary postfix-tail*
//
// postfix-tail = "[" expr "]"
//              | "(" func-args ")"
//              | "." ident
//              | "->" ident
//              | "++"
//              | "--"
static struct Node *postfix(struct Token **rest, struct Token *tok)
{
	if (equal(tok, "(") && is_typename(tok->next)) {
		// Compound literal
		struct Token *start = tok;
		struct Type *ty = typename(&tok, tok->next);
		tok = skip(tok, ")");

		if (is_global_scope()) {
			// never enter scope, so allocate global var
			struct Obj *var = new_anon_gvar(ty);
			gvar_initializer(rest, tok, var);
			return new_var_node(var, start);
		}

		struct Obj *var = new_lvar("", ty);
		struct Node *lhs = lvar_initializer(rest, tok, var);
		// refer to var self under dereference case
		struct Node *rhs = new_var_node(var, tok);
		return new_binary(ND_COMMA, lhs, rhs, start);
	}

	struct Node *node = primary(&tok, tok);

	while (1) {
		if (equal(tok, "(")) {
			node = funcall(&tok, tok->next, node);
			continue;
		}

		if (equal(tok, "[")) {
			// x[y] is short for *(x+y)
			struct Token *start = tok;
			struct Node *idx = expr(&tok, tok->next);

			tok = skip(tok, "]");
			node = new_unary(ND_DEREF, new_add(node, idx, start), start);
			continue;
		}

		if (equal(tok, ".")) {
			node = struct_ref(node, tok->next);
			// skip ident
			tok = tok->next->next;
			continue;
		}

		if (equal(tok, "->")) {
			// x->y is short for (*x).y
			node = new_unary(ND_DEREF, node, tok);
			node = struct_ref(node, tok->next);
			// skip member
			tok = tok->next->next;
			continue;
		}

		if (equal(tok, "++")) {
			node = new_inc_dec(node, tok, 1);
			tok = tok->next;
			continue;
		}

		if (equal(tok, "--")) {
			node = new_inc_dec(node, tok, -1);
			tok = tok->next;
			continue;
		}

		break;
	}

	*rest = tok;
	return node;
}

// cast = "(" type-name ")" cast | unary
static struct Node *cast(struct Token **rest, struct Token *tok)
{
	if (equal(tok, "(") && is_typename(tok->next)) {
		struct Token *start = tok;
		struct Type *ty = typename(&tok, tok->next);
		tok = skip(tok, ")");

		// compound literal
		if (equal(tok, "{"))
			return unary(rest, start);

		// type cast
		struct Node *n = new_cast(cast(rest, tok), ty);
		n->tok = start;
		return n;
	}

	return unary(rest, tok);
}

// mul = cast ("*" cast | "/" cast | "%" cast)*
static struct Node *mul(struct Token **rest, struct Token *tok)
{
	struct Node *node = cast(&tok, tok);

	while (1) {
		struct Token *start = tok;

		if (equal(tok, "*")) {
			node = new_binary(ND_MUL, node, cast(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, "/")) {
			node = new_binary(ND_DIV, node, cast(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, "%")) {
			node = new_binary(ND_MOD, node, cast(&tok, tok->next), start);
			continue;
		}

		*rest = tok;
		return node;
	}
}

static struct Node *new_long(int64_t val, struct Token *tok)
{
	struct Node *node = new_node(ND_NUM, tok);
	node->val = val;
	node->ty = p_ty_long();
	return node;
}

// In C, `+`/`-` operator is overloaded to perform the pointer arithmetic.
//
// If p is a pointer, p+n adds not n but sizeof((*p) * n) to the value of p,
// so that p+n points to the location n elements (not bytes) ahead of p.
// In other words, we need to scale an integer value before adding to a
// pointer value. This function takes care of the scaling.
struct Node *new_add(struct Node *lhs, struct Node *rhs, struct Token *tok)
{
	add_type(lhs);
	add_type(rhs);

	// num + num
	if (is_numeric(lhs->ty) && is_numeric(rhs->ty))
		return new_binary(ND_ADD, lhs, rhs, tok);

	// ptr + ptr
	if (lhs->ty->base && rhs->ty->base)
		error_tok(tok, "invalid operands");

	// canonicalized `num + ptr` to `ptr + num`
	if (!lhs->ty->base && rhs->ty->base) {
		struct Node *tmp = lhs;
		lhs = rhs;
		rhs = tmp;
	}

	// ptr + num
	rhs = new_binary(ND_MUL, rhs, new_long(lhs->ty->base->size, tok), tok);
	return new_binary(ND_ADD, lhs, rhs, tok);
}

static struct Node *new_sub(struct Node *lhs, struct Node *rhs, struct Token *tok)
{
	add_type(lhs);
	add_type(rhs);

	// num - num
	if (is_numeric(lhs->ty) && is_numeric(lhs->ty))
		return new_binary(ND_SUB, lhs, rhs, tok);

	// ptr - ptr, which returns how many elements are between the two.
	if (lhs->ty->base && rhs->ty->base) {
		struct Node *node = new_binary(ND_SUB, lhs, rhs, tok);
		node->ty = p_ty_long();
		return new_binary(ND_DIV, node, new_num(lhs->ty->base->size, tok), tok);
	}

	// ptr - num
	if (lhs->ty->base && is_integer(rhs->ty)) {
		rhs = new_binary(ND_MUL, rhs, new_long(lhs->ty->base->size, tok), tok);
		return new_binary(ND_SUB, lhs, rhs, tok);
	}

	// num - ptr
	error_tok(tok, "invalid operands");
}

static struct Node *add(struct Token **rest, struct Token *tok)
{
	struct Node *node = mul(&tok, tok);

	while (1) {
		struct Token *start = tok;

		if (equal(tok, "+")) {
			node = new_add(node, mul(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, "-")) {
			node = new_sub(node, mul(&tok, tok->next), start);
			continue;
		}

		*rest = tok;
		return node;
	}
}

// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
// 	 | ("++" | "--") unary
// 	 | postfix
static struct Node *unary(struct Token **rest, struct Token *tok)
{
	if (equal(tok, "+"))
		// ignore
		return cast(rest, tok->next);

	if (equal(tok, "-"))
		// depth--
		return new_unary(ND_NEG, cast(rest, tok->next), tok);

	if (equal(tok, "&")) {
		struct Node *lhs = cast(rest, tok->next);

		if (lhs->kind == ND_MEMBER && lhs->member->is_bitfield)
			error_tok(tok, "cannot take address of bitfield");

		return new_unary(ND_ADDR, lhs, tok);
	}

	if (equal(tok, "*")) {
		struct Node *node = cast(rest, tok->next);

		add_type(node);
		// [https://www.sigbus.info/n1570#6.5.3.2p4]
		// This is an oddity in the C spec, but dereferencing
		// a function shouldn't do anything. If foo is a function,
		// `*foo`, `**foo` or `*****foo` are all equivalent to
		// just `foo`.
		if (node->ty->kind == TY_FUNC)
			return node;

		return new_unary(ND_DEREF, node, tok);
	}

	if (equal(tok, "!"))
		return new_unary(ND_NOT, cast(rest, tok->next), tok);

	if (equal(tok, "~"))
		return new_unary(ND_BITNOT, cast(rest, tok->next), tok);

	// read ++i as i+=1
	if (equal(tok, "++"))
		return to_assign(new_add(unary(rest, tok->next),
					 new_num(1, tok), tok));

	// read --i as i-=1
	if (equal(tok, "--"))
		return to_assign(new_sub(unary(rest, tok->next),
					 new_num(1, tok), tok));

	return postfix(rest, tok);
}

// shift = add ("<<" add | ">>" add)*
static struct Node *shift(struct Token **rest, struct Token *tok)
{
	struct Node *node = add(&tok, tok);

	while (1) {
		struct Token *start = tok;

		if (equal(tok, "<<")) {
			node = new_binary(ND_SHL, node, add(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, ">>")) {
			node = new_binary(ND_SHR, node, add(&tok, tok->next), start);
			continue;
		}

		*rest = tok;
		return node;
	}
}

// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static struct Node *relational(struct Token **rest, struct Token *tok)
{
	struct Node *node = shift(&tok, tok);

	while (1) {
		struct Token *start = tok;

		if (equal(tok, "<")) {
			node = new_binary(ND_LT, node, shift(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, "<=")) {
			node = new_binary(ND_LE, node, shift(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, ">")) {
			node = new_binary(ND_LT, shift(&tok, tok->next), node, start);
			continue;
		}

		if (equal(tok, ">=")) {
			node = new_binary(ND_LE, shift(&tok, tok->next), node, start);
			continue;
		}

		*rest = tok;
		return node;
	}
}

static struct Node *equality(struct Token **rest, struct Token *tok)
{
	struct Node *node = relational(&tok, tok);

	while (1) {
		struct Token *start = tok;

		if (equal(tok, "==")) {
			node = new_binary(ND_EQ, node, relational(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, "!=")) {
			node = new_binary(ND_NE, node, relational(&tok, tok->next), start);
			continue;
		}

		*rest = tok;
		return node;
	}
}

// bitand = equality ("&" equality)*
static struct Node *bitand(struct Token **rest, struct Token *tok)
{
	struct Node *node = equality(&tok, tok);

	while (equal(tok, "&")) {
		struct Token *start = tok;
		node = new_binary(ND_BITAND, node, equality(&tok, tok->next), start);
	}

	*rest = tok;
	return node;
}

// bitxor = bitand ("&" bitand)*
static struct Node *bitxor(struct Token **rest, struct Token *tok)
{
	struct Node *node = bitand(&tok, tok);

	while (equal(tok, "^")) {
		struct Token *start = tok;
		node = new_binary(ND_BITXOR, node, bitand(&tok, tok->next), start);
	}

	*rest = tok;
	return node;
}

// bitor = bitxor ("&" bitxor)*
static struct Node *bitor(struct Token **rest, struct Token *tok)
{
	struct Node *node = bitxor(&tok, tok);

	while (equal(tok, "|")) {
		struct Token *start = tok;
		node = new_binary(ND_BITOR, node, bitxor(&tok, tok->next), start);
	}

	*rest = tok;
	return node;
}

// logand = bitor ("&&" bitor)*
static struct Node *logand(struct Token **rest, struct Token *tok)
{
	struct Node *node = bitor(&tok, tok);
	while (equal(tok, "&&")) {
		struct Token *start = tok;
		node = new_binary(ND_LOGAND, node, bitor(&tok, tok->next), start);
	}
	*rest = tok;
	return node;
}

// logor = logand ("||" logand)*
static struct Node *logor(struct Token **rest, struct Token *tok)
{
	struct Node *node = logand(&tok, tok);
	while (equal(tok, "||")) {
		struct Token *start = tok;
		node = new_binary(ND_LOGOR, node, logand(&tok, tok->next), start);
	}
	*rest = tok;
	return node;
}

// conditional = logor ("?" expr ":" conditional)?
static struct Node *conditional(struct Token **rest, struct Token *tok)
{
	struct Node *cond = logor(&tok, tok);

	if (!equal(tok, "?")) {
		*rest = tok;
		return cond;
	}

	struct Node *node = new_node(ND_COND, tok);
	node->cond = cond;
	node->then = expr(&tok, tok->next);

	tok = skip(tok, ":");
	node->els = conditional(rest, tok);
	return node;
}

int64_t const_expr(struct Token **rest, struct Token *tok)
{
	struct Node *n = conditional(rest, tok);
	return eval(n);
}

// assign    = conditional (assign-op assign)?
// assign-op = "=" | "+=" | "-=" | "*=" | "/=" | "%="
//	     | "&=" | "|=" | "^=" | "<<=" | ">>="
struct Node *assign(struct Token **rest, struct Token *tok)
{
	struct Node *node = conditional(&tok, tok);

	if (equal(tok, "="))
		return new_binary(ND_ASSIGN, node, assign(rest, tok->next), tok);

	if (equal(tok, "+="))
		return to_assign(new_add(node, assign(rest, tok->next), tok));

	if (equal(tok, "-="))
		return to_assign(new_sub(node, assign(rest, tok->next), tok));

	if (equal(tok, "*="))
		return to_assign(new_binary(ND_MUL, node, assign(rest, tok->next), tok));

	if (equal(tok, "/="))
		return to_assign(new_binary(ND_DIV, node, assign(rest, tok->next), tok));

	if (equal(tok, "%="))
		return to_assign(new_binary(ND_MOD, node, assign(rest, tok->next), tok));

	if (equal(tok, "&="))
		return to_assign(new_binary(ND_BITAND, node, assign(rest, tok->next), tok));

	if (equal(tok, "|="))
		return to_assign(new_binary(ND_BITOR, node, assign(rest, tok->next), tok));

	if (equal(tok, "^="))
		return to_assign(new_binary(ND_BITXOR, node, assign(rest, tok->next), tok));

	if (equal(tok, "<<="))
		return to_assign(new_binary(ND_SHL, node, assign(rest, tok->next), tok));

	if (equal(tok, ">>="))
		return to_assign(new_binary(ND_SHR, node, assign(rest, tok->next), tok));

	*rest = tok;
	return node;
}

// expr = assign ("," expr)?
static struct Node *expr(struct Token **rest, struct Token *tok)
{
	struct Node *n = assign(&tok, tok);

	if (equal(tok, ","))
		return new_binary(ND_COMMA, n, expr(rest, tok->next), tok);

	*rest = tok;
	return n;
}

// expr_stmt = ";" | expr ";"
static struct Node *expr_stmt(struct Token **rest, struct Token *tok)
{
	if (equal(tok, ";")) {
		*rest = tok->next;
		// generate NULL block
		return new_node(ND_BLOCK, tok);
	}

	struct Node *node = new_node(ND_EXPR_STMT, tok);
	node->lhs = expr(&tok, tok);

	*rest = skip(tok, ";");
	return node;
}

const char *get_ident(struct Token *tok)
{
	if (tok->kind != TK_IDENT)
		error_tok(tok, "expected an identifier");
	return strndup(tok->loc, tok->len);
}

// Lists of all goto
static struct Node *gotos;
static struct Node *labels;

// current "goto" jump target
static const char *brk_label;
// current "continue" jump target
static const char *cont_label;

// Points to a node representing a switch if we are parsing
// a switch statement. Otherwise, NULL.
static struct Node *current_switch;

// stmt = "return" expr? ";"
// 	| "if" "(" expr ")" stmt ("else" stmt)?
//	| "switch" "(" expr ")" stmt
//	| "case" const-expr ":" stmt
//	| "default" ":" stmt
// 	| "for" "(" expr-stmt expr? ";" expr? ")" stmt
// 	| "while" "(" expr ")" stmt
// 	| "do" stmt "while" "(" expr ")" ";"
// 	| "goto" ident ";"
// 	| "break" ";"
// 	| "continue" ";"
// 	| ident ":" stmt
// 	| "{" compound-stmt
// 	| expr_stmt
static struct Node *stmt(struct Token **rest, struct Token *tok)
{
	if (equal(tok, "return")) {
		struct Node *node = new_node(ND_RETURN, tok);
		if (consume(rest, tok->next, ";"))
			return node;

		struct Node *exp = expr(&tok, tok->next);
		*rest = skip(tok, ";");

		add_type(exp);
		struct Type *ty = current_fn->ty->return_ty;
		if (!is_struct_union(ty))
			exp = new_cast(exp, current_fn->ty->return_ty);

		node->lhs = exp;
		return node;
	}

	if (equal(tok, "if")) {
		struct Node *n = new_node(ND_IF, tok);

		tok = skip(tok->next, "(");
		n->cond = expr(&tok, tok);

		tok = skip(tok, ")");
		n->then = stmt(&tok, tok);

		if (equal(tok, "else"))
			n->els = stmt(&tok, tok->next);

		*rest = tok;
		return n;
	}

	if (equal(tok, "switch")) {
		struct Node *n = new_node(ND_SWITCH, tok);

		tok = skip(tok->next, "(");
		n->cond = expr(&tok, tok);
		tok = skip(tok, ")");

		struct Node *sw_prev = current_switch;
		current_switch = n;

		const char *brk_prev = brk_label;
		brk_label = n->brk_label = new_unique_name();

		// a series of "case"s
		n->then = stmt(rest, tok);

		current_switch = sw_prev;
		brk_label = brk_prev;
		return n;
	}

	if (equal(tok, "case")) {
		if (!current_switch)
			error_tok(tok, "stray case");

		struct Node *n = new_node(ND_CASE, tok);
		int val = const_expr(&tok, tok->next);
		tok = skip(tok, ":");

		n->label = new_unique_name();
		n->lhs = stmt(rest, tok);
		n->val = val;
		n->case_next = current_switch->case_next;
		current_switch->case_next = n;
		return n;
	}

	if (equal(tok, "default")) {
		if (!current_switch)
			error_tok(tok, "stray default");

		struct Node *n = new_node(ND_CASE, tok);

		tok = skip(tok->next, ":");
		n->label = new_unique_name();
		n->lhs = stmt(rest, tok);
		current_switch->default_case = n;
		return n;
	}

	if (equal(tok, "for")) {
		struct Node *n = new_node(ND_FOR, tok);

		tok = skip(tok->next, "(");

		enter_scope();

		const char *brk_prev = brk_label;
		const char *cont_prev = cont_label;
		brk_label = n->brk_label = new_unique_name();
		cont_label = n->cont_label = new_unique_name();

		if (is_typename(tok)) {
			struct Type *basety = declspec(&tok, tok, NULL);
			n->init = declaration(&tok, tok, basety, NULL);
		} else {
			n->init = expr_stmt(&tok, tok);
		}

		if (!equal(tok, ";"))
			n->cond = expr(&tok, tok);
		tok = skip(tok, ";");

		if (!equal(tok, ")"))
			n->inc = expr(&tok, tok);
		tok = skip(tok, ")");

		n->then = stmt(rest, tok);

		leave_scope();

		brk_label = brk_prev;
		cont_label = cont_prev;
		return n;
	}

	if (equal(tok, "while")) {
		struct Node *n = new_node(ND_FOR, tok);

		tok = skip(tok->next, "(");
		n->cond = expr(&tok, tok);

		tok = skip(tok, ")");

		// save
		const char *brk_tmp = brk_label;
		const char *cont_tmp = cont_label;

		brk_label = n->brk_label = new_unique_name();
		cont_label = n->cont_label = new_unique_name();

		n->then = stmt(rest, tok);

		// resume
		brk_label = brk_tmp;
		cont_label = cont_tmp;
		return n;
	}

	if (equal(tok, "do")) {
		struct Node *node = new_node(ND_DO, tok);

		// save
		const char *brk_tmp = brk_label;
		const char *cont_tmp = cont_label;

		brk_label = node->brk_label = new_unique_name();
		cont_label = node->cont_label = new_unique_name();

		node->then = stmt(&tok, tok->next);

		// resume
		brk_label = brk_tmp;
		cont_label = cont_tmp;

		tok = skip(tok, "while");
		tok = skip(tok, "(");
		node->cond = expr(&tok, tok);
		tok = skip(tok, ")");
		*rest = skip(tok, ";");

		return node;
	}

	if (equal(tok, "goto")) {
		struct Node *node = new_node(ND_GOTO, tok);

		node->label = get_ident(tok->next);
		node->goto_next = gotos;
		gotos = node;
		*rest = skip(tok->next->next, ";");
		return node;
	}

	if (equal(tok, "break")) {
		if (!brk_label)
			error_tok(tok, "stray break");

		struct Node *node = new_node(ND_GOTO, tok);
		node->unique_label = brk_label;
		*rest = skip(tok->next, ";");
		return node;
	}

	if (equal(tok, "continue")) {
		if (!cont_label)
			error_tok(tok, "stray continue");

		struct Node *node = new_node(ND_GOTO, tok);
		node->unique_label = cont_label;
		*rest = skip(tok->next, ";");
		return node;
	}

	if (tok->kind == TK_IDENT && equal(tok->next, ":")) {
		struct Node *node = new_node(ND_LABEL, tok);

		node->label = strndup(tok->loc, tok->len);
		node->unique_label = new_unique_name();
		// skip ":"
		node->lhs = stmt(rest, tok->next->next);
		node->goto_next = labels;
		labels = node;
		return node;
	}

	if (equal(tok, "{"))
		return compound_stmt(rest, tok->next);

	return expr_stmt(rest, tok);
}

static bool is_function(struct Token *tok)
{
	if (equal(tok, ";"))
		return false;

	struct Type dummy = {};
	struct Type *ty = declarator(&tok, tok, &dummy);

	return ty->kind == TY_FUNC;
}

static void create_param_lvars(struct Type *param)
{
	if (param) {
		create_param_lvars(param->next);
		if (!param->name)
			error_tok(param->name_pos, "parameter name omitted");

		// locals -> arg1 -> arg2 -> ... -> argn
		new_lvar(get_ident(param->name), param);
	}
}

// This function matches gotos with labels.
//
// We cannot resolve gotos as we parse a function because gotos
// can refer a label that appears later in the function.
// So, we need to do this after we parse the entire function.
static void resolve_goto_labels(void)
{
	for (struct Node *x = gotos; x; x = x->goto_next) {
		for (struct Node *y = labels; y; y = y->goto_next) {
			if (!strcmp(x->label, y->label)) {
				x->unique_label = y->unique_label;
				break;
			}
		}

		if (!x->unique_label)
			error_tok(x->tok->next, "use of undeclared label");
	}

	// scope of function
	gotos = labels = NULL;
}

static struct Token *function(struct Token *tok, struct Type *basety,
			      const struct VarAttr *attr)
{
	struct Type *ty = declarator(&tok, tok, basety);
	if (!ty->name)
		error_tok(ty->name_pos, "function name omitted");

	struct Obj *fn = new_gvar(get_ident(ty->name), ty);
	fn->is_function = true;
	fn->is_definition = !consume(&tok, tok, ";");
	fn->is_static = attr->is_static;

	if (!fn->is_definition)
		return tok;

	current_fn = fn;

	// initialize local variables list
	init_locals();
	enter_scope();

	create_param_lvars(ty->params);
	// A buffer for a struct/union return value is passed
	// as the hidden first parameter.
	struct Type *rty = ty->return_ty;
	if (is_struct_union(rty) && rty->size > 2 * (int)sizeof(long))
		new_lvar("", pointer_to(rty));

	fn->params = ret_locals();
	if (ty->is_variadic)
		fn->va_area =
			new_lvar("__va_area__", array_of(p_ty_char(), 0));

	tok = skip(tok, "{");

	// "__func__" is automatically defined as a local variable
	// containing the current function name.
	// [https://www.sigbus.info/n1570#6.4.2.2p1]
	push_scope("__func__")->var =
		new_string_literal(fn->name, array_of(p_ty_char(), strlen(fn->name) + 1));

	// [GNU] __FUNCTION__ is yet another name of __func__.
	push_scope("__FUNCTION__")->var =
		new_string_literal(fn->name, array_of(p_ty_char(), strlen(fn->name) + 1));

	fn->body = compound_stmt(&tok, tok);
	fn->locals = ret_locals();
	leave_scope();

	resolve_goto_labels();
	return tok;
}

static struct Token *global_variable(struct Token *tok, struct Type *basety,
				     struct VarAttr *attr)
{
	bool first = true;

	while (!consume(&tok, tok, ";")) {
		if (!first)
			tok = skip(tok, ",");
		first = false;

		struct Type *ty = declarator(&tok, tok, basety);
		if (!ty->name)
			error_tok(ty->name_pos, "variable name omitted");

		struct Obj *var = new_gvar(get_ident(ty->name), ty);

		var->is_static = attr->is_static;
		var->is_definition = !attr->is_extern;

		if (attr->align)
			var->align = attr->align;

		if (equal(tok, "="))
			gvar_initializer(&tok, tok->next, var);
	}
	return tok;
}

// compound-stmt = (declaration | stmt)* "}"
static struct Node *compound_stmt(struct Token **rest, struct Token *tok)
{
	struct Node head = {};
	struct Node *cur = &head;
	struct Node *node = new_node(ND_BLOCK, tok);

	enter_scope();
	while (!equal(tok, "}")) {
		// make sure it's not a label
		if (is_typename(tok) && !equal(tok->next, ":")) {
			// variable declaration
			struct VarAttr attr = {};
			struct Type *basety = declspec(&tok, tok, &attr);

			if (attr.is_typedef) {
				tok = parse_typedef(tok, basety);
				continue;
			}

			if (is_function(tok)) {
				tok = function(tok, basety, &attr);
				continue;
			}

			if (attr.is_extern) {
				tok = global_variable(tok, basety, &attr);
				continue;
			}

			cur->next = declaration(&tok, tok, basety, &attr);
		} else {
			cur->next = stmt(&tok, tok);
		}
		cur = cur->next;
		add_type(cur);
	}
	node->body = head.next;
	leave_scope();

	// skip "}"
	*rest = tok->next;
	return node;
}

// program = (typedef | function-definition | global-variable)*
struct Obj *parser(struct Token *tok)
{
	while (tok->kind != TK_EOF) {
		struct VarAttr attr = {};
		struct Type *basety = declspec(&tok, tok, &attr);

		// typedef
		if (attr.is_typedef) {
			tok = parse_typedef(tok, basety);
			continue;
		}

		if (is_function(tok)) {
			// function
			tok = function(tok, basety, &attr);
		} else {
			// global variable
			tok = global_variable(tok, basety, &attr);
		}
	}

	return ret_globals();
}
