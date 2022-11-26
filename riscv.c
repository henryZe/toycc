#include <toycc.h>

const char * const argreg[] = {
	"a0",
	"a1",
	"a2",
	"a3",
	"a4",
	"a5",
};

const char *retreg = "a0";

static int depth = 0;

static void addnum(const char *dst, const char *src, int num)
{
	println("\tadd %s, %s, %d", dst, src, num);
}

// push reg into 0(sp)
void push(const char *reg)
{
	println("\taddi sp, sp, -%ld", sizeof(long));
	println("\tsd %s, 0(sp)", reg);
	depth++;
}

// pop 0(sp) to reg
void pop(const char *reg)
{
	println("\tld %s, 0(sp)", reg);
	println("\taddi sp, sp, %ld", sizeof(long));
	depth--;
}

void prologue(struct Obj *fn)
{
	// save fp register
	push("fp");
	// save ra register
	push("ra");
	// save sp register
	println("\tmv fp, sp");
	// expand sp
	addnum("sp", "sp", -fn->stack_size);

	int i = 0;
	// Save passed-by-register arguments to the stack
	for (struct Obj *var = fn->params; var; var = var->next) {
		if (var->ty->size == sizeof(long))
			println("\tsd %s, %d(fp)", argreg[i++], var->offset);
		else
			println("\tsb %s, %d(fp)", argreg[i++], var->offset);
	}
}

void epilogue(void)
{
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

void loadnum(int num)
{
	println("\tli a0, %d", num);
}

void neg(const char *dst, const char *src)
{
	println("\tneg %s, %s", dst, src);
}

void addreg(const char *dst, const char *src)
{
	println("\tadd %s, %s, %s", dst, dst, src);
}

void subreg(const char *dst, const char *src)
{
	println("\tsub %s, %s, %s", dst, dst, src);
}

void mulreg(const char *dst, const char *src)
{
	println("\tmul %s, %s, %s", dst, dst, src);
}

void divreg(const char *dst, const char *src)
{
	println("\tdiv %s, %s, %s", dst, dst, src);
}

void eq(const char *dst, const char *src)
{
	println("\txor %s, %s, %s", dst, dst, src);
	println("\tseqz %s, %s", dst, dst);
}

void neq(const char *dst, const char *src)
{
	println("\txor %s, %s, %s", dst, dst, src);
	println("\tsnez %s, %s", dst, dst);
}

void less_than(const char *dst, const char *src)
{
	println("\tslt %s, %s, %s", dst, dst, src);
}

void less_equal(const char *dst, const char *src)
{
	println("\tslt %s, %s, %s", dst, src, dst);
	println("\tseqz %s, %s", dst, dst);
}

void load_asm(int size)
{
	if (size == sizeof(long))
		println("\tld a0, (a0)");
	else
		println("\tlb a0, (a0)");
}

void store_asm(int size)
{
	pop("a1");

	if (size == sizeof(long))
		println("\tsd a0, (a1)");
	else
		println("\tsb a0, (a1)");
}

void loadaddr(const char *dst, const char *symbol)
{
	println("\tla %s, %s", dst, symbol);
}

void jmp(const char *symbol)
{
	println("\tj %s", symbol);
}

void call(const char *symbol)
{
	println("\tcall %s", symbol);
}

void load_local_var(int stack_offset)
{
	addnum(retreg, "fp", stack_offset);
}

void load_global_var(const char *name)
{
	loadaddr(retreg, name);
}
