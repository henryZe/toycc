#include <toycc.h>

bool equal(struct Token *tok, const char *op)
{
	return !memcmp(tok->loc, op, tok->len) && !op[tok->len];
}

static const char *current_input;	// Input string
static const char *current_filename;	// Input filename

void set_cur_input(const char *p)
{
	current_input = p;
}

void set_cur_filename(const char *filename)
{
	current_filename = filename;
}

void __attribute__((noreturn))
error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	exit(1);
}

// Reports an error message in the following format and exit.
//
// foo.c:10: x = y + 1;
//               ^ <error message here>
void __attribute__((noreturn))
verror_at(const char *loc, const char *fmt, va_list ap)
{
	// find a line containing `loc`
	const char *line = loc;
	// find beginning of the line
	while (current_input < line && line[-1] != '\n')
		line--;

	const char *end = loc;
	// find ending of the line
	while (*end != '\n')
		end++;

	// get a line number
	int line_no = 1;
	for (const char *p = current_input; p < line; p++)
		if (*p == '\n')
			line_no++;

	// print out the line
	int indent = fprintf(stderr, "%s:%d: ", current_filename, line_no);
	fprintf(stderr, "%.*s\n", (int)(end - line), line);

	// show the error message
	int pos = loc - line + indent;

	// print pos spaces
	fprintf(stderr, "%*s", pos, "");
	fprintf(stderr, "^ ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	exit(1);
}

void __attribute__((noreturn))
error_tok(struct Token *tok, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verror_at(tok->loc, fmt, ap);
}
