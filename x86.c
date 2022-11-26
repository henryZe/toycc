#include <toycc.h>

static char *argreg8[] = {
	"%dil",
	"%sil",
	"%dl",
	"%cl",
	"%r8b",
	"%r9b"
};

const char * const argreg[] = {
	"%rdi",
	"%rsi",
	"%rdx",
	"%rcx",
	"%r8",
	"%r9",
};

const char *retreg = "%rax";

static int depth = 0;

// push reg into 0(sp)
void push(const char *reg)
{
	println("\tpush %s", reg);
	depth++;
}

// pop 0(sp) to reg
void pop(const char *reg)
{
	println("\tpop %s", reg);
	depth--;
}

void prologue(struct Obj *fn)
{
	// save rbp
	println("\tpush %%rbp");
	// save rsp to rbp
	println("\tmov %%rsp, %%rbp");
	// expand rsp
	println("\tsub $%d, %%rsp", fn->stack_size);

	int i = 0;
	// Save passed-by-register arguments to the stack
	for (struct Obj *var = fn->params; var; var = var->next) {
		if (var->ty->size == sizeof(long))
			println("\tmov %s, %d(%%rbp)", argreg[i++], var->offset);
		else
			println("\tmov %s, %d(%%rbp)", argreg8[i++], var->offset);
	}
}

void epilogue(void)
{
	assert(!depth);
	// restore rsp register
	println("\tmov %%rbp, %%rsp");
	// restore rbp register
	println("\tpop %%rbp");
	// pop ip
	println("\tret");
}

static void addnum(const char *dst, const char *src, int num)
{
	println("\tadd %s, %s, %d", dst, src, num);
}

void load_local_var(int stack_offset)
{
	addnum(argreg[0], "fp", stack_offset);
}

void load_global_var(const char *name)
{
	loadaddr(argreg[0], name);
}

void jmp(const char *symbol)
{
	println("\tjmp %s", symbol);
}
