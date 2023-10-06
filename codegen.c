#include <toycc.h>
#include <type.h>

#ifdef DEBUG
#define debug(fmt, args...) println("\t# " fmt, ##args)
#else
#define debug(...)
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

#define MAX_ARG_REGS ARRAY_SIZE(argreg)

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

	if ((reg[0] == 'f') && strcmp(reg, "fp"))
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

__attribute__((unused))
static void absolute_addressing(const char *symbol)
{
	// HI20
	println("\tlui a0, %%hi(%s)", symbol);
	// LO12
	println("\taddi a0, a0, %%lo(%s)", symbol);
}

static void relative_addressing(const char *symbol)
{
	int c = count();
	println(".L.pcrel%d:", c);

	// HI20
	println("\tauipc a0, %%pcrel_hi(%s)", symbol);
	// LO12
	println("\taddi a0, a0, %%pcrel_lo(.L.pcrel%d)", c);
}

static void GOT_relative_addressing(const char *symbol)
{
	int c = count();
	println(".L.pcrel%d:", c);

	// HI20
	println("\tauipc a0, %%got_pcrel_hi(%s)", symbol);
	// LO12, reuse %pcrel_lo(label) for its lower half
	println("\tld a0, %%pcrel_lo(.L.pcrel%d)(a0)", c);
}

__attribute__((unused))
static void pseudo_addressing(const char *symbol)
{
	println("\tla a0, %s", symbol);
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(struct Node *node)
{
	switch (node->kind) {
	case ND_VAR:
		// local variable
		if (node->var->is_local) {
			if (beyond_instruction_offset(node->var->offset)) {
				println("\tli t0, %d", node->var->offset);
				println("\tadd a0, fp, t0");
			} else {
				println("\tadd a0, fp, %d", node->var->offset);
			}
			break;
		}

		// Here, we generate an absolute address of a function or a global
		// variable. Even though they exist at a certain address at runtime,
		// their addresses are not known at link-time for the following
		// two reasons.
		//
		//  - Address randomization: Executables are loaded to memory as a
		//    whole but it is not known what address they are loaded to.
		//    Therefore, at link-time, relative address in the same
		//    executable (i.e. the distance between two functions in the
		//    same executable) is known, but the absolute address is not
		//    known.
		//
		//  - Dynamic linking: Dynamic shared objects (DSOs) or .so files
		//    are loaded to memory alongside an executable at runtime and
		//    linked by the runtime loader in memory. We know nothing
		//    about addresses of global stuff that may be defined by DSOs
		//    until the runtime relocation is complete.
		//
		// In order to deal with the former case, we use relative
		// addressing, denoted by `jal symbol` (here is `jalr a0`).
		//
		// For the latter, we obtain an address of a stuff that may be in
		// a shared object file from the Global Offset Table using
		// `got_pcrel_hi(symbol)` notation.

		// function
		if (node->ty->kind == TY_FUNC) {
			if (node->var->is_definition) {
				// relative address
				debug("function call by relative address '%s'",
					node->var->name);
				relative_addressing(node->var->name);

			} else {
				// dynamic linking, from .so files
				debug("function call from DSOs '%s'",
					node->var->name);
				GOT_relative_addressing(node->var->name);
			}
			break;
		}

		// global variable
		debug("global variable '%s'", node->var->name);
		GOT_relative_addressing(node->var->name);
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

	case ND_FUNCALL:
		if (node->ret_buffer)
			gen_expr(node);
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
	case TY_FUNC:
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

#define combine(x, y) (x"\n"y)

// signed => shift right Arithmetic
#define TOI8   "\tslli a0, a0, 56\n\tsrai a0, a0, 56"
#define TOI16  "\tslli a0, a0, 48\n\tsrai a0, a0, 48"
#define TOI32  "\tslli a0, a0, 32\n\tsrai a0, a0, 32"

// unsigned => shift right Logical
#define TOU8   "\tslli a0, a0, 56\n\tsrli a0, a0, 56"
#define TOU16  "\tslli a0, a0, 48\n\tsrli a0, a0, 48"
#define TOU32  "\tslli a0, a0, 32\n\tsrli a0, a0, 32"

#define I32F32 "\tfcvt.s.w fa0, a0"
#define I32F64 "\tfcvt.d.w fa0, a0"
#define I64F32 "\tfcvt.s.l fa0, a0"
#define I64F64 "\tfcvt.d.l fa0, a0"
#define U32F32 "\tfcvt.s.wu fa0, a0"
#define U32F64 "\tfcvt.d.wu fa0, a0"
#define U64F32 "\tfcvt.s.lu fa0, a0"
#define U64F64 "\tfcvt.d.lu fa0, a0"
#define F32I32 "\tfcvt.w.s a0, fa0, rtz"
#define F32I8  combine(F32I32, TOI8)
#define F32I16 combine(F32I32, TOI16)
#define F32I64 "\tfcvt.l.s a0, fa0, rtz"
#define F32U32 "\tfcvt.wu.s a0, fa0, rtz"
#define F32U8  combine(F32U32, TOU8)
#define F32U16 combine(F32U32, TOU16)
#define F32U64 "\tfcvt.lu.s a0, fa0, rtz"
#define F32F64 "\tfcvt.d.s fa0, fa0"
#define F64I32 "\tfcvt.w.d a0, fa0, rtz"
#define F64I8  combine(F64I32, TOI8)
#define F64I16 combine(F64I32, TOI16)
#define F64I64 "\tfcvt.l.d a0, fa0, rtz"
#define F64U32 "\tfcvt.wu.d a0, fa0, rtz"
#define F64U8  combine(F64U32, TOU8)
#define F64U16 combine(F64U32, TOU16)
#define F64U64 "\tfcvt.lu.d a0, fa0, rtz"
#define F64F32 "\tfcvt.s.d fa0, fa0"

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
		debug("cast t1 %d t2 %d", t1, t2);
		println("%s", cast_matrix[t1][t2]);
		debug("end cast");
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

// Structs or unions equal or smaller than 16 bytes are passed
// using up to two registers.
// When structs or unions larger than 16 bytes, save the struct
// in caller's stack, and transmit the pointer to callee by one
// register.
static void push_struct(struct Type *ty)
{
	int i;
	int sz = align_to(ty->size, sizeof(long));
	int n = sz / sizeof(long);

	// transmission by registers
	if (n <= 2) {
		while (n--) {
			println("\tld t0, %d(a0)", n * sizeof(long));
			push("t0");
		}
		return;
	}

	// transmission by stack pointer
	println("\tadd t1, t1, %d", -sz);

	for (i = 0; i < n; i++) {
		// push struct content into stack
		println("\tld t0, %d(a0)", i * sizeof(long));
		println("\tsd t0, %d(t1)", i * sizeof(long));
	}
	depth += n;

	push("t1");
}

static void push_args2(struct Node *args, bool first_pass)
{
	if (!args)
		return;

	// in the right-to-left order
	push_args2(args->next, first_pass);

	if ((first_pass && !args->pass_by_stack) ||
	   (!first_pass && args->pass_by_stack))
		return;

	gen_expr(args);

	switch (args->ty->kind) {
	case TY_STRUCT:
	case TY_UNION:
		push_struct(args->ty);
		break;

	case TY_FLOAT:
	case TY_DOUBLE:
		push("fa0");
		break;

	default:
		push("a0");
		break;
	}
}

#define USED_GENERIC_REG 1
#define USED_FLOAT_REG 2
#define MIXED_REG 3

static void check_struct_mixed(struct Type *ty, int *mixed, int *idx)
{
	if (ty->kind == TY_STRUCT) {
		for (struct Member *mem = ty->members; mem; mem = mem->next)
			check_struct_mixed(mem->ty, mixed, idx);
		return;
	}

	if (ty->kind == TY_ARRAY) {
		for (int i = 0; i < ty->array_len; i++)
			check_struct_mixed(ty->base, mixed, idx);
		return;
	}

	if (is_float(ty)) {
		*mixed |= USED_FLOAT_REG;
		(*idx)++;
		return;
	}

	*mixed |= USED_GENERIC_REG;
	(*idx)++;
}

static void check_struct_contain_float(struct Token *tok,
				       struct Type *ty, size_t g_arg)
{
	int n = align_to(ty->size, sizeof(long)) / sizeof(long);
	if (n > 2)
		return;

	int mixed = 0, idx = 0;
	check_struct_mixed(ty, &mixed, &idx);
	if (mixed != USED_GENERIC_REG && idx == 2) {
		if (g_arg < MAX_ARG_REGS) {
			// transmit by register
			if ((g_arg + 1) == MAX_ARG_REGS && n == 1)
				// pass as the last argument by generic register
				return;

			error_tok(tok, "Not support transmit struct arguments's float member by float register");
		}
	}
}

// Load function call arguments. Arguments are already evaluated and
// stored to the stack as local variables. What we need to do in this
// function is to load them to registers or push them to the stack.
// https://github.com/riscv-non-isa/riscv-elf-psabi-doc/releases
//
// Here is what the spec says:
// - Up to 8 arguments of integral type are passed using a0-a7.
//
// - Up to 8 arguments of floating-point type are passed using fa0-fa7.
//
// - Values are passed in floating-point registers whenever possible,
//   whether or not the integer registers have been exhausted.
//
// - Variadic arguments are passed according to the integer calling
//   convention.
//
// - If all registers of an appropriate type are already used, push an
//   argument to the stack in the right-to-left order.
//   float -> integer -> stack
//
// - Each argument passed on the stack takes 8 bytes, and the end of
//   the argument area must be aligned to a 16 byte boundary.
//
// +---------------------------------------+
// |       struct or union in stack        |
// |---------------------------------------|<--- t1 <---+
// |                  ...                  |            |
// |      other args passed by stack       |            |
// |                  ...                  |            |
// |---------------------------------------|            |
// | arg-pointers forwards struct or union |------------+
// +---------------------------------------+ <--- sp
static size_t push_args(struct Node *node)
{
	size_t stack = 0, struct_stack = 0, g_arg = 0, f_arg = 0;
	struct Type *cur_params = node->func_ty->params;

	// If the return type is a large struct/union, the caller passes
	// a pointer to a buffer as if it were the first argument.
	if (node->ret_buffer && node->ty->size > 2 * (int)sizeof(long))
		g_arg++;

	// Load as many arguments to the registers as possible.
	for (struct Node *arg = node->args; arg; arg = arg->next) {
		if (node->func_ty->is_variadic && cur_params == NULL) {
			// this parameter is variadic
			if (g_arg < MAX_ARG_REGS) {
				g_arg++;

			} else {
				arg->pass_by_stack = true;
				stack++;
			}
			continue;
		}

		cur_params = cur_params->next;

		if (is_struct_union(arg->ty)) {
			check_struct_contain_float(arg->tok, arg->ty, g_arg);

			int n = align_to(arg->ty->size, sizeof(long)) / sizeof(long);
			if (n <= 2) {
				// transmission struct by stack or register(s)
				while (n--) {
					if (g_arg < MAX_ARG_REGS)
						g_arg++;
					else
						stack++;
				}
			} else {
				// reserve stack space for the struct
				struct_stack += n;
				stack += n;

				// push stack pointer
				if (g_arg < MAX_ARG_REGS)
					g_arg++;
				else
					stack++;
			}
			continue;
		}

		if (is_float(arg->ty) && (f_arg < MAX_ARG_REGS)) {
			f_arg++;

		} else if (g_arg < MAX_ARG_REGS) {
			g_arg++;

		} else {
			arg->pass_by_stack = true;
			stack++;
		}
	}

	// expand space for struct or union in stack
	println("\tmv t1, sp");
	println("\tadd sp, sp, -%d", struct_stack * sizeof(long));

	// push stack arguments
	push_args2(node->args, true);
	// push register arguments
	push_args2(node->args, false);

	// If the return type is a large struct/union, the caller passes
	// a pointer to a buffer as if it were the first argument.
	if (node->ret_buffer && node->ty->size > 2 * (int)sizeof(long)) {
		println("\tadd a0, fp, %d", node->ret_buffer->offset);
		push("a0");
	}

	return stack;
}

static void copy_ret_buffer(struct Obj *var)
{
	struct Type *ty = var->ty;

	debug("copy_ret_buffer size %d", ty->size);

	for (int i = 0; i < MIN((int)sizeof(long), ty->size); i++) {
		println("\tsb a0, %d(fp)", var->offset + i);
		println("\tsrl a0, a0, 8");
	}

	if (ty->size > (int)sizeof(long)) {
		for (int i = 8; i < MIN(16, ty->size); i++) {
			println("\tsb a1, %d(fp)", var->offset + i);
			println("\tsrl a1, a1, 8");
		}
	}

	debug("copy_ret_buffer end");
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
	println("\t.loc %d %d", node->tok->file->file_no,
				node->tok->line_no);

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
		gen_addr(node);
		load(node->ty);
		return;

	case ND_MEMBER:
		gen_addr(node);
		load(node->ty);

		struct Member *mem = node->member;
		if (mem->is_bitfield) {
			// Clear unused high bits of field member variables
			println("\tslli a0, a0, %d", 64 - mem->bit_width - mem->bit_offset);
			// Clear unused low bits of field member variables
			if (mem->ty->is_unsigned)
				println("\tsrli a0, a0, %d", 64 - mem->bit_width);
			else
				println("\tsrai a0, a0, %d", 64 - mem->bit_width);
		}
		return;

	case ND_DEREF:
		debug("ND_DEREF load");
		gen_expr(node->lhs);
		load(node->ty);
		debug("end ND_DEREF load");
		return;

	case ND_ADDR:
		debug("ND_ADDR var");
		gen_addr(node->lhs);
		debug("end ND_ADDR var");
		return;

	case ND_ASSIGN:
		debug("ND_ASSIGN var");
		gen_addr(node->lhs);
		push("a0");
		gen_expr(node->rhs);

		if (node->lhs->kind == ND_MEMBER &&
		    node->lhs->member->is_bitfield) {
			// If the lhs is a bitfield, we need to read
			// the current value from memory and merge it
			// with a new value.
			struct Member *mem = node->lhs->member;

			debug("merge new value into bit_field");

			println("\tmv t0, a0");
			println("\tli t1, %ld", (1L << mem->bit_width) - 1);
			println("\tand t0, t0, t1");

			println("\tslli t0, t0, %d", mem->bit_offset);

			// Load the address where the bit field value is saved in.
			println("\tld a0, (sp)");
			load(mem->ty);

			long mask = ((1L << mem->bit_width) - 1) << mem->bit_offset;
			println("\tli t1, %ld", ~mask);

			println("\tand a0, a0, t1");
			println("\tor a0, a0, t0");

			debug("merge new value into bit_field end");
		}

		store(node->ty);
		debug("end ND_ASSIGN var");
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
		debug("ND_MEMZERO size %d", node->var->ty->size);
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
		debug("end ND_MEMZERO");
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
		debug("ND_FUNCALL");

		// push arguments into stack first
		int stack_args = push_args(node);

		// fetch function address
		gen_expr(node->lhs);
		println("\tmv t0, a0");

		struct Type *cur_params = node->func_ty->params;
		size_t g_arg = 0, f_arg = 0;

		// If the return type is a large struct/union, the caller passes
		// a pointer to a buffer as if it were the first argument.
		if (node->ret_buffer && node->ty->size > (int)sizeof(long) * 2) {
			debug("pop struct's pointer to a0");
			pop(argreg[g_arg++]);
		}

		// then pop arguments from stack
		for (struct Node *arg = node->args; arg; arg = arg->next) {
			debug("%sarg %.*s",
			      node->func_ty->is_variadic ? "variadic " : "",
			      arg->tok->len, arg->tok->loc);

			// transfer args to variadic function with generic registers
			if (node->func_ty->is_variadic && cur_params == NULL) {
				if (g_arg < MAX_ARG_REGS)
					pop(argreg[g_arg++]);
				continue;
			}

			cur_params = cur_params->next;

			if (is_struct_union(arg->ty)) {
				int n = align_to(arg->ty->size, sizeof(long)) / sizeof(long);

				// transmission by register(s) or stack
				if (n <= 2) {
					while (n--) {
						if (g_arg >= MAX_ARG_REGS)
							break;
						pop(argreg[g_arg++]);
					}
					continue;
				}
			}

			if (is_float(arg->ty) && (f_arg < MAX_ARG_REGS))
				pop(argflt[f_arg++]);

			else if (g_arg < MAX_ARG_REGS)
				pop(argreg[g_arg++]);
		}

		// call function
		println("\tjalr t0");

		if (stack_args) {
			println("\taddi sp, sp, %d", stack_args * sizeof(long));
			depth -= stack_args;
		}

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

		// If the return type is a small struct, a value is returned
		// using up to two registers.
		if (node->ret_buffer && node->ty->size <= 2 * (int)sizeof(long)) {
			copy_ret_buffer(node->ret_buffer);
			debug("mv struct's pointer to a0");
			println("\tadd a0, fp, %d", node->ret_buffer->offset);
		}

		debug("end ND_FUNCALL");
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

static void copy_struct_reg(void)
{
	int i, pos, sz;
	struct Type *ty = current_fn->ty->return_ty;

	debug("copy_struct_reg");

	println("\tmv t0, a0");
	// load instruction(like lb, lh, lw) would clear invalid high bits
	for (i = 0, pos = 0; pos < ty->size; i++, pos += sz) {
		sz = MIN((int)sizeof(long), ty->size);

		println("\tli a%d, 0", i);
		switch (sz) {
		case 1:
			println("\tlb a%d, %d(t0)", i, pos);
			break;

		case 2:
			println("\tlh a%d, %d(t0)", i, pos);
			break;

		case 3:
		case 4:
			println("\tlw a%d, %d(t0)", i, pos);
			break;

		default:
			println("\tld a%d, %d(t0)", i, pos);
			break;
		}
	}

	debug("copy_struct_reg end");
}

static void copy_struct_mem(void)
{
	struct Type *ty = current_fn->ty->return_ty;
	struct Obj *var = current_fn->params;

	debug("copy_struct_mem");

	debug("get struct's pointer passed by caller");
	println("\tld a1, %d(fp)", var->offset);

	for (int i = 0; i < ty->size; i++) {
		println("\tlb t0, %d(a0)", i);
		println("\tsb t0, %d(a1)", i);
	}

	debug("return struct's pointer by a0");
	println("\tmv a0, a1");

	debug("copy_struct_mem end");
}

static void gen_stmt(struct Node *node)
{
	int c;

	// .loc $file-index $line-number
	println("\t.loc %d %d", node->tok->file->file_no,
				node->tok->line_no);

	switch (node->kind) {
	case ND_IF:
		c = count();

		debug("ND_IF");
		gen_expr(node->cond);
		cmp_zero(node->cond->ty);
		println("\tbnez a0, else.%d", c);

		gen_stmt(node->then);
		println("\tj end.%d", c);

		println("else.%d:", c);
		if (node->els)
			gen_stmt(node->els);

		println("end.%d:", c);
		debug("end ND_IF");
		return;

	case ND_FOR:
		c = count();

		debug("ND_FOR");
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
		debug("end ND_FOR");
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
		if (node->lhs) {
			gen_expr(node->lhs);

			struct Type *ty = node->lhs->ty;
			if (is_struct_union(ty)) {
				if (ty->size <= (int)sizeof(long) * 2)
					copy_struct_reg();
				else
					copy_struct_mem();
			}
		}
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

static void assign_lvar_offsets(struct Obj *prog)
{
	for (struct Obj *fn = prog; fn; fn = fn->next) {
		if (!fn->is_function)
			continue;

		// If a function has many parameters, some parameters are
		// inevitably passed by stack rather than by register.
		// The first passed-by-stack parameter resides at fp+16.
		// +----------------+
		// |    va_area?    |
		// +----------------+
		// |   stack args   | (NR*8)			[caller]
		// +----------------+ top: stack's first arg (fp+16) --> sp
		// |       ra       |
		// |       fp       |				[callee]
		// +----------------+ bottom (fp)
		// |   local vars   |
		// +----------------+
		int top = 16;

		size_t g_arg = 0, f_arg = 0;
		// initialize pass-by-stack parameters' offset
		for (struct Obj *var = fn->params; var; var = var->next) {
			if (is_struct_union(var->ty)) {
				int sz = align_to(var->ty->size, sizeof(long));
				int n = sz / sizeof(long);

				if (n <= 2) {
					if ((g_arg + n) <= MAX_ARG_REGS) {
						g_arg += n;
						continue;
					} else if (g_arg + 1 == MAX_ARG_REGS) {
						error_tok(var->ty->name_pos,
							"Not support transmit struct parameter half by register and half by stack");
					}
				} else {
					// Passed by caller stack, so just skip the register.
					if (g_arg < MAX_ARG_REGS)
						g_arg++;
				}
			} else if (is_float(var->ty)) {
				if (f_arg < MAX_ARG_REGS) {
					f_arg++;
					continue;

				} else if (g_arg < MAX_ARG_REGS) {
					g_arg++;
					continue;
				}
			} else {
				if (g_arg < MAX_ARG_REGS) {
					g_arg++;
					continue;
				}
			}

			top = align_to(top, sizeof(long));
			var->offset = top;
			debug("%s's parameter %s offset %d",
				fn->name, var->name, var->offset);
			top += var->ty->size;
		}

		if (fn->va_area) {
			top = align_to(top, sizeof(long));
			fn->va_area->offset = top;
		}

		int bottom = 0;
		// initialize var's offset
		// Assign offsets to pass-by-register parameters and local variables.
		for (struct Obj *var = fn->locals; var; var = var->next) {
			if (var->offset)
				continue;

			bottom += var->ty->size;
			bottom = align_to(bottom, var->align);
			var->offset = -bottom;
		}
		// initialize stack size
		fn->stack_size = align_to(bottom, sizeof(long));
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
				pos += sizeof(long);
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
	const char *rs = "sp";

	if (beyond_instruction_offset(offset)) {
		println("\tli t0, %d", offset);
		println("\tadd t0, sp, t0");
		rs = "t0";
		offset = 0;
	}

	switch (sz) {
	case sizeof(char):
		println("\tsb %s, %d(%s)", argreg[r], offset, rs);
		break;

	case sizeof(short):
		println("\tsh %s, %d(%s)", argreg[r], offset, rs);
		break;

	case sizeof(int):
		println("\tsw %s, %d(%s)", argreg[r], offset, rs);
		break;

	case sizeof(long):
		println("\tsd %s, %d(%s)", argreg[r], offset, rs);
		break;

	default:
		for (int i = 0; i < sz; i++) {
			println("\tsb %s, %d(%s)", argreg[r], offset + i, rs);
			println("\tsrli %s, %s, 8", argreg[r], argreg[r]);
		}
		break;
	}
}

static void store_fltargs(size_t r, int offset, int sz)
{
	assert(r < MAX_ARG_REGS);

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
		debug("Prologue");

		int va_size = 0;
		if (fn->va_area) {
			size_t va_gp = 0, va_fp = 0;

			for (struct Obj *var = fn->params; var; var = var->next) {
				// count all used registers by all type parameters
				struct Type *ty = var->ty;

				switch (ty->kind) {
				case TY_STRUCT:
				case TY_UNION:
					error_tok(var->ty->name_pos,
						 "Not support transmit struct or union parameter in variadic function");
					break;

				case TY_FLOAT:
				case TY_DOUBLE:
					va_fp < MAX_ARG_REGS ? va_fp++ : va_gp++;
					break;

				default:
					va_gp++;
					break;
				}
			}

			// Expand the space only when variadic parameters
			// are transmitted by registers.
			if (va_gp < MAX_ARG_REGS) {
				va_size = (MAX_ARG_REGS - va_gp) * sizeof(long);
				debug("va_area's size is %d", va_size);
				println("\tadd sp, sp, -%d", va_size);
			}
		}

		push("ra");
		push("fp");
		println("\tmv fp, sp");
		debug("Prologue end");

		// Save passed-by-register arguments to the stack
		debug("'%s' save args into stack", fn->name);

		size_t g_arg = 0, f_arg = 0;
		for (struct Obj *var = fn->params; var; var = var->next) {
			// pass-by-stack parameters are already in stack now
			if (var->offset > 0) {
				if (g_arg < MAX_ARG_REGS) {
					// Skip the argument register,
					// only when struct's size > (2 * sizeof(long))
					assert(is_struct_union(var->ty));
					g_arg++;
				}

			} else if (is_struct_union(var->ty)) {
				store_args(g_arg++, var->offset,
					   MIN(var->ty->size, (int)sizeof(long)));

				if (var->ty->size > (int)sizeof(long))
					store_args(g_arg++, var->offset + sizeof(long),
							    var->ty->size - sizeof(long));

			} else if (is_float(var->ty) && (f_arg < MAX_ARG_REGS)) {
				store_fltargs(f_arg++, var->offset, var->ty->size);

			} else if (g_arg < MAX_ARG_REGS) {
				store_args(g_arg++, var->offset, var->ty->size);
			}
		}

		// Save arg registers if function is variadic
		if (fn->va_area) {
			debug("'%s' save variadic args into stack",
				fn->va_area->name);

			// store "__va_area__"(local variable) into stack
			int off = fn->va_area->offset;
			debug("va_area->offset %d", fn->va_area->offset);

			while (g_arg < MAX_ARG_REGS) {
				store_args(g_arg++, off, sizeof(long));
				off += sizeof(long);
			}

			debug("end '%s' save variadic args into stack",
				fn->va_area->name);
		}

		if (beyond_instruction_offset(-fn->stack_size)) {
			println("\tli t0, -%d", fn->stack_size);
			println("\tadd sp, sp, t0");
		} else {
			println("\tadd sp, sp, -%d", fn->stack_size);
		}

		debug("'%s' save args end", fn->name);

		int pre_depth = depth;
		// Emit code
		gen_stmt(fn->body);
		if (depth != pre_depth) {
			printf("pre_depth: %d != depth: %d\n",
				pre_depth, depth);
			assert(depth == pre_depth);
		}

		// epilogue
		debug("epilogue");
		println("return.%s:", fn->name);
		// restore sp register
		println("\tmv sp, fp");
		// restore fp register
		pop("fp");
		// restore ra register
		pop("ra");

		// return the space reserved for va_area
		if (fn->va_area && va_size) {
			debug("return va_area's size is %d", va_size);
			println("add sp, sp, %d", va_size);
		}

		// mv ra to pc
		println("\tret");
		debug("epilogue end");

		assert(!depth);
	}
}

// Traverse the AST to emit assembly.
void codegen(struct Obj *prog, FILE *out)
{
	output_file = out;

	struct File **files = get_input_files();
	for (int i = 0; files[i]; i++)
		println(".file %d \"%s\"",
			files[i]->file_no, files[i]->name);

	assign_lvar_offsets(prog);
	emit_data(prog);
	emit_text(prog);
}
