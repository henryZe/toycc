#include <toycc.h>

void warn_tok(struct Token *tok, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verror_at(tok->file->name, tok->file->contents,
		  tok->line_no, tok->loc, fmt, ap);
	va_end(ap);
}

bool equal(struct Token *tok, const char *op)
{
	return !memcmp(tok->loc, op, tok->len) && !op[tok->len];
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
void verror_at(const char *filename, const char *input, int line_no,
	       const char *loc, const char *fmt, va_list ap)
{
	// find a line containing `loc`
	const char *line = loc;
	// find beginning of the line
	while (input < line && line[-1] != '\n')
		line--;

	const char *end = loc;
	// find ending of the line
	while (*end && *end != '\n')
		end++;

	// print out the line
	int indent = fprintf(stderr, "%s:%d: ", filename, line_no);
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
	verror_at(tok->file->name, tok->file->contents,
		  tok->line_no, tok->loc, fmt, ap);
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
