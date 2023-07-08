#ifndef __PREPROCESSOR_H__
#define __PREPROCESSOR_H__

void init_macros(void);
struct Macro *add_macro(const char *name, bool is_objlike, struct Token *body);

#endif
