#!/usr/bin/python3
import re
import sys

print("""typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long int64_t;
typedef unsigned long size_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

typedef struct FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

typedef void* va_list;

struct stat {
	char _[512];
};

void *malloc(long size);
void *calloc(long nmemb, long size);
void *realloc(void *buf, long size);
int *__errno_location();
char *strerror(int errnum);
FILE *fopen(char *pathname, char *mode);
FILE *open_memstream(char **ptr, size_t *sizeloc);
long fread(void *ptr, long size, long nmemb, FILE *stream);
size_t fwrite(void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush(FILE *stream);
int fclose(FILE *fp);
int fputc(int c, FILE *stream);
int feof(FILE *stream);
static void assert(...) {}
int strcmp(char *s1, char *s2);
int strncasecmp(char *s1, char *s2, long n);
int memcmp(char *s1, char *s2, long n);
int printf(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int fprintf(FILE *fp, char *fmt, ...);
int vfprintf(FILE *fp, char *fmt, va_list ap);
long strlen(char *p);
int strncmp(char *p, char *q, long n);
void *memset(void *s, int c, size_t n);
void *memcpy(char *dst, char *src, long n);
char *strdup(char *p);
char *strndup(char *p, long n);
int isspace(int c);
int ispunct(int c);
int isdigit(int c);
int isxdigit(int c);
char *strstr(char *haystack, char *needle);
char *strchr(char *s, int c);
double strtod(char *nptr, char **endptr);
static void va_end(va_list ap) {}
long strtoul(char *nptr, char **endptr, int base);
void exit(int code);
char *basename(char *path);
char *strrchr(char *s, int c);
int unlink(char *pathname);
int mkstemp(char *template);
int close(int fd);
int fork(void);
int execvp(char *file, char **argv);
void _exit(int code);
int wait(int *wstatus);
int atexit(void (*)(void));
int isalpha(int c);
""")

for path in sys.argv[1:]:
    with open(path) as file:
        s = file.read()
        s = re.sub(r'\\\n', '', s)
        s = re.sub(r'^\s*#.*', '', s, flags=re.MULTILINE)
        s = re.sub(r'"\n\s*"', '', s)
        s = re.sub(r'"\n\s*"', '', s)

        s = re.sub(r'\b__attribute__\(\([^)]*\)\)', '', s)
        s = re.sub(r'\t\t\t\t.kind =', '', s)
        s = re.sub(r'\t\t\t\t.size =', '', s)
        s = re.sub(r'\t\t\t\t.align =', '', s)
        s = re.sub(r'\t\t\t\t.is_unsigned =', '', s)

        s = re.sub(r'\bbool\b', '_Bool', s)
        s = re.sub(r'\berrno\b', '*__errno_location()', s)
        s = re.sub(r'\btrue\b', '1', s)
        s = re.sub(r'\bfalse\b', '0', s)
        s = re.sub(r'\bNULL\b', '0', s)
        s = re.sub(r'\bva_start\(([^)]*),([^)]*)\)', '\\1=__va_area__', s)
        s = re.sub(r'\bunreachable\(\)', 'error("unreachable")', s)
        s = re.sub(r'\bMIN\(([^)]*),([^)]*)\)', '((\\1)<(\\2)?(\\1):\\2)', s)

        s = re.sub(r'ARRAY_SIZE\(([^)]*)\)', 'sizeof(\\1)/sizeof(\\1[0])', s)
        s = re.sub(r'debug', 'println', s)
        s = re.sub(r'INIT_CAPACITY', '8', s)

        s = re.sub(r'TOI8', r'"\\tslli a0, a0, 56\\n\\tsrai a0, a0, 56"', s)
        s = re.sub(r'TOU8', r'"\\tslli a0, a0, 56\\n\\tsrli a0, a0, 56"', s)
        s = re.sub(r'TOU16', r'"\\tslli a0, a0, 48\\n\\tsrli a0, a0, 48"', s)
        s = re.sub(r'TOI16', r'"\\tslli a0, a0, 48\\n\\tsrai a0, a0, 48"', s)
        s = re.sub(r'TOU32', r'"\\tslli a0, a0, 32\\n\\tsrli a0, a0, 32"', s)
        s = re.sub(r'TOI32', r'"\\tslli a0, a0, 32\\n\\tsrai a0, a0, 32"', s)

        s = re.sub(r'I32F32', r'"\\tfcvt.s.w fa0, a0\\n"', s)
        s = re.sub(r'I32F64', r'"\\tfcvt.d.w fa0, a0\\n"', s)
        s = re.sub(r'I64F32', r'"\\tfcvt.s.l fa0, a0\\n"', s)
        s = re.sub(r'I64F64', r'"\\tfcvt.d.l fa0, a0\\n"', s)
        s = re.sub(r'U32F32', r'"\\tfcvt.s.wu fa0, a0\\n"', s)
        s = re.sub(r'U32F64', r'"\\tfcvt.d.wu fa0, a0\\n"', s)
        s = re.sub(r'U64F32', r'"\\tfcvt.s.lu fa0, a0\\n"', s)
        s = re.sub(r'U64F64', r'"\\tfcvt.d.lu fa0, a0\\n"', s)

        s = re.sub(r'F32I32', r'"\\tfcvt.w.s a0, fa0, rtz\\n"', s)
        s = re.sub(r'F32I8', r'"\\tfcvt.w.s a0, fa0, rtz\\n\\tslli a0, a0, 56\\n\\tsrai a0, a0, 56"', s)
        s = re.sub(r'F32I16', r'"\\tfcvt.w.s a0, fa0, rtz\\n\\tslli a0, a0, 48\\n\\tsrai a0, a0, 48"', s)
        s = re.sub(r'F32I64', r'"\\tfcvt.l.s a0, fa0, rtz\\n"', s)
        s = re.sub(r'F32U32', r'"\\tfcvt.wu.s a0, fa0, rtz\\n"', s)
        s = re.sub(r'F32U8', r'"\\tfcvt.wu.s a0, fa0, rtz\\n\\tslli a0, a0, 56\\n\\tsrli a0, a0, 56"', s)
        s = re.sub(r'F32U16', r'"\\tfcvt.wu.s a0, fa0, rtz\\n\\tslli a0, a0, 48\\n\\tsrli a0, a0, 48"', s)
        s = re.sub(r'F32U64', r'"\\tfcvt.lu.s a0, fa0, rtz\\n"', s)
        s = re.sub(r'F64I32', r'"\\tfcvt.w.d a0, fa0, rtz\\n"', s)
        s = re.sub(r'F64I8', r'"\\tfcvt.w.d a0, fa0, rtz\\n\\tslli a0, a0, 56\\n\\tsrai a0, a0, 56"', s)
        s = re.sub(r'F64I16', r'"\\tfcvt.w.d a0, fa0, rtz\\n\\tslli a0, a0, 48\\n\\tsrai a0, a0, 48"', s)
        s = re.sub(r'F64I64', r'"\\tfcvt.l.d a0, fa0, rtz\\n"', s)
        s = re.sub(r'F64U32', r'"\\tfcvt.wu.d a0, fa0, rtz\\n"', s)
        s = re.sub(r'F64U8', r'"\\tfcvt.wu.d a0, fa0, rtz\\n\\tslli a0, a0, 56\\n\\tsrli a0, a0, 56"', s)
        s = re.sub(r'F64U16', r'"\\tfcvt.wu.d a0, fa0, rtz\\n\\tslli a0, a0, 48\\n\\tsrli a0, a0, 48"', s)
        s = re.sub(r'F64U64', r'"\\tfcvt.lu.d a0, fa0, rtz\\n"', s)

        s = re.sub(r'F32F64', r'"\\tfcvt.d.s fa0, fa0\\n"', s)
        s = re.sub(r'F64F32', r'"\\tfcvt.s.d fa0, fa0\\n"', s)

        print(s)
