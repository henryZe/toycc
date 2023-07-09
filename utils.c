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
	int indent = 0;
	// find beginning of the line
	while (input < line && line[-1] != '\n') {
		if (line[-1] == '\t')
			indent++;
		line--;
	}

	const char *end = loc;
	// find ending of the line
	while (*end && *end != '\n')
		end++;

	// print out the line
	fprintf(stderr, "%s:%d:\n", filename, line_no);

	int len = end - line;
	fprintf(stderr, "%.*s\n", len, line);

	// show the error message
	int pos = loc - line + indent * (8 - 1);

	fprintf(stderr, "%*s", pos, "");	// print pos spaces
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
	int ret = 0;

	for (int n = num; n > 1; n >>= 1) {
		if (n & 1)
			error("wrong input value %d", num);
		ret++;
	}

	return ret;
}

struct Token *skip(struct Token *tok, const char *s)
{
	if (!equal(tok, s))
		error_tok(tok, "expected '%s'", s);
	return tok->next;
}
