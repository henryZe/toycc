#ifndef __TYPE_H__
#define __TYPE_H__

struct Type {
	enum TypeKind kind;
	int size;		// sizeof() value
	int align;		// alignment
	bool is_unsigned;	// unsigned or signed
	bool is_atomic;		// true if _Atomic
	struct Type *origin;	// for type compatibility check

	// pointer-to or array-of type.
	// We intentionally use the same member to
	// represent pointer/array duality in C.
	struct Type *base;

	// declaration
	struct Token *name;
	struct Token *name_pos;

	// Array
	int array_len;

	// Variable-length array
	struct Node *vla_len; // # of elements
	struct Obj *vla_size; // sizeof() value

	// struct
	struct Member *members;
	bool is_flexible;
	bool is_packed;

	// function type
	struct Type *return_ty;
	struct Type *params;
	bool is_variadic;
	struct Type *next;
};

struct Type *p_ty_void(void);
struct Type *p_ty_bool(void);
struct Type *p_ty_char(void);
struct Type *p_ty_short(void);
struct Type *p_ty_int(void);
struct Type *p_ty_long(void);
struct Type *p_ty_uchar(void);
struct Type *p_ty_ushort(void);
struct Type *p_ty_uint(void);
struct Type *p_ty_ulong(void);
struct Type *p_ty_float(void);
struct Type *p_ty_double(void);
struct Type *p_ty_ldouble(void);

bool is_integer(struct Type *ty);
bool is_float(struct Type *ty);
bool is_float_arg(struct Type *ty);
bool is_struct_union(struct Type *ty);
bool is_numeric(struct Type *ty);
bool is_compatible(struct Type *t1, struct Type *t2);
struct Type *copy_type(struct Type *ty);
struct Type *pointer_to(struct Type *base);
struct Type *func_type(struct Type *return_ty);
struct Type *array_of(struct Type *base, int size);
struct Type *vla_of(struct Type *base, struct Node *expr);
struct Type *enum_type(void);
struct Type *struct_type(void);

void add_type(struct Node *node);

#endif
