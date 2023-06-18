#include <toycc.h>

static bool is_hash(struct Token *tok)
{
	return tok->at_bol && equal(tok, "#");
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
