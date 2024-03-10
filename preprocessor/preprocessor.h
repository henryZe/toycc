#ifndef __PREPROCESSOR_H__
#define __PREPROCESSOR_H__

typedef struct Token *macro_handler_fn(struct Token *);

struct Macro {
	const char *name;
	bool is_objlike;		// object-like or function-like
	struct MacroParam *params;
	const char *va_args_name;
	struct Token *body;
        macro_handler_fn *handler;
};

struct Macro *add_macro(const char *name, bool is_objlike, struct Token *body);
struct Token *new_str_token(const char *str, struct Token *tmpl);
struct Token *new_num_token(int val, struct Token *tmpl);

#endif
