#include <toycc.h>

// code generator
static int depth = 0;

// push reg into 0(sp)
static void push(char *reg)
{
	printf("\taddi sp, sp, -8\n");
	printf("\tsd %s, 0(sp)\n", reg);
	depth++;
}

// pop 0(sp) to reg
static void pop(char *reg)
{
	printf("\tld %s, 0(sp)\n", reg);
	printf("\taddi sp, sp, 8\n");
	depth--;
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(struct Node *node)
{
	if (node->kind == ND_VAR) {
		assert(islower(node->name));

		int offset = (node->name - 'a' + 1) * sizeof(long);
		printf("\tadd a0, fp, %d\n", -offset);
		return;
	}

	error("not a lvalue");
}

// Traverse the AST to emit assembly.
void gen_expr(struct Node *node)
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

	case ND_ASSIGN:
		gen_addr(node->lhs);
		push("a0");
		gen_expr(node->rhs);
		pop("a1");
		printf("\tsd a0, (a1)\n");
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
		error("invalid expression");
		break;
	}
}

static void gen_stmt(struct Node *node)
{
	if (node->kind == ND_EXPR_STMT) {
		gen_expr(node->lhs);
		return;
	}

	error("invalid statement");
}

void codegen(struct Node *node)
{
	printf(".global main\n");
	printf("main:\n");

	// prologue
	push("fp");
	printf("\tmv fp, sp\n");
	printf("\tadd sp, sp, -(26 * 8)\n");

	for (struct Node *n = node; n; n = n->next) {
		gen_stmt(n);
	}

	// epilogue
	printf("\tadd sp, sp, (26 * 8)\n");
	pop("fp");
	printf("\tret\n");

	assert(!depth);
}
