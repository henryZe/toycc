#include <toycc.h>

int main(int argc, char **argv)
{
	if (argc != 2)
		error("%s: invalid number of arguments", argv[0]);

	// Tokenize and parse
	struct Token *tok = tokenize(argv[1]);
	struct Obj *prog = parser(tok);
	codegen(prog);

	return 0;
}
