#include <toycc.h>

#ifdef DEBUG
#define debug(fmt, args...) println(fmt, ##args)
#else
#define debug(fmt, args...)
#endif

static int count(void)
{
	static int i = 1;
	return i++;
}

// Round up `n` to the nearest multiple of `align`. For instance,
// align_to(5, 8) returns 8 and align_to(11, 8) returns 16.
static int align_to(int n, int align)
{
	return (n + align - 1) / align * align;
}

static void gen_expr(struct Node *node);
static void gen_stmt(struct Node *node);

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(struct Node *node)
{
	switch (node->kind) {
	case ND_VAR:
		if (node->var->is_local)
			// local variable
			addnum(argreg[0], "fp", node->var->offset);
		else
			// global variable
			loadaddr(argreg[0], node->var->name);
		break;

	case ND_DEREF:
		gen_expr(node->lhs);
		break;

	default:
		error_tok(node->tok, "not a lvalue");
		break;
	}
}

static struct Obj *current_fn;

// Load a value from where a0 is pointing to.
static void load(struct Type *ty)
{
	if (ty->kind == TY_ARRAY) {
		// If it is an array, do not attempt to load a value
		// to the register because in general we can't load
		// an entire array to a register. As a result,
		// the result of an evaluation of an array becomes
		// not the array itself but the address of the array.
		// This is where "array is automatically converted to
		// a pointer to the first element of the array in C"
		// occurs.
		return;
	}

	if (ty->size == sizeof(char))
		loadb(argreg[0], argreg[0], 0);
	else
		loadd(argreg[0], argreg[0], 0);
}

// Store a0 to an address that the stack top is pointing to.
static void store(struct Type *ty)
{
	pop(argreg[1]);

	if (ty->size == sizeof(char))
		storeb(argreg[0], argreg[1], 0);
	else
		stored(argreg[0], argreg[1], 0);
}

void gen_call(struct Node *node)
{
	int nargs = 0;

	debug("\t# call %s", node->funcname);
	for (struct Node *arg = node->args; arg; arg = arg->next) {
		gen_expr(arg);
		push(argreg[0]);
		nargs++;
	}
	for (int i = nargs - 1; i >= 0; i--) {
		pop(argreg[i]);
	}
	call(node->funcname);
	debug("\t# end call %s", node->funcname);
}

// Generate code for a given node.
static void gen_expr(struct Node *node)
{
	switch (node->kind) {
	case ND_NUM:
		loadnum(argreg[0], node->val);
		return;

	case ND_NEG:
		gen_expr(node->lhs);
		neg(argreg[0], argreg[0]);
		return;

	case ND_VAR:
		debug("\t# var %s", node->var->name);
		gen_addr(node);
		load(node->ty);
		debug("\t# end var %s", node->var->name);
		return;

	case ND_DEREF:
		debug("\t# deref");
		gen_expr(node->lhs);
		load(node->ty);
		debug("\t# end deref");
		return;

	case ND_ADDR:
		debug("\t# addr");
		gen_addr(node->lhs);
		debug("\t# end addr");
		return;

	case ND_ASSIGN:
		debug("\t# assign");
		gen_addr(node->lhs);
		push(argreg[0]);
		gen_expr(node->rhs);
		store(node->ty);
		debug("\t# end assign");
		return;

	case ND_STMT_EXPR:
		for (struct Node *n = node->body; n; n = n->next)
			gen_stmt(n);
		return;

	case ND_FUNCALL:
		gen_call(node);
		return;

	default:
		break;
	}

	// left_side -> a0
	// right_side -> a1
	gen_expr(node->rhs);
	push(argreg[0]);
	gen_expr(node->lhs);
	pop(argreg[1]);

	switch (node->kind) {
	case ND_ADD:
		addreg(argreg[0], argreg[1]);
		break;
	case ND_SUB:
		subreg(argreg[0], argreg[1]);
		break;
	case ND_MUL:
		mulreg(argreg[0], argreg[1]);
		break;
	case ND_DIV:
		divreg(argreg[0], argreg[1]);
		break;
	case ND_EQ:
		eq(argreg[0], argreg[1]);
		break;
	case ND_NE:
		neq(argreg[0], argreg[1]);
		break;
	case ND_LT:
		less_than(argreg[0], argreg[1]);
		break;
	case ND_LE:
		less_equal(argreg[0], argreg[1]);
		break;
	default:
		error_tok(node->tok, "invalid expression");
		break;
	}
}

static void gen_if(struct Node *node)
{
	int c = count();

	debug("\t# %s", __func__);
	gen_expr(node->cond);
	println("\tbeqz a0, else.%d", c);

	gen_stmt(node->then);
	jmp(format("end.%d", c));

	println("else.%d:", c);
	if (node->els)
		gen_stmt(node->els);

	println("end.%d:", c);
	debug("\t# end %s", __func__);
}

static void gen_for(struct Node *node)
{
	int c = count();

	debug("\t# %s", __func__);
	if (node->init)
		gen_stmt(node->init);

	println("begin.%d:", c);
	if (node->cond) {
		gen_expr(node->cond);
		println("\tbeqz a0, end.%d", c);
	}
	gen_stmt(node->then);
	if (node->inc)
		gen_expr(node->inc);
	jmp(format("begin.%d", c));

	println("end.%d:", c);
	debug("\t# end %s", __func__);
}

static void gen_stmt(struct Node *node)
{
	switch (node->kind) {
	case ND_IF:
		gen_if(node);
		break;

	case ND_FOR:
		gen_for(node);
		break;

	case ND_BLOCK:
		for (struct Node *n = node->body; n; n = n->next)
			gen_stmt(n);
		break;

	case ND_RETURN:
		gen_expr(node->lhs);
		jmp(format("return.%s", current_fn->name));
		break;

	case ND_EXPR_STMT:
		gen_expr(node->lhs);
		break;

	default:
		error_tok(node->tok, "invalid statement");
		break;
	}
}

// Assign offsets to local variables.
static void assign_lvar_offsets(struct Obj *prog)
{
	for (struct Obj *fn = prog; fn; fn = fn->next) {
		if (!fn->is_function)
			continue;

		int offset = 0;
		// initialize var's offset
		for (struct Obj *var = fn->locals; var; var = var->next) {
			offset += var->ty->size;
			var->offset = -offset;
		}
		// initialize stack size
		fn->stack_size = align_to(offset, sizeof(long));
	}
}

static void emit_data(struct Obj *prog)
{
	for (struct Obj *var = prog; var; var = var->next) {
		if (var->is_function)
			continue;

		println(".data");
		println(".global %s", var->name);
		println("%s:", var->name);

		if (var->init_data) {
			for (int i = 0; i < var->ty->size; i++)
				println("\t.byte %d", var->init_data[i]);
		} else {
			println("\t.zero %d", var->ty->size);
		}
	}
}

static void emit_text(struct Obj *prog)
{
	for (struct Obj *fn = prog; fn; fn = fn->next) {
		if (!fn->is_function)
			continue;

		println(".text");
		println(".global %s", fn->name);
		println("%s:", fn->name);
		current_fn = fn;

		prologue();

		// Save passed-by-register arguments to the stack
		int i = 0;
		debug("\t# '%s' save args into stack", fn->name);
		for (struct Obj *var = fn->params; var; var = var->next) {
			if (var->ty->size == sizeof(char))
				storeb(argreg[i++], "sp", var->offset);
			else
				stored(argreg[i++], "sp", var->offset);
		}
		addnum("sp", "sp", -fn->stack_size);
		debug("\t# end '%s' save args", fn->name);

		// Emit code
		gen_stmt(fn->body);

		println("return.%s:", fn->name);
		epilogue();
	}
}

// Traverse the AST to emit assembly.
void codegen(struct Obj *prog, FILE *out)
{
	set_output_file(out);

	assign_lvar_offsets(prog);
	emit_data(prog);
	emit_text(prog);
}
