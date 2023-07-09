#ifndef __PREPROCESSOR_H__
#define __PREPROCESSOR_H__

typedef struct Token *macro_handler_fn(struct Token *);

struct Macro {
	struct Macro *next;
	const char *name;
	bool is_objlike;		// object-like or function-like
	struct MacroParam *params;
	bool is_variadic;
	struct Token *body;
	bool deleted;			// used for #undef
        macro_handler_fn *handler;
};

void init_macros(void);
struct Macro *add_macro(const char *name, bool is_objlike, struct Token *body);
struct Token *new_str_token(const char *str, struct Token *tmpl);
struct Token *new_num_token(int val, struct Token *tmpl);

#endif
