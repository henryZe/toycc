#define ASSERT(x, y) assert(x, y, #y)

void assert(int expected, int actual, const char *code);
void pass(void);
int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, void *ap);
int strcmp(const char *p, const char *q);
int strncmp(const char *s1, const char *s2, long unsigned n);
long strlen(const char *s);
int memcmp(const void *dest, const void *src, long unsigned int);
void exit(int n);
void *memcpy(const void *dest, const void *src, long n);
