#define ASSERT(x, y) assert(x, y, #y)

void assert(int expected, int actual, char *code);
void pass(void);
int printf(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int vsprintf(char *buf, char *fmt, void *ap);
int strcmp(char *p, char *q);
int memcmp(const void *, const void *, long unsigned int);
void exit(int n);
