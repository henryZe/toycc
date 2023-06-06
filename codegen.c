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
	"a6",
	"a7",
};

static const char * const argflt[] = {
	"fa0",
	"fa1",
	"fa2",
	"fa3",
	"fa4",
	"fa5",
	"fa6",
	"fa7",
};

// push reg into 0(sp)
static void push(const char *reg)
{
	const char *prefix;

	if (strcmp(reg, "fp") && (reg[0] == 'f'))
		prefix = "f";
	else
		prefix = "";

	println("\taddi sp, sp, -%ld", sizeof(long));
	println("\t%ssd %s, 0(sp)", prefix, reg);
	depth++;
}

// pop 0(sp) to reg
static void pop(const char *reg)
{
	const char *prefix;

	if (strcmp(reg, "fp") && (reg[0] == 'f'))
		prefix = "f";
	else
		prefix = "";

	println("\t%sld %s, 0(sp)", prefix, reg);
	println("\taddi sp, sp, %ld", sizeof(long));
	depth--;
}

// Round up `n` to the nearest multiple of `align`. For instance,
// align_to(5, 8) returns 8 and align_to(11, 8) returns 16.
int align_to(int n, int align)
{
	return (n + align - 1) / align * align;
}

static bool beyond_instruction_offset(int offset)
{
	// riscv immediate[11:0]
	return offset > 2047 || offset < -2048;
}

static void gen_expr(struct Node *node);
static void gen_stmt(struct Node *node);

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(struct Node *node)
{
	switch (node->kind) {
	case ND_VAR:
		if (node->var->is_local) {
			// local variable
			if (beyond_instruction_offset(node->var->offset)) {
				println("\tli t0, %d", node->var->offset);
				println("\tadd a0, fp, t0");
			} else {
				println("\tadd a0, fp, %d", node->var->offset);
			}
		} else {
			// global variable
			println("\tla a0, %s", node->var->name);
		}
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
	switch (ty->kind) {
	case TY_ARRAY:
	case TY_STRUCT:
	case TY_UNION:
		return;

	case TY_FLOAT:
		println("\tflw fa0, (a0)");
		return;
	case TY_DOUBLE:
		println("\tfld fa0, (a0)");
		return;

	default:
		break;
	}

	char *suffix = ty->is_unsigned ? "u" : "";

	// When we load a char or a short value to a register, we always
	// extend them to the size of int, so we can assume the lower half of
	// a register always contains a valid value. The upper half of a
	// register for char, short and int may contain garbage. When we load
	// a long value to a register, it simply occupies the entire register.
	if (ty->size == sizeof(char))
		println("\tlb%s a0, (a0)", suffix);
	else if (ty->size == sizeof(short))
		println("\tlh%s a0, (a0)", suffix);
	else if (ty->size == sizeof(int))
		println("\tlw%s a0, (a0)", suffix);
	else
		println("\tld a0, (a0)");
}

// Store a0 to an address that the stack top is pointing to.
static void store(struct Type *ty)
{
	pop("a1");

	switch (ty->kind) {
	case TY_STRUCT:
	case TY_UNION:
		for (int i = 0; i < ty->size; i++) {
			// load & store byte by byte
			println("\tlb t0, %d(a0)", i);
			println("\tsb t0, %d(a1)", i);
		}
		return;

	case TY_FLOAT:
		println("\tfsw fa0, (a1)");
		return;

	case TY_DOUBLE:
		println("\tfsd fa0, (a1)");
		return;

	default:
		break;
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

enum { I8, I16, I32, I64, U8, U16, U32, U64, F32, F64, CAST_MAX_TYPE };

static int getTypeId(struct Type *ty)
{
	switch (ty->kind) {
	case TY_CHAR:
		return ty->is_unsigned ? U8 : I8;
	case TY_SHORT:
		return ty->is_unsigned ? U16 : I16;
	case TY_INT:
		return ty->is_unsigned ? U32 : I32;
	case TY_LONG:
		return ty->is_unsigned ? U64 : I64;
	case TY_FLOAT:
		return F32;
	case TY_DOUBLE:
		return F64;
	default:
		return U64;
	}
}

// signed => shift right Arithmetic
#define TOI8   "\tslli a0, a0, 56\n\tsrai a0, a0, 56"
#define TOI16  "\tslli a0, a0, 48\n\tsrai a0, a0, 48"
#define TOI32  "\tslli a0, a0, 32\n\tsrai a0, a0, 32"

// unsigned => shift right Logical
#define TOU8   "\tslli a0, a0, 56\n\tsrli a0, a0, 56"
#define TOU16  "\tslli a0, a0, 48\n\tsrli a0, a0, 48"
#define TOU32  "\tslli a0, a0, 32\n\tsrli a0, a0, 32"

#define I32F32 "\tfcvt.s.w fa0, a0\n"
#define I32F64 "\tfcvt.d.w fa0, a0\n"
#define I64F32 "\tfcvt.s.l fa0, a0\n"
#define I64F64 "\tfcvt.d.l fa0, a0\n"
#define U32F32 "\tfcvt.s.wu fa0, a0\n"
#define U32F64 "\tfcvt.d.wu fa0, a0\n"
#define U64F32 "\tfcvt.s.lu fa0, a0\n"
#define U64F64 "\tfcvt.d.lu fa0, a0\n"
#define F32I32 "\tfcvt.w.s a0, fa0, rtz\n"
#define F32I8  (F32I32 TOI8)
#define F32I16 (F32I32 TOI16)
#define F32I64 "\tfcvt.l.s a0, fa0\n"
#define F32U32 "\tfcvt.wu.s a0, fa0\n"
#define F32U8  (F32U32 TOU8)
#define F32U16 (F32U32 TOU16)
#define F32U64 "\tfcvt.lu.s a0, fa0\n"
#define F32F64 "\tfcvt.d.s fa0, fa0\n"
#define F64I32 "\tfcvt.w.d a0, fa0, rtz\n"
#define F64I8  (F64I32 TOI8)
#define F64I16 (F64I32 TOI16)
#define F64I64 "\tfcvt.l.d a0, fa0\n"
#define F64U32 "\tfcvt.wu.d a0, fa0\n"
#define F64U8  (F64U32 TOU8)
#define F64U16 (F64U32 TOU16)
#define F64U64 "\tfcvt.lu.d a0, fa0\n"
#define F64F32 "\tfcvt.s.d fa0, fa0\n"

// cast_matrix[from][to]
static const char *cast_matrix[CAST_MAX_TYPE][CAST_MAX_TYPE] = {
	// to
	// i8,    i16,    i32,    i64,    u8,    u16,    u32,    u64,    f32,    f64,       // from
	{  NULL,  NULL,   NULL,   NULL,   NULL,  NULL,   NULL,   NULL,   I32F32, I32F64 },  // i8
	{  TOI8,  NULL,   NULL,   NULL,   TOU8,  NULL,   NULL,   NULL,   I32F32, I32F64 },  // i16
	{  TOI8,  TOI16,  NULL,   NULL,   TOU8,  TOU16,  NULL,   NULL,   I32F32, I32F64 },  // i32
	{  TOI8,  TOI16,  TOI32,  NULL,   TOU8,  TOU16,  TOU32,  NULL,   I64F32, I64F64 },  // i64
	{  NULL,  NULL,   NULL,   NULL,   NULL,  NULL,   NULL,   NULL,   I32F32, I32F64 },  // u8
	{  TOI8,  NULL,   NULL,   NULL,   TOU8,  NULL,   NULL,   NULL,   I32F32, I32F64 },  // u16
	{  TOI8,  TOI16,  NULL,   NULL,   TOU8,  TOU16,  NULL,   NULL,   U32F32, U32F64 },  // u32
	{  TOI8,  TOI16,  TOI32,  NULL,   TOU8,  TOU16,  TOU32,  NULL,   U64F32, U64F64 },  // u64
	{  F32I8, F32I16, F32I32, F32I64, F32U8, F32U16, F32U32, F32U64, NULL,   F32F64 },  // f32
	{  F64I8, F64I16, F64I32, F64I64, F64U8, F64U16, F64U32, F64U64, F64F32, NULL   },  // f64
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

	if (cast_matrix[t1][t2]) {
		debug("\t# cast t1 %d t2 %d", t1, t2);
		println("%s", cast_matrix[t1][t2]);
		debug("\t# end cast");
	}
}

static void cmp_zero(struct Type *ty)
{
	switch (ty->kind) {
	case TY_FLOAT:
		println("\tfmv.s.x fa1, zero");
		println("\tfeq.s a0, fa0, fa1");
		break;

	case TY_DOUBLE:
		println("\tfmv.d.x fa1, zero");
		println("\tfeq.d a0, fa0, fa1");
		break;

	default:
		println("\tseqz a0, a0");
		break;
	}
	return;
}

static void push_args(struct Node *args)
{
	if (args) {
		push_args(args->next);

		gen_expr(args);
		if (is_float(args->ty))
			push("fa0");
		else
			push("a0");
	}
}

// Generate code for a given node.
static void gen_expr(struct Node *node)
{
	int c;
	union {
		float f32;
		double f64;
		uint32_t u32;
		uint64_t u64;
	} u;

	// .loc $file-index $line-number
	println("\t.loc 1 %d", node->tok->line_no);

	switch (node->kind) {
	case ND_NULL_EXPR:
		return;

	case ND_NUM:
		switch (node->ty->kind) {
		case TY_FLOAT:
			u.f32 = node->fval;
			println("\tli a0, %u\t# float %f", u.u32, u.f32);
			println("\tfmv.s.x fa0, a0\t");
			return;

		case TY_DOUBLE:
			u.f64 = node->fval;
			println("\tli a0, %lu\t# double %f", u.u64, u.f64);
			println("\tfmv.d.x fa0, a0\t");
			return;

		default:
			println("\tli a0, %ld", node->val);
			return;
		}

	case ND_NEG:
		gen_expr(node->lhs);

		switch (node->ty->kind) {
		case TY_FLOAT:
			println("\tfneg.s fa0, fa0");
			break;

		case TY_DOUBLE:
			println("\tfneg.d fa0, fa0");
			break;

		default:
			if (node->ty->size == sizeof(long))
				println("\tneg a0, a0");
			else
				println("\tnegw a0, a0");
			break;
		}
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
			int offset = node->var->offset + i;

			if (beyond_instruction_offset(offset)) {
				println("\tli t0, %d", offset);
				println("\tadd t0, fp, t0");
				println("\tsb zero, (t0)");
			} else {
				println("\tsb zero, %d(fp)", offset);
			}
		}
		debug("\t# end ND_MEMZERO");
		return;

	case ND_COND:
		c = count();
		gen_expr(node->cond);
		cmp_zero(node->cond->ty);
		println("\tbnez a0, .L.else.%d", c);
		gen_expr(node->then);
		println("\tj .L.end.%d", c);
		println(".L.else.%d:", c);
		gen_expr(node->els);
		println(".L.end.%d:", c);
		return;

	case ND_NOT:
		gen_expr(node->lhs);
		cmp_zero(node->lhs->ty);
		return;

	case ND_BITNOT:
		gen_expr(node->lhs);
		println("\tnot a0, a0");
		return;

	case ND_LOGAND:
		c = count();
		gen_expr(node->lhs);
		cmp_zero(node->lhs->ty);
		println("\tbnez a0, .L.false.%d", c);
		gen_expr(node->rhs);
		cmp_zero(node->lhs->ty);
		println("\tbnez a0, .L.false.%d", c);
		println("\tli a0, 1");
		println("\tj .L.end.%d", c);
		println(".L.false.%d:", c);
		println("\tli a0, 0");
		println(".L.end.%d:", c);
		return;

	case ND_LOGOR:
		c = count();
		gen_expr(node->lhs);
		cmp_zero(node->lhs->ty);
		println("\tbeqz a0, .L.true.%d", c);
		gen_expr(node->rhs);
		cmp_zero(node->rhs->ty);
		println("\tbeqz a0, .L.true.%d", c);
		println("\tli a0, 0");
		println("\tj .L.end.%d", c);
		println(".L.true.%d:", c);
		println("\tli a0, 1");
		println(".L.end.%d:", c);
		return;

	case ND_FUNCALL:
		debug("\t# ND_FUNCALL func %s", node->funcname);

		push_args(node->args);

		int g_arg = 0, f_arg = 0;
		for (struct Node *arg = node->args; arg; arg = arg->next) {
			debug("\t# %sarg %.*s", node->func_ty->is_variadic ? "variadic " : "",
				arg->tok->len, arg->tok->loc);

			// transfer args to variadic function with generic registers
			if (node->func_ty->is_variadic || !is_float(arg->ty))
				pop(argreg[g_arg++]);
			else
				pop(argflt[f_arg++]);
		}

		println("\tcall %s", node->funcname);

		// It looks like the most significant 48 or 56 bits in a0 may
		// contain garbage if a function return type is short or bool/char,
		// respectively. We clear the upper bits here.
		switch (node->ty->kind) {
		case TY_BOOL:
		case TY_CHAR:
			println("\tslli a0, a0, 56");
			if (node->ty->is_unsigned)
				println("\tsrli a0, a0, 56");
			else
				println("\tsrai a0, a0, 56");
			break;

		case TY_SHORT:
			println("\tslli a0, a0, 48");
			if (node->ty->is_unsigned)
				println("\tsrli a0, a0, 48");
			else
				println("\tsrai a0, a0, 48");
			break;

		default:
			break;
		}

		debug("\t# end ND_FUNCALL func %s", node->funcname);
		return;

	default:
		break;
	}

	if (is_float(node->lhs->ty)) {
		gen_expr(node->rhs);
		push("fa0");
		gen_expr(node->lhs);
		pop("fa1");

		const char *sz = (node->lhs->ty->kind == TY_FLOAT) ? "s" : "d";

		switch (node->kind) {
		case ND_ADD:
			println("\tfadd.%s fa0, fa0, fa1", sz);
			break;

		case ND_SUB:
			println("\tfsub.%s fa0, fa0, fa1", sz);
			break;

		case ND_MUL:
			println("\tfmul.%s fa0, fa0, fa1", sz);
			break;

		case ND_DIV:
			println("\tfdiv.%s fa0, fa0, fa1", sz);
			break;

		case ND_EQ:
			println("\tfeq.%s a0, fa0, fa1", sz);
			break;

		case ND_NE:
			println("\tfeq.%s a0, fa0, fa1", sz);
			println("\tseqz a0, a0");
			break;

		case ND_LT:
			println("\tflt.%s a0, fa0, fa1", sz);
			break;

		case ND_LE:
			println("\tfle.%s a0, fa0, fa1", sz);
			break;

		default:
			error_tok(node->tok, "invalid expression");
			break;
		}
		return;
	}

	// left_side -> a0
	// right_side -> a1
	gen_expr(node->rhs);
	push("a0");
	gen_expr(node->lhs);
	pop("a1");

	const char *suffix;
	// if type is long or pointer
	if (node->lhs->ty->kind == TY_LONG || node->lhs->ty->base)
		suffix = "";
	else
		// default type is int
		suffix = "w";

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
		if (node->ty->is_unsigned)
			println("\tdivu%s a0, a0, a1", suffix);
		else
			println("\tdiv%s a0, a0, a1", suffix);
		break;
	case ND_MOD:
		if (node->ty->is_unsigned)
			println("\tremu%s a0, a0, a1", suffix);
		else
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
		if (node->lhs->ty->is_unsigned)
			println("\tsltu a0, a0, a1");
		else
			println("\tslt a0, a0, a1");
		break;
	case ND_LE:
		if (node->ty->is_unsigned)
			println("\tsltu a0, a1, a0");
		else
			println("\tslt a0, a1, a0");
		println("\tseqz a0, a0");
		break;
	case ND_SHL:
		println("\tsll%s a0, a0, a1", suffix);
		break;
	case ND_SHR:
		if (node->ty->is_unsigned)
			println("\tsrl%s a0, a0, a1", suffix);
		else
			println("\tsra%s a0, a0, a1", suffix);
		break;
	default:
		error_tok(node->tok, "invalid expression");
		break;
	}
}

static void gen_stmt(struct Node *node)
{
	// .loc $file-index $line-number
	println("\t.loc 1 %d", node->tok->line_no);

	int c;

	switch (node->kind) {
	case ND_IF:
		c = count();

		debug("\t# ND_IF");
		gen_expr(node->cond);
		cmp_zero(node->cond->ty);
		println("\tbnez a0, else.%d", c);

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
			cmp_zero(node->cond->ty);
			println("\tbnez a0, %s", node->brk_label);
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
		cmp_zero(node->cond->ty);
		println("\tbeqz a0, begin.%d", c);

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
				char c = var->init_data[pos++];

				if (' ' <= c && c <= '~')
					println("\t.byte %d\t# '%c'", c, c);
				else
					println("\t.byte %d", c);
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

static void store_fltargs(int r, int offset, int sz)
{
	switch (sz) {
	case sizeof(float):
		println("\tfsw %s, %d(sp)", argflt[r], offset);
		break;

	case sizeof(double):
		println("\tfsd %s, %d(sp)", argflt[r], offset);
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
		debug("\t# '%s' save args into stack", fn->name);

		uint32_t g_arg = 0, f_arg = 0;
		for (struct Obj *var = fn->params; var; var = var->next) {
			if (is_float(var->ty))
				store_fltargs(f_arg++, var->offset, var->ty->size);
			else
				store_args(g_arg++, var->offset, var->ty->size);
		}

		// Save arg registers if function is variadic
		if (fn->va_area) {
			debug("\t# '%s' save variadic args into stack", fn->va_area->name);

			// store "__va_area__"(local variable) into stack
			int off = fn->va_area->offset;

			while (g_arg < ARRAY_SIZE(argreg)) {
				store_args(g_arg++, off, sizeof(long));
				off += sizeof(long);
			}

			debug("\t# end '%s' save variadic args into stack", fn->va_area->name);
		}

		if (beyond_instruction_offset(-fn->stack_size)) {
			println("\tli t0, -%d", fn->stack_size);
			println("\tadd sp, sp, t0");
		} else {
			println("\tadd sp, sp, -%d", fn->stack_size);
		}

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
