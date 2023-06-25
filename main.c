#include <toycc.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libgen.h>

static bool opt_S;
static bool opt_cc1;
static bool opt_hash_hash_hash;
static const char *opt_o;
static const char *input_path;
static struct StringArray tmpfiles;

static void usage(int status)
{
	fprintf(stderr, "toycc [ -o <path> ] <file>\n");
	exit(status);
}

static void parse_args(int argc, const char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-###")) {
			opt_hash_hash_hash = true;
			continue;
		}

		if (!strcmp(argv[i], "-cc1")) {
			opt_cc1 = true;
			continue;
		}

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

		if (!strcmp(argv[i], "-S")) {
			opt_S = true;
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
		error("cannot open output file: %s: %s",
			path, strerror(errno));

	return out;
}

static void run_subprocess(const char **argv)
{
	// if -### is given, dump the subprocess's command line
	if (opt_hash_hash_hash) {
		fprintf(stderr, "%s", argv[0]);
		for (int i = 1; argv[i]; i++)
			fprintf(stderr, " %s", argv[i]);
		fprintf(stderr, "\n");
	}

	if (fork() == 0) {
		// child process: run a new command
		execvp(argv[0], (char *const *)argv);
		fprintf(stderr, "exec failed: %s: %s\n",
				argv[0], strerror(errno));
		_exit(1);
	}

	// wait for the child processes to finish
	int status;
	while (wait(&status) > 0);
	if (status)
		exit(1);
}

static void run_cc1(int argc, const char **argv,
		    const char *input, const char *output)
{
	const char **args = malloc((argc + 10) * sizeof(char *));

	for (int i = 0; i < argc; i++)
		args[i] = argv[i];

	args[argc++] = "-cc1";

	if (input)
		args[argc++] = input;

	if (output) {
		args[argc++] = "-o";
		args[argc++] = output;
	}

	args[argc] = NULL;

	run_subprocess(args);
}

static void cc1(void)
{
	// Tokenize and parse
	struct Token *tok = tokenize_file(input_path);
	struct Obj *prog = parser(tok);

	FILE *out = open_file(opt_o);
	// .file $file-index $file-name
	fprintf(out, ".file 1 \"%s\"\n", input_path);

	codegen(prog, out);
	// do not close if stdout
}

static void cleanup(void)
{
	for (int i = 0; i < tmpfiles.len; i++)
		// only remove the temp files
		unlink(tmpfiles.data[i]);
}

// Replace file extension
static const char *replace_extn(const char *tmpl, const char *extn)
{
	const char *filename = basename(strdup(tmpl));
	char *dot = strrchr(filename, '.');

	if (dot)
		*dot = '\0';
	return format("%s%s", filename, extn);
}

static const char *create_tmpfile(void)
{
	// create temp file under local dir
	char *path = strdup("./toycc-tmpfile-XXXXXX");
	int fd = mkstemp(path);

	if (fd == -1)
		error("mkstemp failed: %s", strerror(errno));
	close(fd);

	strarray_push(&tmpfiles, path);
	return path;
}

static void assemble(const char *input, const char *output)
{
	// '-c':
	// Compile an assembly language source file into an object file
	// without linking it with other object files or libraries.
	const char *cmd[] = {
		"riscv64-linux-gnu-as",
		"-c",
		input,
		"-o",
		output,
		NULL,
	};
	run_subprocess(cmd);
}

int main(int argc, const char **argv)
{
	const char *output;

	atexit(cleanup);
	parse_args(argc, argv);

	if (opt_cc1) {
		cc1();
		return 0;
	}

	if (opt_o)
		output = opt_o;
	else if (opt_S)
		output = replace_extn(input_path, ".s");
	else
		output = replace_extn(input_path, ".o");

	// if -S is given, assembly text is the final output.
	if (opt_S) {
		run_cc1(argc, argv, input_path, output);
		return 0;
	}

	// Otherwise, run the assembler to assemble our output.
	const char *tmpfile = create_tmpfile();

	run_cc1(argc, argv, input_path, tmpfile);
	assemble(tmpfile, output);

	return 0;
}
