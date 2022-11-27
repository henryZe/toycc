#include <toycc.h>

#ifdef DEBUG
#define debug(fmt, args...) println(fmt, ##args)
#else
#define debug(fmt, args...)
#endif

static FILE *output_file;

static void println(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(output_file, fmt, ap);
	va_end(ap);
	fprintf(output_file, "\n");
}

static int count(void)
{
	static int i = 1;
	return i++;
}

// code generator
static int depth = 0;
static const char * const argreg[] = {
	"a0",
	"a1",
	"a2",
	"a3",
	"a4",
	"a5",
};

// push reg into 0(sp)
static void push(const char *reg)
{
	println("\taddi sp, sp, -%ld", sizeof(long));
	println("\tsd %s, 0(sp)", reg);
	depth++;
}

// pop 0(sp) to reg
static void pop(const char *reg)
{
	println("\tld %s, 0(sp)", reg);
	println("\taddi sp, sp, %ld", sizeof(long));
	depth--;
}

// Round up `n` to the nearest multiple of `align`. For instance,
// align_to(5, 8) returns 8 and align_to(11, 8) returns 16.
int align_to(int n, int align)
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
			println("\tadd a0, fp, %d", node->var->offset);
		else
			// global variable
			println("\tla a0, %s", node->var->name);
		break;

	case ND_DEREF:
		gen_expr(node->lhs);
		break;

	case ND_COMMA:
		gen_expr(node->lhs);
		gen_addr(node->rhs);
		break;

	case ND_MEMBER:
		gen_addr(node->lhs);
		println("\tadd a0, a0, %d", node->member->offset);
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
	// If it is an array, do not attempt to load a value
	// to the register because in general we can't load
	// an entire array to a register. As a result,
	// the result of an evaluation of an array becomes
	// not the array itself but the address of the array.
	// This is where "array is automatically converted to
	// a pointer to the first element of the array in C"
	// occurs.
	if (ty->kind == TY_ARRAY ||
		ty->kind == TY_STRUCT ||
		ty->kind == TY_UNION) {
		return;
	}

	if (ty->size == sizeof(char))
		println("\tlb a0, (a0)");
	else if (ty->size == sizeof(int))
		println("\tlw a0, (a0)");
	else
		println("\tld a0, (a0)");
}

// Store a0 to an address that the stack top is pointing to.
static void store(struct Type *ty)
{
	pop("a1");

	if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
		for (int i = 0; i < ty->size; i++) {
			// load & store byte by byte
			println("\tlb a2, %d(a0)", i);
			println("\tsb a2, %d(a1)", i);
		}
		return;
	}

	if (ty->size == sizeof(char))
		println("\tsb a0, (a1)");
	else if (ty->size == sizeof(int))
		println("\tsw a0, (a1)");
	else
		println("\tsd a0, (a1)");
}

// Generate code for a given node.
static void gen_expr(struct Node *node)
{
	println("\t.loc 1 %d", node->tok->line_no);

	switch (node->kind) {
	case ND_NUM:
		println("\tli a0, %d", node->val);
		return;

	case ND_NEG:
		gen_expr(node->lhs);
		println("\tneg a0, a0");
		return;

	case ND_VAR:
	case ND_MEMBER:
		gen_addr(node);
		load(node->ty);
		return;

	case ND_DEREF:
		debug("\t# ND_DEREF load");
		gen_expr(node->lhs);
		load(node->ty);
		debug("\t# end ND_DEREF load");
		return;

	case ND_ADDR:
		debug("\t# ND_ADDR var");
		gen_addr(node->lhs);
		debug("\t# end ND_ADDR var");
		return;

	case ND_ASSIGN:
		debug("\t# ND_ASSIGN var");
		gen_addr(node->lhs);
		push("a0");
		gen_expr(node->rhs);
		store(node->ty);
		debug("\t# end ND_ASSIGN var");
		return;

	case ND_STMT_EXPR:
		for (struct Node *n = node->body; n; n = n->next)
			gen_stmt(n);
		return;

	case ND_COMMA:
		gen_expr(node->lhs);
		gen_expr(node->rhs);
		return;

	case ND_FUNCALL:
		debug("\t# ND_FUNCALL func %s", node->funcname);

		int nargs = 0;

		for (struct Node *arg = node->args; arg; arg = arg->next) {
			gen_expr(arg);
			push("a0");
			nargs++;
		}

		for (int i = nargs - 1; i >= 0; i--) {
			pop(argreg[i]);
		}

		println("\tcall %s", node->funcname);

		debug("\t# end ND_FUNCALL func %s", node->funcname);
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
		println("\tadd a0, a0, a1");
		break;
	case ND_SUB:
		println("\tsub a0, a0, a1");
		break;
	case ND_MUL:
		println("\tmul a0, a0, a1");
		break;
	case ND_DIV:
		println("\tdiv a0, a0, a1");
		break;
	case ND_EQ:
		println("\txor a0, a0, a1");
		println("\tseqz a0, a0");
		break;
	case ND_NE:
		println("\txor a0, a0, a1");
		println("\tsnez a0, a0");
		break;
	case ND_LT:
		println("\tslt a0, a0, a1");
		break;
	case ND_LE:
		println("\tslt a0, a1, a0");
		println("\tseqz a0, a0");
		break;
	default:
		error_tok(node->tok, "invalid expression");
		break;
	}
}

static void gen_stmt(struct Node *node)
{
	println("\t.loc 1 %d", node->tok->line_no);

	int c;

	switch (node->kind) {
	case ND_IF:
		c = count();

		debug("\t# ND_IF");
		gen_expr(node->cond);
		println("\tbeqz a0, else.%d", c);

		gen_stmt(node->then);
		println("\tj end.%d", c);

		println("else.%d:", c);
		if (node->els)
			gen_stmt(node->els);

		println("end.%d:", c);
		debug("\t# end ND_IF");
		return;

	case ND_FOR:
		c = count();

		debug("\t# ND_FOR");
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
		println("\tj begin.%d", c);

		println("end.%d:", c);
		debug("\t# end ND_FOR");
		return;

	case ND_BLOCK:
		for (struct Node *n = node->body; n; n = n->next)
			gen_stmt(n);
		return;

	case ND_RETURN:
		gen_expr(node->lhs);
		println("\tj return.%s", current_fn->name);
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
			offset = align_to(offset, var->ty->align);
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

static void store_args(int r, int offset, int sz)
{
	switch (sz) {
	case sizeof(char):
		println("\tsb %s, %d(sp)", argreg[r], offset);
		break;

	case sizeof(int):
		println("\tsw %s, %d(sp)", argreg[r], offset);
		break;

	case sizeof(long):
		println("\tsd %s, %d(sp)", argreg[r], offset);
		break;

	default:
		unreachable();
		break;
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

		// Prologue
		push("fp");
		push("ra");
		println("\tmv fp, sp");

		// Save passed-by-register arguments to the stack
		int i = 0;
		debug("\t# '%s' save args into stack", fn->name);
		for (struct Obj *var = fn->params; var; var = var->next)
			store_args(i++, var->offset, var->ty->size);
		println("\tadd sp, sp, -%d", fn->stack_size);
		debug("\t# end '%s' save args", fn->name);

		// Emit code
		int cur_depth = depth;
		gen_stmt(fn->body);
		assert(depth == cur_depth);

		// epilogue
		println("return.%s:", fn->name);
		// restore sp register
		println("\tmv sp, fp");
		// restore ra register
		pop("ra");
		// restore fp register
		pop("fp");
		// mv ra to pc
		println("\tret");

		assert(!depth);
	}
}

// Traverse the AST to emit assembly.
void codegen(struct Obj *prog, FILE *out)
{
	output_file = out;

	assign_lvar_offsets(prog);
	emit_data(prog);
	emit_text(prog);
}
