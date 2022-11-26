#include <toycc.h>

static const char *opt_o;
static const char *input_path;

static void usage(int status)
{
	fprintf(stderr, "toycc [ -o <path> ] <file>\n");
	exit(status);
}

static void parse_args(int argc, const char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--help"))
			usage(0);

		if (!strcmp(argv[i], "-o")) {
			if (!argv[++i])
				usage(1);
			opt_o = argv[i];
			continue;
		}

		if (!strncmp(argv[i], "-o", 2)) {
			opt_o = argv[i] + 2;
			continue;
		}

		if (argv[i][0] == '-' && argv[i][1] != '\0')
			error("unknown argument: %s", argv[i]);

		input_path = argv[i];
	}

	if (!input_path)
		error("no input files");
}

static FILE *open_file(const char *path)
{
	if (!path || !strcmp(path, "-"))
		return stdout;

	FILE *out = fopen(path, "w");
	if (!out)
		error("cannot open output file: %s: %s", path, strerror(errno));

	return out;
}

int main(int argc, const char **argv)
{
	parse_args(argc, argv);

	// Tokenize and parse
	struct Token *tok = tokenize_file(input_path);
	struct Obj *prog = parser(tok);

	FILE *out = open_file(opt_o);
	fprintf(out, ".file 1 \"%s\"\n", input_path);

	codegen(prog, out);
	// do not close if stdout

	return 0;
}
