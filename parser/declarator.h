#ifndef __DECLARATOR_H__
#define __DECLARATOR_H__

// Variable attributes such as typedef or extern.
struct VarAttr {
	bool is_typedef;
	bool is_static;
	bool is_extern;
	bool is_inline;
	bool is_tls;
	int align;
};

struct Type *declspec(struct Token **rest, struct Token *tok,
                      struct VarAttr *attr);
struct Type *declarator(struct Token **rest, struct Token *tok, struct Type *ty);
struct Node *declaration(struct Token **rest, struct Token *tok,
			 struct Type *basety, struct VarAttr *attr);
struct Token *parse_typedef(struct Token *tok, struct Type *basety);
void declare_builtin_functions(void);
struct Node *compute_vla_size(struct Type *ty, struct Token *tok);

#endif
