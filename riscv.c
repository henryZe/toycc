#include <toycc.h>

const char * const argreg[] = {
	"a0",
	"a1",
	"a2",
	"a3",
	"a4",
	"a5",
};

static int depth = 0;

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

void prologue(void)
{
	// save fp register
	push("fp");
	// save ra register
	push("ra");
	// save sp register
	println("\tmv fp, sp");
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

void loadnum(const char *dst, int num)
{
	println("\tli %s, %d", dst, num);
}

void neg(const char *dst, const char *src)
{
	println("\tneg %s, %s", dst, src);
}

void addnum(const char *dst, const char *src, int num)
{
	println("\tadd %s, %s, %d", dst, src, num);
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

void loadb(const char *reg1, const char *reg2, int offset)
{
	println("\tlb %s, %d(%s)", reg1, offset, reg2);
}

void loadd(const char *reg1, const char *reg2, int offset)
{
	println("\tld %s, %d(%s)", reg1, offset, reg2);
}

void storeb(const char *reg1, const char *reg2, int offset)
{
	println("\tsb %s, %d(%s)", reg1, offset, reg2);
}

void stored(const char *reg1, const char *reg2, int offset)
{
	println("\tsd %s, %d(%s)", reg1, offset, reg2);
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
