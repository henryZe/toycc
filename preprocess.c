#include <toycc.h>
#include <libgen.h>

static bool is_hash(struct Token *tok)
{
	return tok->at_bol && equal(tok, "#");
}

// Some preprocessor directives such as #include allow extraneous
// tokens before newline. This function skips such tokens.
static struct Token *skip_token(struct Token *tok)
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
	if (!tok1 || tok1->kind == TK_EOF)
		return tok2;

	struct Token head = {};
	struct Token *cur = &head;

	for (; tok1 && tok1->kind != TK_EOF; tok1 = tok1->next) {
		cur->next = copy_token(tok1);
		cur = cur->next;
	}

	cur->next = tok2;
	return head.next;
}

// Visit all tokens in `tok` while evaluating
// preprocessing macros and directives.
static struct Token *preprocess(struct Token *tok)
{
	struct Token head = {};
	struct Token *cur = &head;

	while (tok->kind != TK_EOF) {
		// Pass through if it is not a "#".
		if (!is_hash(tok)) {
			cur->next = tok;
			cur = cur->next;

			tok = tok->next;
			continue;
		}

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

			tok = skip_token(tok->next);
			// append header file
			tok = append(tok2, tok);
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
	convert_keywords(tok);
	return tok;
};
