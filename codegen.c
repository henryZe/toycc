#include <toycc.h>

static int count(void)
{
	static int i = 1;
	return i++;
}

// code generator
static int depth = 0;
static char *argreg[] = {
	"a0",
	"a1",
	"a2",
	"a3",
	"a4",
	"a5",
};

// push reg into 0(sp)
static void push(char *reg)
{
	printf("\taddi sp, sp, -%ld\n", sizeof(long));
	printf("\tsd %s, 0(sp)\n", reg);
	depth++;
}

// pop 0(sp) to reg
static void pop(char *reg)
{
	printf("\tld %s, 0(sp)\n", reg);
	printf("\taddi sp, sp, %ld\n", sizeof(long));
	depth--;
}

// Round up `n` to the nearest multiple of `align`. For instance,
// align_to(5, 8) returns 8 and align_to(11, 8) returns 16.
static int align_to(int n, int align)
{
	return (n + align - 1) / align * align;
}

static void gen_expr(struct Node *node);

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(struct Node *node)
{
	switch (node->kind) {
	case ND_VAR:
		if (node->var->is_local)
			// local variable
			printf("\tadd a0, fp, %d\n", node->var->offset);
		else
			// global variable
			printf("\tla a0, %s\n", node->var->name);
		return;

	case ND_DEREF:
		gen_expr(node->lhs);
		return;

	default:
		break;
	}

	error_tok(node->tok, "not a lvalue");
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
		printf("\tlb a0, (a0)\n");
	else
		printf("\tld a0, (a0)\n");
}

// Store a0 to an address that the stack top is pointing to.
static void store(struct Type *ty)
{
	pop("a1");

	if (ty->size == sizeof(char))
		printf("\tsb a0, (a1)\n");
	else
		printf("\tsd a0, (a1)\n");
}

// Generate code for a given node.
static void gen_expr(struct Node *node)
{
	switch (node->kind) {
	case ND_NUM:
		printf("\tli a0, %d\n", node->val);
		return;

	case ND_NEG:
		gen_expr(node->lhs);
		printf("\tneg a0, a0\n");
		return;

	case ND_VAR:
		debug("\t# ND_VAR load var %s\n",
					node->var->name);
		gen_addr(node);
		load(node->ty);
		debug("\t# end ND_VAR load var %s\n",
					node->var->name);
		return;

	case ND_DEREF:
		debug("\t# ND_DEREF load\n");
		gen_expr(node->lhs);
		load(node->ty);
		debug("\t# end ND_DEREF load\n");
		return;

	case ND_ADDR:
		debug("\t# ND_ADDR var\n");
		gen_addr(node->lhs);
		debug("\t# end ND_ADDR var\n");
		return;

	case ND_ASSIGN:
		debug("\t# ND_ASSIGN var\n");
		gen_addr(node->lhs);
		push("a0");
		gen_expr(node->rhs);
		store(node->ty);
		debug("\t# end ND_ASSIGN var\n");
		return;

	case ND_FUNCALL: {
		int nargs = 0;

		debug("\t# ND_FUNCALL func %s\n", node->funcname);

		for (struct Node *arg = node->args; arg; arg = arg->next) {
			gen_expr(arg);
			push("a0");
			nargs++;
		}

		for (int i = nargs - 1; i >= 0; i--) {
			pop(argreg[i]);
		}

		printf("\tcall %s\n", node->funcname);

		debug("\t# end ND_FUNCALL func %s\n", node->funcname);
		return;
	}

	default:
		break;
	}

	// left_side -> a0
	// right_side -> a1
	gen_expr(node->rhs);
	push("a0");
	gen_expr(node->lhs);
	pop("a1");

	switch (node->kind) {
	case ND_ADD:
		printf("\tadd a0, a0, a1\n");
		break;
	case ND_SUB:
		printf("\tsub a0, a0, a1\n");
		break;
	case ND_MUL:
		printf("\tmul a0, a0, a1\n");
		break;
	case ND_DIV:
		printf("\tdiv a0, a0, a1\n");
		break;
	case ND_EQ:
		printf("\txor a0, a0, a1\n");
		printf("\tseqz a0, a0\n");
		break;
	case ND_NE:
		printf("\txor a0, a0, a1\n");
		printf("\tsnez a0, a0\n");
		break;
	case ND_LT:
		printf("\tslt a0, a0, a1\n");
		break;
	case ND_LE:
		printf("\tslt a0, a1, a0\n");
		printf("\tseqz a0, a0\n");
		break;
	default:
		error_tok(node->tok, "invalid expression");
		break;
	}
}

static void gen_stmt(struct Node *node)
{
	int c;

	switch (node->kind) {
	case ND_IF:
		c = count();

		debug("\t# ND_IF\n");
		gen_expr(node->cond);
		printf("\tbeqz a0, else.%d\n", c);

		gen_stmt(node->then);
		printf("\tj end.%d\n", c);

		printf("else.%d:\n", c);
		if (node->els)
			gen_stmt(node->els);

		printf("end.%d:\n", c);
		debug("\t# end ND_IF\n");
		return;

	case ND_FOR:
		c = count();

		debug("\t# ND_FOR\n");
		if (node->init)
			gen_stmt(node->init);

		printf("begin.%d:\n", c);
		if (node->cond) {
			gen_expr(node->cond);
			printf("\tbeqz a0, end.%d\n", c);
		}
		gen_stmt(node->then);
		if (node->inc)
			gen_expr(node->inc);
		printf("\tj begin.%d\n", c);

		printf("end.%d:\n", c);
		debug("\t# end ND_FOR\n");
		return;

	case ND_BLOCK:
		for (struct Node *n = node->body; n; n = n->next)
			gen_stmt(n);
		return;

	case ND_RETURN:
		gen_expr(node->lhs);
		printf("\tj return.%s\n", current_fn->name);
		return;

	case ND_EXPR_STMT:
		gen_expr(node->lhs);
		return;

	default:
		break;
	}

	error_tok(node->tok, "invalid statement");
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

		printf(".data\n");
		printf(".global %s\n", var->name);
		printf("%s:\n", var->name);

		if (var->init_data) {
			for (int i = 0; i < var->ty->size; i++)
				printf("\t.byte %d\n", var->init_data[i]);
		} else {
			printf("\t.zero %d\n", var->ty->size);
		}
	}
}

static void emit_text(struct Obj *prog)
{
	for (struct Obj *fn = prog; fn; fn = fn->next) {
		if (!fn->is_function)
			continue;

		printf(".text\n");
		printf(".global %s\n", fn->name);
		printf("%s:\n", fn->name);
		current_fn = fn;

		// Prologue
		push("fp");
		push("ra");
		printf("\tmv fp, sp\n");

		// Save passed-by-register arguments to the stack
		int i = 0;
		debug("\t# '%s' save args into stack\n", fn->name);
		for (struct Obj *var = fn->params; var; var = var->next) {
			if (var->ty->size == sizeof(char))
				printf("\tsb %s, %d(sp)\n", argreg[i++], var->offset);
			else
				printf("\tsd %s, %d(sp)\n", argreg[i++], var->offset);
		}
		printf("\tadd sp, sp, -%d\n", fn->stack_size);
		debug("\t# end '%s' save args\n", fn->name);

		// Emit code
		int cur_depth = depth;
		gen_stmt(fn->body);
		assert(depth == cur_depth);

		// epilogue
		printf("return.%s:\n", fn->name);
		// restore sp register
		printf("\tmv sp, fp\n");
		// restore ra register
		pop("ra");
		// restore fp register
		pop("fp");
		// mv ra to pc
		printf("\tret\n");

		assert(!depth);
	}
}

// Traverse the AST to emit assembly.
void codegen(struct Obj *prog)
{
	assign_lvar_offsets(prog);
	emit_data(prog);
	emit_text(prog);
}
