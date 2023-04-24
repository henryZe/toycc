#include <toycc.h>
#include <type.h>

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

	// When we load a char or a short value to a register, we always
	// extend them to the size of int, so we can assume the lower half of
	// a register always contains a valid value. The upper half of a
	// register for char, short and int may contain garbage. When we load
	// a long value to a register, it simply occupies the entire register.
	if (ty->size == sizeof(char))
		println("\tlb a0, (a0)");
	else if (ty->size == sizeof(short))
		println("\tlh a0, (a0)");
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
	else if (ty->size == sizeof(short))
		println("\tsh a0, (a1)");
	else if (ty->size == sizeof(int))
		println("\tsw a0, (a1)");
	else
		println("\tsd a0, (a1)");
}

enum { I8, I16, I32, I64 };

static int getTypeId(struct Type *ty)
{
	switch (ty->kind) {
	case TY_CHAR:
		return I8;
	case TY_SHORT:
		return I16;
	case TY_INT:
		return I32;
	default:
		return I64;
	}
}

static const char toi8[] = "\tslli a0, a0, 56\n\tsrai a0, a0, 56";
static const char toi16[] = "\tslli a0, a0, 48\n\tsrai a0, a0, 48";
static const char toi32[] = "\tslli a0, a0, 32\n\tsrai a0, a0, 32";
static const char *castMatrix[4][4] = {
	{ NULL, NULL, NULL, NULL, },	// i8
	{ toi8, NULL, NULL, NULL, },	// i16 -> i8
	{ toi8, toi16, NULL, NULL, },	// i32 -> i8, i16
	{ toi8, toi16, toi32, NULL, },	// i64 -> i8, i16, i32
};

static void cast(struct Type *from, struct Type *to)
{
	if (to->kind == TY_VOID)
		return;

	if (to->kind == TY_BOOL) {
		println("\tsnez a0, a0");
		return;
	}

	int t1 = getTypeId(from);
	int t2 = getTypeId(to);

	if (castMatrix[t1][t2]) {
		debug("\t# cast t1 %d t2 %d", t1, t2);
		println("%s", castMatrix[t1][t2]);
		debug("\t# end cast");
	}
}

// Generate code for a given node.
static void gen_expr(struct Node *node)
{
	int c;

	println("\t.loc 1 %d", node->tok->line_no);

	switch (node->kind) {
	case ND_NULL_EXPR:
		return;

	case ND_NUM:
		println("\tli a0, %ld", node->val);
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

	case ND_CAST:
		gen_expr(node->lhs);
		cast(node->lhs->ty, node->ty);
		return;

	case ND_MEMZERO:
		debug("\t# ND_MEMZERO size %d", node->var->ty->size);
		for (int i = 0; i < node->var->ty->size; i++) {
			println("\tsb zero, %d(fp)", node->var->offset + i);
		}
		debug("\t# end ND_MEMZERO");
		return;

	case ND_COND:
		c = count();
		gen_expr(node->cond);
		println("\tbeqz a0, .L.else.%d", c);
		gen_expr(node->then);
		println("\tj .L.end.%d", c);
		println(".L.else.%d:", c);
		gen_expr(node->els);
		println(".L.end.%d:", c);
		return;

	case ND_NOT:
		gen_expr(node->lhs);
		println("\tseqz a0, a0");
		return;

	case ND_BITNOT:
		gen_expr(node->lhs);
		println("\tnot a0, a0");
		return;

	case ND_LOGAND:
		c = count();
		gen_expr(node->lhs);
		println("\tbeqz a0, .L.false.%d", c);
		gen_expr(node->rhs);
		println("\tbeqz a0, .L.false.%d", c);
		println("\tli a0, 1");
		println("\tj .L.end.%d", c);
		println(".L.false.%d:", c);
		println("\tli a0, 0");
		println(".L.end.%d:", c);
		return;

	case ND_LOGOR:
		c = count();
		gen_expr(node->lhs);
		println("\tbnez a0, .L.true.%d", c);
		gen_expr(node->rhs);
		println("\tbnez a0, .L.true.%d", c);
		println("\tli a0, 0");
		println("\tj .L.end.%d", c);
		println(".L.true.%d:", c);
		println("\tli a0, 1");
		println(".L.end.%d:", c);
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

	// default type is int
	const char *suffix = "w";
	// if type is long or pointer
	if (node->lhs->ty->kind == TY_LONG || node->lhs->ty->base)
		suffix = "";

	switch (node->kind) {
	case ND_ADD:
		println("\tadd%s a0, a0, a1", suffix);
		break;
	case ND_SUB:
		println("\tsub%s a0, a0, a1", suffix);
		break;
	case ND_MUL:
		println("\tmul%s a0, a0, a1", suffix);
		break;
	case ND_DIV:
		println("\tdiv%s a0, a0, a1", suffix);
		break;
	case ND_MOD:
		println("\trem%s a0, a0, a1", suffix);
		break;
	case ND_BITAND:
		println("\tand a0, a0, a1");
		break;
	case ND_BITOR:
		println("\tor a0, a0, a1");
		break;
	case ND_BITXOR:
		println("\txor a0, a0, a1");
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
	case ND_SHL:
		if (node->ty->size == sizeof(long))
			println("\tsll a0, a0, a1");
		else
			println("\tsllw a0, a0, a1");
		break;
	case ND_SHR:
		if (node->ty->size == sizeof(long))
			println("\tsra a0, a0, a1");
		else
			println("\tsraw a0, a0, a1");
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
			println("\tbeqz a0, %s", node->brk_label);
		}
		gen_stmt(node->then);
		println("%s:", node->cont_label);
		if (node->inc)
			gen_expr(node->inc);
		println("\tj begin.%d", c);

		println("%s:", node->brk_label);
		debug("\t# end ND_FOR");
		return;

	case ND_DO:
		c = count();

		println("begin.%d:", c);
		gen_stmt(node->then);
		println("%s:", node->cont_label);

		gen_expr(node->cond);
		println("\tbnez a0, begin.%d", c);

		println("%s:", node->brk_label);
		return;

	case ND_SWITCH:
		gen_expr(node->cond);

		for (struct Node *n = node->case_next; n; n = n->case_next) {
			println("\tli a1, %d", n->val);
			println("\tbeq a0, a1, %s", n->label);
		}

		if (node->default_case)
			println("\tj %s", node->default_case->label);

		// "case"s are over
		println("\tj %s", node->brk_label);
		gen_stmt(node->then);
		println("%s:", node->brk_label);
		return;

	case ND_CASE:
		println("%s:", node->label);
		gen_stmt(node->lhs);
		return;

	case ND_BLOCK:
		for (struct Node *n = node->body; n; n = n->next)
			gen_stmt(n);
		return;

	case ND_GOTO:
		println("\tj %s", node->unique_label);
		return;

	case ND_LABEL:
		println("%s:", node->unique_label);
		gen_stmt(node->lhs);
		return;

	case ND_RETURN:
		if (node->lhs)
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
			offset = align_to(offset, var->align);
			var->offset = -offset;
		}
		// initialize stack size
		fn->stack_size = align_to(offset, sizeof(long));
	}
}

static void emit_data(struct Obj *prog)
{
	for (struct Obj *var = prog; var; var = var->next) {
		if (var->is_function || !var->is_definition)
			continue;

		if (var->is_static)
			println(".local %s", var->name);
		else
			println(".global %s", var->name);

		println(".align %d", llog2(var->align));

		if (!var->init_data) {
			println(".bss");
			println("%s:", var->name);
			println("\t.zero %d", var->ty->size);
			continue;
		}

		struct Relocation *rel = var->rel;
		int pos = 0;

		println(".data");
		println("%s:", var->name);

		while (pos < var->ty->size) {
			if (rel && rel->offset == pos) {
				// declare as a pointer
				println("\t.quad %s+%ld", rel->label, rel->addend);
				rel = rel->next;
				pos += 8;
			} else {
				println("\t.byte %d", var->init_data[pos++]);
			}
		}
	}
}

static void store_args(int r, int offset, int sz)
{
	switch (sz) {
	case sizeof(char):
		println("\tsb %s, %d(sp)", argreg[r], offset);
		break;

	case sizeof(short):
		println("\tsh %s, %d(sp)", argreg[r], offset);
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
		// function definition
		if (!fn->is_function || !fn->is_definition)
			continue;

		println(".text");
		if (fn->is_static)
			println(".local %s", fn->name);
		else
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
