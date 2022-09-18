#include <toycc.h>

// Input string
static const char *current_input;

void error_set_current_input(const char *p)
{
	current_input = p;
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

void __attribute__((noreturn))
verror_at(const char *loc, const char *fmt, va_list ap)
{
	int pos = loc - current_input;

	fprintf(stderr, "%s\n", current_input);
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
