#include <toycc.h>
#include <scope.h>
#include <type.h>
#include <parser.h>

// All local variable instances created during parsing are
// accumulated to this list.
static struct Obj *locals;

void init_locals(void)
{
	locals = NULL;
}

struct Obj *ret_locals(void)
{
	return locals;
}

// Likewise, global variables are accumulated to this list.
static struct Obj *globals;

struct Obj *ret_globals(void)
{
	return globals;
}

static struct Scope *scope = &(struct Scope){};

void enter_scope(void)
{
	struct Scope *sc = malloc(sizeof(struct Scope));
	sc->vars = NULL;
	sc->next = scope;
	scope = sc;
}

void leave_scope(void)
{
	scope = scope->next;
}

struct VarScope *find_var(struct Token *tok)
{
	for (struct Scope *sc = scope; sc; sc = sc->next)
		for (struct VarScope *vars = sc->vars; vars; vars = vars->next)
			if (equal(tok, vars->name))
				return vars;
	return NULL;
}

struct Type *find_tag(struct Token *tok)
{
	for (struct Scope *sc = scope; sc; sc = sc->next)
		for (struct TagScope *tag = sc->tags; tag; tag = tag->next)
			if (equal(tok, tag->name))
				return tag->ty;
	return NULL;
}

struct Type *overwrite_tag(struct Token *tag, struct Type *ty)
{
	for (struct TagScope *sc = scope->tags; sc; sc = sc->next) {
		if (equal(tag, sc->name)) {
			*sc->ty = *ty;
			return sc->ty;
		}
	}

	return NULL;
}

void push_tag_scope(struct Token *tok, struct Type *ty)
{
	struct TagScope *sc = malloc(sizeof(struct TagScope));
	sc->name = strndup(tok->loc, tok->len);
	sc->ty = ty;
	sc->next = scope->tags;
	scope->tags = sc;
}

struct VarScope *push_scope(const char *name)
{
	struct VarScope *sc = malloc(sizeof(struct VarScope));
	sc->name = name;
	sc->next = scope->vars;
	scope->vars = sc;
	return sc;
}

// create variable and link to `locals` list
static struct Obj *new_var(const char *name, struct Type *ty)
{
	struct Obj *var = malloc(sizeof(struct Obj));
	var->name = name;
	var->ty = ty;
	var->align = ty->align;
	push_scope(name)->var = var;
	return var;
}

struct Obj *new_lvar(const char *name, struct Type *ty)
{
	struct Obj *var = new_var(name, ty);
	var->is_local = true;
	var->next = locals;
	locals = var;
	return var;
}

struct Obj *new_gvar(const char *name, struct Type *ty)
{
	struct Obj *var = new_var(name, ty);
	var->is_local = false;
	var->next = globals;
	var->is_definition = true;
	globals = var;
	return var;
}

// anonymous global variable
struct Obj *new_anon_gvar(struct Type *ty)
{
	return new_gvar(new_unique_name(), ty);
}

struct Obj *new_string_literal(const char *p, struct Type *ty)
{
	struct Obj *var = new_anon_gvar(ty);
	var->init_data = p;
	return var;
}

bool is_global_scope(void)
{
	// the last scope is the global scope
	return scope->next == NULL;
}
