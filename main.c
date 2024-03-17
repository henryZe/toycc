#include <toycc.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libgen.h>
#include <glob.h>
#include <hashmap.h>

static bool opt_E;
static bool opt_M;
static bool opt_MD;
static bool opt_MP;
static bool opt_S;
static bool opt_c;
static bool opt_cc1;
static bool opt_hash_hash_hash;

static const char *opt_MF;
static const char *opt_MT;
static const char *opt_o;
static const char *output_file;
static const char *base_file;

static struct StringArray input_paths;
static struct StringArray tmpfiles;
static struct StringArray include_paths;
static struct StringArray opt_include;
static struct StringArray ld_extra_args;

static bool opt_fcommon = true;

enum FileType {
	FILE_NONE,
	FILE_C,
	FILE_ASM,
	FILE_OBJ,
	FILE_AR,
	FILE_DSO,
};

// FILE_NONE as default
static enum FileType opt_x;

const char *get_base_file(void)
{
	return base_file;
}

const struct StringArray *get_include_paths(void)
{
	return &include_paths;
}

bool get_opt_fcommon(void)
{
	return opt_fcommon;
}

static void usage(int status)
{
	fprintf(stderr, "toycc [ -o <path> ] <file>\n");
	exit(status);
}

static bool take_arg(const char *arg)
{
	const char *x[] = {
		"-o",
		"-I",
		"-idirafter",
		"-include",
		"-x",
		"-MF",
		"-MT",
	};

	for (size_t i = 0; i < ARRAY_SIZE(x); i++)
		if (!strcmp(arg, x[i]))
			return true;

	return false;
}

static void define(const char *str)
{
	const char *eq = strchr(str, '=');

	if (eq)
		define_macro(strndup(str, eq - str), eq + 1);
	else
		define_macro(str, "1");
}

static enum FileType parse_opt_x(const char *s)
{
	if (!strcmp(s, "c"))
		return FILE_C;

	if (!strcmp(s, "assembler"))
		return FILE_ASM;

	if (!strcmp(s, "none"))
		return FILE_NONE;

	error("<command line>: unknown argument for -x: %s", s);
}

// Generate dependency in Makefile syntax, be like:
// $(obj)foo.o -> $$(obj)foo.o
static const char *quote_makefile(const char *s)
{
	char *buf = calloc(1, strlen(s) * 2 + 1);

	for (int i = 0, j = 0; s[i]; i++) {
		switch (s[i]) {
		case '$':
			buf[j++] = '$';
			buf[j++] = '$';
			break;

		case '#':
			buf[j++] = '\\';
			buf[j++] = '#';
			break;

		case ' ':
		case '\t':
			for (int k = i - 1; k >= 0 && s[k] == '\\'; k--)
				buf[j++] = '\\';
			buf[j++] = '\\';
			buf[j++] = s[i];
			break;

		default:
			buf[j++] = s[i];
			break;
		}
	}
	return (const char *)buf;
}

static void parse_args(int argc, const char **argv)
{
	int i;

	// Make sure that all command line options that
	// take an argument have an argument.
	for (i = 1; i < argc; i++)
		if (take_arg(argv[i]))
			if (!argv[++i])
				usage(1);

	struct StringArray idirafter = {};

	for (i = 1; i < argc; i++) {
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
			opt_o = argv[++i];
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

		if (!strcmp(argv[i], "-fcommon")) {
			opt_fcommon = true;
			continue;
		}

		if (!strcmp(argv[i], "-fno-common")) {
			opt_fcommon = false;
			continue;
		}

		if (!strcmp(argv[i], "-c")) {
			opt_c = true;
			continue;
		}

		if (!strcmp(argv[i], "-E")) {
			opt_E = true;
			continue;
		}

		if (!strncmp(argv[i], "-I", 2)) {
			strarray_push(&include_paths, argv[i] + 2);
			continue;
		}

		if (!strcmp(argv[i], "-D")) {
			define(argv[++i]);
			continue;
		}

		if (!strncmp(argv[i], "-D", 2)) {
			define(argv[i] + 2);
			continue;
		}

		if (!strcmp(argv[i], "-U")) {
			undef_macro(argv[++i]);
			continue;
		}

		if (!strncmp(argv[i], "-U", 2)) {
			undef_macro(argv[i] + 2);
			continue;
		}

		if (!strcmp(argv[i], "-include")) {
			strarray_push(&opt_include, argv[++i]);
			continue;
		}

		// -x c
		if (!strcmp(argv[i], "-x")) {
			opt_x = parse_opt_x(argv[++i]);
			continue;
		}

		// -xc
		if (!strncmp(argv[i], "-x", 2)) {
			opt_x = parse_opt_x(argv[i] + 2);
			continue;
		}

		if (!strncmp(argv[i], "-l", 2)) {
			strarray_push(&input_paths, argv[i]);
			continue;
		}

		if (!strcmp(argv[i], "-s")) {
			strarray_push(&ld_extra_args, "-s");
			continue;
		}

		if (!strcmp(argv[i], "-static")) {
			strarray_push(&ld_extra_args, "-static");
			continue;
		}

		if (!strcmp(argv[i], "-M")) {
			opt_M = true;
			continue;
		}

		if (!strcmp(argv[i], "-MF")) {
			opt_MF = argv[++i];
			continue;
		}

		if (!strcmp(argv[i], "-MP")) {
			opt_MP = true;
			continue;
		}

		if (!strcmp(argv[i], "-MT")) {
			if (opt_MT == NULL)
				opt_MT = argv[++i];
			else
				// expand
				opt_MT = format("%s %s", opt_MT, argv[++i]);
			continue;
		}

		if (!strcmp(argv[i], "-MD")) {
			opt_MD = true;
			continue;
		}

		if (!strcmp(argv[i], "-MQ")) {
			if (opt_MT == NULL)
				opt_MT = quote_makefile(argv[++i]);
			else
				// expand -MT option
				opt_MT = format("%s %s", opt_MT,
						quote_makefile(argv[++i]));
			continue;
		}

		if (!strcmp(argv[i], "-cc1-input")) {
			base_file = argv[++i];
			continue;
		}

		if (!strcmp(argv[i], "-cc1-output")) {
			output_file = argv[++i];
			continue;
		}

		if (!strcmp(argv[i], "-idirafter")) {
			strarray_push(&idirafter, argv[i++]);
			continue;
		}

		if (!strcmp(argv[i], "-hashmap-test")) {
			hashmap_test();
			exit(0);
		}

		// These options are ignored for now.
		if (!strncmp(argv[i], "-O", 2) ||
		    !strncmp(argv[i], "-W", 2) ||
		    !strncmp(argv[i], "-std=", 5) ||
		    !strcmp(argv[i], "-g") ||
		    !strcmp(argv[i], "-ffreestanding") ||
		    !strcmp(argv[i], "-fno-builtin") ||
		    !strcmp(argv[i], "-fno-omit-frame-pointer") ||
		    !strcmp(argv[i], "-fno-stack-protector") ||
		    !strcmp(argv[i], "-fno-strict-aliasing") ||
		    !strcmp(argv[i], "-m64") ||
		    !strcmp(argv[i], "-mno-red-zone") ||
		    !strcmp(argv[i], "-w") ||
		    !strcmp(argv[i], "-march=native"))
			continue;

		if (argv[i][0] == '-' && argv[i][1] != '\0')
			error("unknown argument: %s", argv[i]);

		strarray_push(&input_paths, argv[i]);
	}

	for (int i = 0; i < idirafter.len; i++)
		strarray_push(&include_paths, idirafter.data[i]);

	if (input_paths.len == 0)
		error("no input files");

	// -E implies that the input is the C macro language.
	if (opt_E)
		opt_x = FILE_C;
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

	if (input) {
		args[argc++] = "-cc1-input";
		args[argc++] = input;
	}

	if (output) {
		args[argc++] = "-cc1-output";
		args[argc++] = output;
	}

	args[argc] = NULL;

	run_subprocess(args);
}

// Print tokens to stdout. Used for -E.
static void print_tokens(struct Token *tok)
{
	FILE *out = open_file(opt_o ? opt_o : "-");
	bool first_line = true;

	for (; tok->kind != TK_EOF; tok = tok->next) {
		if (!first_line && tok->at_bol)
			fprintf(out, "\n");

		if (tok->has_space && !tok->at_bol)
			fprintf(out, " ");
		fprintf(out, "%.*s", (int)tok->len, tok->loc);

		first_line = false;
	}
	fprintf(out, "\n");
}

static struct Token *must_tokenize_file(const char *path)
{
	struct Token *tok = tokenize_file(path);
	if (!tok)
		error("%s: %s", path, strerror(errno));
	return tok;
}

static struct Token *append_tokens(struct Token *tok1, struct Token *tok2)
{
	if (!tok1 || tok1->kind == TK_EOF)
		return tok2;

	struct Token *t = tok1;
	// find the last one
	while (t->next->kind != TK_EOF)
		t = t->next;

	t->next = tok2;
	return tok1;
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

// If -M options is given, the compiler write a list of input files to
// stdout in a format that "make" command can read.
// This feature is used to automate file dependency management.
static void print_dependencies(void)
{
	const char *path;
	if (opt_MF)
		path = opt_MF;
	else if (opt_MD)
		path = replace_extn(opt_o ? opt_o : base_file, ".d");
	else if (opt_o)
		path = opt_o;
	else
		path = "-";

	FILE *out = open_file(path);
	if (opt_MT)
		fprintf(out, "%s:", opt_MT);
	else
		fprintf(out, "%s:", quote_makefile(replace_extn(base_file, ".o")));

	struct File **files = get_input_files();

	for (int i = 0; files[i]; i++)
		fprintf(out, " \\\n\t%s", files[i]->name);
	fprintf(out, "\n");

	if (opt_MP)
		for (int i = 1; files[i]; i++)
			fprintf(out, "%s:\n\n", quote_makefile(files[i]->name));
}

static void cc1(void)
{
	struct Token *tok = NULL;

	// Process -include option
	for (int i = 0; i < opt_include.len; i++) {
		const char *incl = opt_include.data[i];

		const char *path;
		// current directory
		if (file_exists(incl)) {
			path = incl;

		} else {
			// -I, include directory
			path = search_include_paths(incl);
			if (!path)
				error("-include: %s: %s", incl, strerror(errno));
		}

		struct Token *tok2 = must_tokenize_file(path);
		tok = append_tokens(tok, tok2);
	}

	// Tokenize and parse.
	// current C source file
	struct Token *tok2 = must_tokenize_file(base_file);
	tok = append_tokens(tok, tok2);

	tok = preprocessor(tok);

	// If -M or -MD are given, print file dependencies.
	if (opt_M || opt_MD) {
		print_dependencies();
		if (opt_M)
			return;
		// else continue to generate target file
	}

	// if -E is given, print out preprocessed C code as a result.
	if (opt_E) {
		print_tokens(tok);
		return;
	}

	struct Obj *prog = parser(tok);

	// Open a temporary output buffer.
	char *buf;
	size_t buflen;
	FILE *output_buf = open_memstream(&buf, &buflen);

	// Traverse the AST to emit assembly.
	codegen(prog, output_buf);
	fclose(output_buf);

	// Write the assembly text to a file.
	FILE *out = open_file(output_file);
	fwrite(buf, buflen, 1, out);
	fclose(out);
}

static void cleanup(void)
{
	for (int i = 0; i < tmpfiles.len; i++)
		// only remove the temp files
		unlink(tmpfiles.data[i]);
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

static bool endswith(const char *p, const char *q)
{
	int len1 = strlen(p);
	int len2 = strlen(q);
	return (len1 >= len2) && !strcmp(p + len1 - len2, q);
}

static char *find_file(const char *pattern)
{
	char *path = NULL;
	glob_t buf = {};

	glob(pattern, 0, NULL, &buf);
	if (buf.gl_pathc)
		// select the last one
		path = strdup(buf.gl_pathv[buf.gl_pathc - 1]);

	globfree(&buf);
	return path;
}

static const char *find_libpath(void)
{
	const char *paths[] = {
		"/usr/riscv64-linux-gnu/lib/crt1.o",
		"/usr/lib/gcc-cross/riscv64-linux-gnu/*/crt1.o",
	};

	for (uint32_t i = 0; i < ARRAY_SIZE(paths); i++) {
		char *path = find_file(paths[i]);
		if (path)
			return dirname(path);
	}

	error("library path is not found");
}

static const char *find_gcc_libpath(void)
{
	const char *paths[] = {
		"/usr/lib/gcc-cross/riscv64-linux-gnu/*/crtbegin.o",
	};

	for (uint32_t i = 0; i < ARRAY_SIZE(paths); i++) {
		char *path = find_file(paths[i]);
		if (path)
			return dirname(path);
	}

	error("gcc library path is not found");
}

static void run_linker(struct StringArray *inputs, const char *output)
{
	struct StringArray arr = {};

	strarray_push(&arr, "riscv64-linux-gnu-ld");

	strarray_push(&arr, "-o");
	strarray_push(&arr, output);

	strarray_push(&arr, "-m");
	strarray_push(&arr, "elf64lriscv");

	const char *libpath = find_libpath();
	const char *gcc_libpath = find_gcc_libpath();

	// static link
	// strarray_push(&arr, "-static");
	// dynamic link as default
	strarray_push(&arr, "-dynamic-linker");
	strarray_push(&arr, format("%s/ld-linux-riscv64-lp64d.so.1", libpath));

	// boot code of C language
	strarray_push(&arr, format("%s/crt1.o", libpath));
	strarray_push(&arr, format("%s/crti.o", libpath));
	strarray_push(&arr, format("%s/crtbeginT.o", gcc_libpath));

	// specify the lib paths
	strarray_push(&arr, format("-L%s", gcc_libpath));
	strarray_push(&arr, format("-L%s", libpath));

	for (int i = 0; i < ld_extra_args.len; i++)
		strarray_push(&arr, ld_extra_args.data[i]);

	for (int i = 0; i < inputs->len; i++)
		strarray_push(&arr, inputs->data[i]);

	// link libs in the order
	strarray_push(&arr, "--start-group");
	strarray_push(&arr, "-lgcc");		// libgcc.{a, so}
	strarray_push(&arr, "-lgcc_eh");	// libgcc_eh.a
	strarray_push(&arr, "-lc");		// libc.{a, so}
	strarray_push(&arr, "--end-group");

	// only link when the lib is needed for dynamic-link
	// strarray_push(&arr, "--as-needed");
	// strarray_push(&arr, "-lgcc_s");	// libgcc_s.so
	// strarray_push(&arr, "--no-as-needed");

	strarray_push(&arr, format("%s/crtend.o", gcc_libpath));
	strarray_push(&arr, format("%s/crtn.o", libpath));

	strarray_push(&arr, NULL);
	run_subprocess(arr.data);
}

static void add_default_include_paths(const char *argv0)
{
	// We expect that toycc-specific include files
	// are installed to ./include relative to argv[0].
	strarray_push(&include_paths,
		      format("%s/include", dirname(strdup(argv0))));

	// Add standard include paths.
	strarray_push(&include_paths, "/usr/local/include");
	strarray_push(&include_paths, "/usr/riscv64-linux-gnu/include");
	strarray_push(&include_paths, "/usr/include");
}

static enum FileType get_file_type(const char *filename)
{
	if (opt_x != FILE_NONE)
		return opt_x;

	if (endswith(filename, ".a"))
		return FILE_AR;

	if (endswith(filename, ".so"))
		return FILE_DSO;

	if (endswith(filename, ".o"))
		return FILE_OBJ;

	if (endswith(filename, ".c"))
		return FILE_C;

	if (endswith(filename, ".s"))
		return FILE_ASM;

	error("<command line>: unknown file extension: %s", filename);
}

int main(int argc, const char **argv)
{
	const char *input, *output;
	struct StringArray ld_args = {};

	atexit(cleanup);
	init_macros();
	parse_args(argc, argv);

	if (opt_cc1) {
		add_default_include_paths(argv[0]);
		cc1();
		return 0;
	}

	if (input_paths.len > 1 && opt_o && (opt_c || opt_S || opt_E))
		error("cannot specify '-o' with '-c', '-S' or '-E' with multiple files");

	for (int i = 0; i < input_paths.len; i++) {
		input = input_paths.data[i];

		if (!strncmp(input, "-l", 2)) {
			strarray_push(&ld_args, input);
			continue;
		}

		if (opt_o)
			output = opt_o;
		else if (opt_S)
			output = replace_extn(input, ".s");
		else
			output = replace_extn(input, ".o");

		enum FileType type = get_file_type(input);

		// Handle .o or .a or .so
		if (type == FILE_OBJ ||
		    type == FILE_AR ||
		    type == FILE_DSO) {
			// link files .o/.a/.so -> binary
			strarray_push(&ld_args, input);
			continue;
		}

		// handle .s
		if (type == FILE_ASM) {
			if (!opt_S)
				// .s -> .o
				assemble(input, output);
			continue;
		}

		assert(type == FILE_C);

		// just preprocess
		if (opt_E || opt_M) {
			run_cc1(argc, argv, input, NULL);
			continue;
		}

		// just compile
		// if -S is given, assembly text is the final output.
		if (opt_S) {
			run_cc1(argc, argv, input, output);
			continue;
		}

		// compile and assemble
		if (opt_c) {
			const char *tmpfile = create_tmpfile();

			run_cc1(argc, argv, input, tmpfile);
			assemble(tmpfile, output);
			continue;
		}

		// Compile, assemble and link
		const char *tmp1 = create_tmpfile();
		const char *tmp2 = create_tmpfile();

		run_cc1(argc, argv, input, tmp1);
		assemble(tmp1, tmp2);
		strarray_push(&ld_args, tmp2);
	}

	if (ld_args.len)
		run_linker(&ld_args, opt_o ? opt_o : "a.out");

	return 0;
}
