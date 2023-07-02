// This file implements the C preprocessor.
//
// The preprocessor takes a list of tokens as an input and returns a
// new list of tokens as an output.
//
// The preprocessing language is designed in such a way that that's
// guaranteed to stop even if there is a recursive macro.
// Informally speaking, a macro is applied only once for each token.
// That is, if a macro token T appears in a result of direct or
// indirect macro expansion of T, T won't be expanded any further.
// For example, if T is defined as U, and U is defined as T, then
// token T is expanded to U and then to T and the macro expansion
// stops at that point.
//
// To achieve the above behavior, we attach for each token a set of
// macro names from which the token is expanded. The set is called
// "hideset". Hideset is initially empty, and every time we expand a
// macro, the macro name is added to the resulting tokens' hidesets.
//
// The above macro expansion algorithm is explained in this document,
// which is used as a basis for the standard's wording:
// https://github.com/rui314/chibicc/wiki/cpp.algo.pdf

#include <toycc.h>
#include <libgen.h>

// formal parameter
struct MacroParam {
	struct MacroParam *next;
	const char *name;
};

// actual parameter
struct MacroArg {
	struct MacroArg *next;
	const char *name;
	struct Token *tok;
};

struct Macro {
	struct Macro *next;
	const char *name;
	bool is_objlike;		// object-like or function-like
	struct MacroParam *params;
	struct Token *body;
	bool deleted;			// used for #undef
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

static struct Token *preprocess(struct Token *tok);
// Read and evaluate a constant expression.
static long eval_const_expr(struct Token **rest, struct Token *tok)
{
	struct Token *start = tok;
	struct Token *expr = copy_line(rest, tok->next);

	expr = preprocess(expr);

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
	struct CondIncl *ci = calloc(1, sizeof(struct CondIncl));

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
		if (is_hash(tok) &&
		   (equal(tok->next, "if") ||
		    equal(tok->next, "ifdef") ||
		    equal(tok->next, "ifndef"))) {
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

static struct Hideset *new_hideset(const char *name)
{
	struct Hideset *hs = calloc(1, sizeof(struct Hideset));

	hs->next = NULL;
	hs->name = name;
	return hs;
}

// union hs2 to the end of hs1
static struct Hideset *hideset_union(struct Hideset *hs1,
				     struct Hideset *hs2)
{
	struct Hideset head = {};
	struct Hideset *cur = &head;

	for (; hs1; hs1 = hs1->next) {
		cur->next = new_hideset(hs1->name);
		cur = cur->next;
	}
	cur->next = hs2;

	return head.next;
}

static bool hideset_contains(struct Hideset *hs, const char *s, size_t len)
{
	for (; hs; hs = hs->next)
		if (strlen(hs->name) == len && !strncmp(hs->name, s, len))
			return true;

	return false;
}

static struct Token *add_hideset(struct Token *tok, struct Hideset *hs)
{
	struct Token head = {};
	struct Token *cur = &head;

	for (; tok; tok = tok->next) {
		struct Token *t = copy_token(tok);
		t->hideset = hideset_union(t->hideset, hs);

		cur->next = t;
		cur = cur->next;
	}

	return head.next;
}

static struct MacroArg *read_macro_arg_one(struct Token **rest, struct Token *tok)
{
	struct Token head = {};
	struct Token *cur = &head;
	int level = 0;

	while (level > 0 || (!equal(tok, ",") && !equal(tok, ")"))) {
		if (tok->kind == TK_EOF)
			error_tok(tok, "premature end of input");

		if (equal(tok, "("))
			level++;
		else if (equal(tok, ")"))
			level--;

		cur->next = copy_token(tok);
		cur = cur->next;

		tok = tok->next;
	}

	cur->next = new_eof(tok);

	struct MacroArg *arg = calloc(1, sizeof(struct MacroArg));
	arg->tok = head.next;

	*rest = tok;
	return arg;
}

static struct MacroArg *read_macro_args(struct Token **rest, struct Token *tok,
					struct MacroParam *params)
{
	// the macro self
	struct Token *start = tok;
	// skip '('
	tok = tok->next->next;

	struct MacroArg head = {};
	struct MacroArg *cur = &head;

	struct MacroParam *pp;

	for (pp = params; pp; pp = pp->next) {
		if (cur != &head)
			tok = skip(tok, ",");

		cur->next = read_macro_arg_one(&tok, tok);
		cur = cur->next;
		// arg->name points to param->name
		cur->name = pp->name;
	}

	if (pp)
		error_tok(start, "too many arguments");

	*rest = skip(tok, ")");
	return head.next;
}

static struct MacroArg *find_arg(struct MacroArg *args, struct Token *tok)
{
	for (struct MacroArg *ap = args; ap; ap = ap->next)
		if (tok->len == strlen(ap->name) && !strncmp(tok->loc, ap->name, tok->len))
			return ap;

	return NULL;
}

// Replace func-like macro parameters with given arguments.
static struct Token *subst(struct Token *tok, struct MacroArg *args)
{
	struct Token head = {};
	struct Token *cur = &head;

	while (tok->kind != TK_EOF) {
		// match args' param->name in the macro's body
		struct MacroArg *arg = find_arg(args, tok);

		// Handle a macro token.
		// Macro arguments are completely macro-expanded
		// before they are substituted into a macro body.
		if (arg) {
			// expand the macro
			struct Token *t = preprocess(arg->tok);

			for (; t->kind != TK_EOF; t = t->next) {
				cur->next = copy_token(t);
				cur = cur->next;
			}

			tok = tok->next;
			continue;
		}

		// Handle a non-macro token
		cur->next = copy_token(tok);
		cur = cur->next;

		tok = tok->next;
		continue;
	}

	cur->next = tok;
	return head.next;
}

// If tok is a macro, expand it and return true.
// Otherwise, do nothing and return false.
static bool expand_macro(struct Token **rest, struct Token *tok)
{
	// If tok has already been in the tok's hideset,
	// that means it has already been expanded before.
	if (hideset_contains(tok->hideset, tok->loc, tok->len))
		return false;

	// search the token in the macro list
	struct Macro *m = find_macro(tok);
	if (!m)
		return false;

	// Object-like macro application
	if (m->is_objlike) {
		// add macro to the tok's hideset
		struct Hideset *hs = hideset_union(tok->hideset, new_hideset(m->name));
		struct Token *body = add_hideset(m->body, hs);

		*rest = append(body, tok->next);
		return true;
	}

	// If a func-like macro token is not followed by an argument list,
	// treat it as a normal identifier.
	if (!equal(tok->next, "("))
		return false;

	// Function-like macro application
	struct MacroArg *args = read_macro_args(&tok, tok, m->params);
	*rest = append(subst(m->body, args), tok);
	return true;
}

static struct Macro *add_macro(const char *name, bool is_objlike,
			       struct Token *body)
{
	struct Macro *m = calloc(1, sizeof(struct Macro));

	m->next = macros;
	m->name = name;
	m->is_objlike = is_objlike;
	m->body = body;
	m->deleted = false;

	macros = m;
	return m;
}

static struct MacroParam *read_macro_params(struct Token **rest, struct Token *tok)
{
	struct MacroParam head = {};
	struct MacroParam *cur = &head;

	while (!equal(tok, ")")) {
		if (cur != &head)
			tok = skip(tok, ",");

		if (tok->kind != TK_IDENT)
			error_tok(tok, "expected an identifier");

		struct MacroParam *m = calloc(1, sizeof(struct MacroParam));
		m->name = strndup(tok->loc, tok->len);

		cur->next = m;
		cur = cur->next;

		tok = tok->next;
	}

	// skip ')'
	*rest = tok->next;
	return head.next;
}

static void read_macro_definition(struct Token **rest, struct Token *tok)
{
	if (tok->kind != TK_IDENT)
		error_tok(tok, "macro name must be an identifier");

	const char *name = strndup(tok->loc, tok->len);
	tok = tok->next;

	if (!tok->has_space && equal(tok, "(")) {
		// function-like macro
		struct MacroParam *params = read_macro_params(&tok, tok->next);
		struct Macro *m = add_macro(name, false, copy_line(rest, tok));
		m->params = params;

	} else {
		// object-like macro
		add_macro(name, true, copy_line(rest, tok));
	}
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
			read_macro_definition(&tok, tok->next);
			continue;
		}

		if (equal(tok, "undef")) {
			tok = tok->next;
			if (tok->kind != TK_IDENT)
				error_tok(tok, "macro name must be an identifier");

			const char *name = strndup(tok->loc, tok->len);
			tok = skip_line(tok->next);

			struct Macro *m = add_macro(name, true, NULL);
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

		if (equal(tok, "ifdef")) {
			bool defined = find_macro(tok->next);

			push_cond_incl(tok, defined);
			tok = skip_line(tok->next->next);

			if (!defined)
				tok = skip_cond_incl(tok);
			continue;
		}

		if (equal(tok, "ifndef")) {
			bool defined = find_macro(tok->next);

			push_cond_incl(tok, !defined);
			tok = skip_line(tok->next->next);

			if (defined)
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
