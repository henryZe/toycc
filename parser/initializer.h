#ifndef __INITIALIZER_H__
#define __INITIALIZER_H__

#include <toycc.h>

// This struct represents a variable initializer. Since initializers
// can be nested (e.g. `int x[2][2] = {{1, 2}, {3, 4}}`), this struct
// is a tree data structure.
struct Initializer {
	struct Initializer *next;

	struct Type *ty;
	struct Token *tok;
	bool is_flexible;

	// If it's not an aggregate type and has an initializer,
	// `expr` has an initialization expression.
	struct Node *expr;

	// If it's an initializer for an aggregate type (e.g. array or struct),
	// `children` has initializers for its children.
	struct Initializer **children;

	// Only one member can be initialized for a union.
	// `mem` is used to clarify which member is initialized.
	struct Member *mem;
};

// For local variable initializer.
struct InitDesg {
	// former level of array
	struct InitDesg *next;
	int idx;
	struct Member *member;
	struct Obj *var;
};

// initializer
void gvar_initializer(struct Token **rest, struct Token *tok,
			     struct Obj *var);
struct Node *lvar_initializer(struct Token **rest, struct Token *tok,
				     struct Obj *var);
struct Member *get_struct_member(struct Type *ty, struct Token *tok);

#endif
