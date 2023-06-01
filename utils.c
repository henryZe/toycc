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

const char *get_cur_input(void)
{
	return current_input;
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
	va_end(ap);
	exit(1);
}

// Reports an error message in the following format.
//
// foo.c:10: x = y + 1;
//               ^ <error message here>
void verror_at(int line_no, const char *loc, const char *fmt, va_list ap)
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

	// print out the line
	int indent = fprintf(stderr, "%s:%d: ", current_filename, line_no);
	int len = end - line;
	fprintf(stderr, "%.*s\n", len, line);

	// show the error message
	int pos = loc - line + indent;

	// print pos spaces
	fprintf(stderr, "%*s", pos, "");
	fprintf(stderr, "^ ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
}

void __attribute__((noreturn))
error_tok(struct Token *tok, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verror_at(tok->line_no, tok->loc, fmt, ap);
	va_end(ap);
	exit(1);
}

int llog2(int num)
{
	int n = num;
	int ret = 0;

	while (n > 1) {
		if (n % 2)
			error("wrong input value %d", num);
		n /= 2;
		ret++;
	}

	return ret;
}
