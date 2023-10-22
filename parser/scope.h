#ifndef __SCOPE_H__
#define __SCOPE_H__

// Scope for local variables, global variables,
// typedefs or enum constants
struct VarScope {
	struct VarScope *next;
	const char *name;
	struct Obj *var;
	struct Type *type_def;
	struct Type *enum_ty;
	int enum_val;
};

// Scope for struct, union or enum tags
struct TagScope {
	struct TagScope *next;
	const char *name;
	struct Type *ty;
};

// represents a block scope
struct Scope {
	struct Scope *next;

	// C has two block scopes:
	// one is for variables/typedefs
	struct VarScope *vars;
	// and the other is for struct/union/enum tags.
	struct TagScope *tags;
};

void init_locals(void);
struct Obj *ret_locals(void);
struct Obj *ret_globals(void);

void enter_scope(void);
void leave_scope(void);

struct Type *find_tag(struct Token *tok);
struct Obj *find_func(const char *name);

void push_tag_scope(struct Token *tok, struct Type *ty);
struct VarScope *push_scope(const char *name);
struct Type *overwrite_tag(struct Token *tok, struct Type *ty);

struct Obj *new_lvar(const char *name, struct Type *ty);
struct Obj *new_gvar(const char *name, struct Type *ty);
struct Obj *new_anon_gvar(struct Type *ty);
struct Obj *new_string_literal(const char *p, struct Type *ty);

bool is_global_scope(void);
void scan_globals(void);

#endif
