#include "test.h"

char *asm_fn1(void)
{
	asm("li a0, 50\n"
	    "mv sp, fp\n"
	    "ld	s0, 0(sp)\n"
	    "add sp, sp, 8\n"
	    "ld	ra, 0(sp)\n"
	    "add sp, sp, 8\n"
	    "ret");
}

char *asm_fn2(void)
{
	asm inline volatile("li a0, 55\n"
			    "mv sp, fp\n"
			    "ld	s0, 0(sp)\n"
			    "add sp, sp, 8\n"
			    "ld	ra, 0(sp)\n"
			    "add sp, sp, 8\n"
			    "ret");
}

int main()
{
	ASSERT(50, asm_fn1());
	ASSERT(55, asm_fn2());

	pass();
	return 0;
}
