#include <toycc.h>

// code generator
static int depth = 0;
static void push(char *reg)
{
	printf("\taddi sp, sp, -8\n");
	printf("\tsd %s, 0(sp)\n", reg);
	depth++;
}

static void pop(char *reg)
{
	printf("\tld %s, 0(sp)\n", reg);
	printf("\taddi sp, sp, 8\n");
	depth--;
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

void codegen(struct Node *node)
{
	printf(".global main\n");
	printf("main:\n");

	gen_expr(node);
	printf("\tret\n");

	assert(!depth);
}
