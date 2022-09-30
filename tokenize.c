#include <toycc.h>

static void __attribute__((noreturn))
error_at(const char *loc, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verror_at(loc, fmt, ap);
}

static struct Token *new_token(enum TokenKind kind,
				const char *start, const char *end)
{
	struct Token *tok = malloc(sizeof(struct Token));

	tok->kind = kind;
	tok->loc = start;
	tok->len = end - start;
	return tok;
}

static bool startwith(const char *p, const char *q)
{
	return strncmp(p, q, strlen(q)) == 0;
}

static int read_punct(const char *p)
{
	if (startwith(p, "==") || startwith(p, "!=") ||
		startwith(p, "<=") || startwith(p, ">="))
		return 2;
	return ispunct(*p) ? 1 : 0;
}

// Returns true if c is valid as the first character of an identifier.
static bool is_ident1(char c)
{
	return isalpha(c) || c == '_';
}

// Returns true if c is valid as a non-first character of an identifier.
static bool is_ident2(char c)
{
	return is_ident1(c) || isdigit(c);
}

static bool is_keyword(struct Token *tok)
{
	static const char *kw[] = {
		"return",
		"if",
		"else",
		"for",
		"while",
	};

	for (int i = 0; i < ARRAY_SIZE(kw); i++)
		if (equal(tok, kw[i]))
			return true;
	return false;
}

static void convert_keywords(struct Token *tok)
{
	for (struct Token *t = tok; t->kind != TK_EOF; t = t->next)
		if (is_keyword(t))
			t->kind = TK_KEYWORD;
}

// Tokenize a given string and returns new tokens.
struct Token *tokenize(const char *p)
{
	struct Token head;
	struct Token *cur = &head;

	error_set_current_input(p);

	while (*p) {
		// Skip whitespace characters
		if (isspace(*p)) {
			p++;
			continue;
		}

		// Numeric literal
		if (isdigit(*p)) {
			cur->next = new_token(TK_NUM, p, p);
			cur = cur->next;

			const char *q = p;
			cur->val = strtol(p, (char **)&p, 10);
			cur->len = p - q;
			continue;
		}

		// Identifier or keyword
		if (is_ident1(*p)) {
			const char *start = p;
			do {
				p++;
			} while (is_ident2(*p));
			cur->next = new_token(TK_IDENT, start, p);
			cur = cur->next;
			continue;
		}

		// Punctuators
		int punct_len = read_punct(p);
		if (punct_len) {
			cur->next = new_token(TK_PUNCT, p, p + punct_len);
			cur = cur->next;
			p += punct_len;
			continue;
		}

		error_at(p, "invalid token");
	}

	cur->next = new_token(TK_EOF, p, p);

	convert_keywords(head.next);
	return head.next;
}
