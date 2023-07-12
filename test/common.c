#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void assert(int expected, int actual, const char *code)
{
	if (expected != actual) {
		printf("%s => %d expected but got %d\n", code, expected, actual);
		exit(1);
	}

	printf("%s => %d\n", code, actual);
}

static int static_fn() { return 5; }
int ext1 = 5;
int *ext2 = &ext1;
int ext3 = 7;
int ext_fn1(int x) { return x; }
int ext_fn2(int x) { return x; }

int true_fn() { return 513; }
int false_fn() { return 512; }
int char_fn() { return (2<<8)+3; }
int short_fn() { return (2<<16)+5; }

int uchar_fn() { return (2<<10)-1-4; }
int ushort_fn() { return (2<<20)-1-7; }

int schar_fn() { return (2<<10)-1-4; }
int sshort_fn() { return (2<<20)-1-7; }

int add_all(int n, ...)
{
	va_list ap;
	va_start(ap, n);

	int sum = 0;
	for (int i = 0; i < n; i++)
		sum += va_arg(ap, int);

	va_end(ap);
	return sum;
}

float add_float(float x, float y)
{
	return x + y;
}

double add_double(double x, double y)
{
	return x + y;
}

int add10_int(int x1, int x2, int x3, int x4, int x5, int x6, int x7, int x8, int x9, int x10)
{
	return x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10;
}

float add10_float(float x1, float x2, float x3, float x4, float x5, float x6, float x7, float x8, float x9, float x10)
{
	return x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10;
}

double add10_double(double x1, double x2, double x3, double x4, double x5, double x6, double x7, double x8, double x9, double x10)
{
	return x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10;
}

float add19_float(float x1, float x2, float x3, float x4, float x5, float x6, float x7, float x8, float x9, float x10,
		  float x11, float x12, float x13, float x14, float x15, float x16, float x17, float x18, float x19)
{
	return x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10 + x11 + x12 + x13 + x14 + x15 + x16 + x17 + x18 + x19;
}
