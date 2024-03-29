#ifndef __STDARG_H
#define __STDARG_H

typedef void *va_list;

#define va_start(ap, last)	\
do {				\
	ap = __va_area__;	\
} while (0)

#define va_end(ap)

#define va_arg(ap, type)		\
({					\
	ap += sizeof(long);		\
	*(type *)(ap - sizeof(long));	\
})

#define __GNUC_VA_LIST 1
typedef va_list __gnuc_va_list;

#define va_copy(dest, src) (dest = src)

#endif
