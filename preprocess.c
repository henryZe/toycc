#include <toycc.h>

// Entry point function of the preprocessor
struct Token *preprocessor(struct Token *tok)
{
	convert_keywords(tok);
	return tok;
};
