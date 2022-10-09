#include <toycc.h>

static int count(void)
{
	static int i = 1;
	return i++;
}

// code generator
static int depth = 0;

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
		printf("\tadd a0, fp, %d\n", node->var->offset);
		return;

	case ND_DEREF:
		gen_expr(node->lhs);
		return;

	default:
		break;
	}

	error_tok(node->tok, "not a lvalue");
}

static char *argreg[] = { "a0", "a1", "a2", "a3", "a4", "a5" };

// Traverse the AST to emit assembly.
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
		gen_addr(node);
		printf("\tld a0, (a0)\n");
		return;

	case ND_DEREF:
		gen_expr(node->lhs);
		printf("\tld a0, (a0)\n");
		return;

	case ND_ADDR:
		gen_addr(node->lhs);
		return;

	case ND_ASSIGN:
		gen_addr(node->lhs);
		push("a0");
		gen_expr(node->rhs);
		pop("a1");
		printf("\tsd a0, (a1)\n");
		return;

	case ND_FUNCALL:
		int nargs = 0;
		for (struct Node *arg = node->args; arg; arg = arg->next) {
			gen_expr(arg);
			push("a0");
			nargs++;
		}

		for (int i = nargs - 1; i >= 0; i--)
			pop(argreg[i]);

		printf("\tcall %s\n", node->funcname);
		return;

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

		gen_expr(node->cond);
		printf("\tbeqz a0, else.%d\n", c);

		gen_stmt(node->then);
		printf("\tj end.%d\n", c);

		printf("else.%d:\n", c);
		if (node->els)
			gen_stmt(node->els);

		printf("end.%d:\n", c);
		return;

	case ND_FOR:
		c = count();

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
		return;

	case ND_BLOCK:
		for (struct Node *n = node->body; n; n = n->next)
			gen_stmt(n);
		return;

	case ND_RETURN:
		gen_expr(node->lhs);
		printf("\tj return\n");
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
static void assign_lvar_offsets(struct Function *prog)
{
	int offset = 0;
	for (struct Obj *var = prog->locals; var; var = var->next) {
		offset += sizeof(long);
		var->offset = -offset;
	}
	prog->stack_size = align_to(offset, sizeof(long));
}

// Traverse the AST to emit assembly.
void codegen(struct Function *prog)
{
	assign_lvar_offsets(prog);

	printf(".global main\n");
	printf("main:\n");

	// prologue
	push("fp");
	push("ra");
	printf("\tmv fp, sp\n");
	printf("\tadd sp, sp, -%d\n", prog->stack_size);

	int cur_depth = depth;
	gen_stmt(prog->body);
	assert(depth == cur_depth);

	// epilogue
	printf("return:\n");
	// restore sp register
	printf("\tmv sp, fp\n");
	// restore fp register
	pop("ra");
	pop("fp");
	// mv ra to pc
	printf("\tret\n");

	assert(!depth);
}
