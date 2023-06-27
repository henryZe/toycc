#include <toycc.h>
#include <libgen.h>

struct Macro {
	struct Macro *next;
	const char *name;
	struct Token *body;
	bool deleted;
};

struct CondIncl {
	struct CondIncl *next;
	enum { IN_THEN, IN_ELIF, IN_ELSE } ctx;
	struct Token *tok;
	bool included;
};

static struct Macro *macros;
static struct CondIncl *cond_incl;

static bool is_hash(struct Token *tok)
{
	return tok->at_bol && equal(tok, "#");
}

// Some preprocessor directives such as #include allow extraneous
// tokens before newline. This function skips such tokens.
static struct Token *skip_line(struct Token *tok)
{
	if (tok->at_bol)
		return tok;

	warn_tok(tok, "extra token");
	while (tok->at_bol)
		tok = tok->next;
	return tok;
}

static struct Token *copy_token(struct Token *tok)
{
	struct Token *t = malloc(sizeof(struct Token));

	*t = *tok;
	t->next = NULL;
	return t;
}

// append tok2 to the end of tok1
static struct Token *append(struct Token *tok1, struct Token *tok2)
{
	if (tok1->kind == TK_EOF)
		return tok2;

	struct Token head = {};
	struct Token *cur = &head;

	for (; tok1->kind != TK_EOF; tok1 = tok1->next) {
		cur->next = copy_token(tok1);
		cur = cur->next;
	}

	cur->next = tok2;
	return head.next;
}

static struct Token *new_eof(struct Token *tok)
{
	struct Token *t = copy_token(tok);
	t->kind = TK_EOF;
	t->len = 0;
	return t;
}

// Copy all tokens until the next newline, terminate them with
// an EOF token and then returns them. This function is used to
// create a new list of tokens for `#if` arguments.
static struct Token *copy_line(struct Token **rest, struct Token *tok)
{
	struct Token head = {};
	struct Token *cur = &head;

	for (; !tok->at_bol; tok = tok->next) {
		cur->next = copy_token(tok);
		cur = cur->next;
	}

	// end with token whose kind is TK_EOF
	cur->next = new_eof(tok);
	*rest = tok;
	return head.next;
}

// Read and evaluate a constant expression.
static long eval_const_expr(struct Token **rest, struct Token *tok)
{
	struct Token *start = tok;
	struct Token *expr = copy_line(rest, tok->next);

	if (expr->kind == TK_EOF)
		error_tok(start, "no expression");

	struct Token *rest2;
	long val = const_expr(&rest2, expr);

	if (rest2->kind != TK_EOF)
		error_tok(rest2, "extra token");

	return val;
}

static struct CondIncl *push_cond_incl(struct Token *tok, bool included)
{
	struct CondIncl *ci = malloc(sizeof(struct CondIncl));

	ci->next = cond_incl;
	ci->ctx = IN_THEN;
	ci->tok = tok;
	ci->included = included;

	cond_incl = ci;
	return ci;
}

static struct Token *skip_cond_incl2(struct Token *tok)
{
	while (tok->kind != TK_EOF) {
		if (is_hash(tok) && equal(tok->next, "if")) {
			tok = skip_cond_incl2(tok->next->next);
			continue;
		}

		if (is_hash(tok) && equal(tok->next, "endif"))
			return tok->next->next;

		tok = tok->next;
	}
	return tok;
}

// Skip until next `#else`, `#elif` or `#endif`.
// Nested #if and #endif are skipped.
static struct Token *skip_cond_incl(struct Token *tok)
{
	while (tok->kind != TK_EOF) {
		if (is_hash(tok) && equal(tok->next, "if")) {
			tok = skip_cond_incl2(tok->next->next);
			continue;
		}

		// end with #else or #endif
		if (is_hash(tok) &&
		   (equal(tok->next, "elif") ||
		    equal(tok->next, "else") ||
		    equal(tok->next, "endif")))
			break;

		tok = tok->next;
	}
	return tok;
}

static struct Macro *find_macro(struct Token *tok)
{
	if (tok->kind != TK_IDENT)
		return NULL;

	for (struct Macro *m = macros; m; m = m->next)
		if (strlen(m->name) == tok->len &&
		   !strncmp(m->name, tok->loc, tok->len))
			return m->deleted ? NULL : m;

	return NULL;
}

// If tok is a macro, expand it and return true.
// Otherwise, do nothing and return false.
static bool expand_macro(struct Token **rest, struct Token *tok)
{
	struct Macro *m = find_macro(tok);
	if (!m)
		return false;

	*rest = append(m->body, tok->next);
	return true;
}

static struct Macro *add_macro(const char *name, struct Token *body)
{
	struct Macro *m = malloc(sizeof(struct Macro));

	m->next = macros;
	m->name = name;
	m->body = body;

	macros = m;
	return m;
}

// Visit all tokens in `tok` while evaluating
// preprocessing macros and directives.
static struct Token *preprocess(struct Token *tok)
{
	struct Token head = {};
	struct Token *cur = &head;

	while (tok->kind != TK_EOF) {
		// If it is a macro, expand it.
		if (expand_macro(&tok, tok))
			continue;

		// Pass through if it is not a "#".
		if (!is_hash(tok)) {
			cur->next = tok;
			cur = cur->next;

			tok = tok->next;
			continue;
		}

		struct Token *start = tok;
		tok = tok->next;

		if (equal(tok, "include")) {
			tok = tok->next;

			if (tok->kind != TK_STR)
				error_tok(tok, "expected a filename");

			const char *path;
			if (tok->str[0] == '/')
				// root directory
				path = tok->str;
			else
				path = format("%s/%s", dirname(strdup(tok->file->name)), tok->str);
			struct Token *tok2 = tokenize_file(path);

			if (!tok2)
				error_tok(tok, "%s", strerror(errno));

			tok = skip_line(tok->next);
			// append header file
			tok = append(tok2, tok);
			continue;
		}

		if (equal(tok, "define")) {
			tok = tok->next;
			if (tok->kind != TK_IDENT)
				error_tok(tok, "macro name must be an identifier");

			const char *name = strndup(tok->loc, tok->len);
			add_macro(name, copy_line(&tok, tok->next));
			continue;
		}

		if (equal(tok, "undef")) {
			tok = tok->next;
			if (tok->kind != TK_IDENT)
				error_tok(tok, "macro name must be an identifier");

			const char *name = strndup(tok->loc, tok->len);
			tok = skip_line(tok->next);

			struct Macro *m = add_macro(name, NULL);
			m->deleted = true;
			continue;
		}

		if (equal(tok, "if")) {
			long val = eval_const_expr(&tok, tok);

			push_cond_incl(start, val);
			if (!val)
				tok = skip_cond_incl(tok);
			continue;
		}

		if (equal(tok, "elif")) {
			if (!cond_incl || cond_incl->ctx == IN_ELSE)
				error_tok(start, "stray #elif");

			cond_incl->ctx = IN_ELIF;
			if (!cond_incl->included && eval_const_expr(&tok, tok))
				cond_incl->included = true;
			else
				tok = skip_cond_incl(tok);

			continue;
		}

		if (equal(tok, "else")) {
			if (!cond_incl || cond_incl->ctx == IN_ELSE)
				error_tok(start, "stray #else");

			cond_incl->ctx = IN_ELSE;
			tok = skip_line(tok->next);

			if (cond_incl->included)
				tok = skip_cond_incl(tok);

			continue;
		}

		if (equal(tok, "endif")) {
			if (!cond_incl)
				error_tok(start, "stray #endif");

			// pop
			cond_incl = cond_incl->next;
			tok = skip_line(tok->next);
			continue;
		}

		// `#`-only line is legal. It's called a null directive.
		if (tok->at_bol)
			continue;

		error_tok(tok, "invalid preprocessor directive");
	}

	cur->next = tok;
	return head.next;
}

// Entry point function of the preprocessor
struct Token *preprocessor(struct Token *tok)
{
	tok = preprocess(tok);
	if (cond_incl)
		error_tok(cond_incl->tok, "unterminated conditional directive");

	convert_keywords(tok);
	return tok;
};
