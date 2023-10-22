#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static const char *reset = "\33[0m";
static const char *red = "\33[31m";
static const char *green = "\33[32m";

void assert(int expected, int actual, const char *code)
{
	if (expected != actual) {
		printf("%s%s => %d expected but got %d%s\n",
			red, code, expected, actual, reset);
		exit(1);
	}

	printf("%s => %d\n", code, actual);
}

void pass(void)
{
	printf("%sOK%s\n", green, reset);
}

static int static_fn() { return 5; }
int ext1 = 5;
int *ext2 = &ext1;
int ext3 = 7;
int ext_fn1(int x) { return x; }
int ext_fn2(int x) { return x; }
int common_ext2 = 3;
static int common_local = 1;

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

typedef struct { int a,b; short c; char d; } Ty4;
typedef struct { int a; float b; double c; } Ty5;
typedef struct { unsigned char a[3]; } Ty6;
typedef struct { long a, b, c; } Ty7;

int struct_test4(Ty4 x, int n)
{
	switch (n) {
	case 0: return x.a;
	case 1: return x.b;
	case 2: return x.c;
	default: return x.d;
	}
}

int struct_test5(Ty5 x, int n)
{
	switch (n) {
	case 0: return x.a;
	case 1: return x.b;
	default: return x.c;
	}
}

int struct_test6(Ty6 x, int n)
{
	return x.a[n];
}

int struct_test7(Ty7 x, int n)
{
	switch (n) {
	case 0: return x.a;
	case 1: return x.b;
	default: return x.c;
	}
}

// 单个成员变量的结构体
typedef struct {_Bool a;} StTy1_1;
typedef struct {short a;} StTy1_2;
typedef struct {unsigned a;} StTy1_3;
typedef struct {long a;} StTy1_4;

int struct_type_1_1_test(StTy1_1 x) { return x.a; }
int struct_type_1_2_test(StTy1_2 x) { return x.a; }
int struct_type_1_3_test(StTy1_3 x) { return x.a; }
int struct_type_1_4_test(StTy1_4 x) { return x.a; }

// 使用一个寄存器的结构体
typedef struct {char a;char b;char c;char d;char e;char f;char g;char h;} StTy2_1;
typedef struct {int a;int b;} StTy2_2;
typedef struct {unsigned a;unsigned b;} StTy2_3;

int struct_type_2_1_test(StTy2_1 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; case 2: return x.c;case 3: return x.d;case 4: return x.e;case 5:return x.f;case 6: return x.g;case 7: return x.h; default: return -1; }}
int struct_type_2_2_test(StTy2_2 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; default: return -1; }}
int struct_type_2_3_test(StTy2_3 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; default: return -1; }}

typedef struct {char a;char b;char c;char d;int e;} StTy3_1;
typedef struct {char a;char b;char c;int d;} StTy3_2;
typedef struct {char a;short b;char c;short d;} StTy3_3;

int struct_type_3_1_test(StTy3_1 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; case 2: return x.c;case 3: return x.d;case 4: return x.e;default: return -1; }}
int struct_type_3_2_test(StTy3_2 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; case 2: return x.c;case 3: return x.d;default: return -1; }}
int struct_type_3_3_test(StTy3_3 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; case 2: return x.c;case 3: return x.d;default: return -1; }}

// 使用两个寄存器的结构体
typedef struct {char a;char b;char c;char d;int e;char f;int g;} StTy4_1;
typedef struct {char a;char b;char c;int d;char e;} StTy4_2;
typedef struct {char a;short b;char c;short d;char e;short f;} StTy4_3;
typedef struct {char a;short b;char c;short d;int e;short f;char g;} StTy4_4;

int struct_type_4_1_test(StTy4_1 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; case 2: return x.c;case 3: return x.d;case 4: return x.e;case 5: return x.f;case 6: return x.g;default: return -1; }}
int struct_type_4_2_test(StTy4_2 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; case 2: return x.c;case 3: return x.d;case 4: return x.e;default: return -1; }}
int struct_type_4_3_test(StTy4_3 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; case 2: return x.c;case 3: return x.d;case 4: return x.e;case 5: return x.f;default: return -1; }}
int struct_type_4_4_test(StTy4_4 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; case 2: return x.c;case 3: return x.d;case 4: return x.e;case 5: return x.f;case 6: return x.g;default: return -1; }}

// 使用地址传递的结构体
typedef struct {long a;long b;long c;} StTy5_1;
typedef struct {long a;long b;long c;long d;long e;long f;long g;long h;} StTy5_2;

int struct_type_5_1_test(StTy5_1 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; case 2: return x.c;default: return -1; }}
int struct_type_5_2_test(StTy5_2 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; case 2: return x.c;case 3: return x.d;case 4: return x.e;case 5:return x.f;case 6: return x.g;case 7: return x.h; default: return -1; }}
int struct_type_5_3_test(StTy5_1 x, StTy5_1 y, int n) {switch(n){case 0: return x.a; case 1: return x.b; case 2: return x.c;case 3: return y.a; case 4: return y.b; case 5: return y.c; default: return -1; }}
int struct_type_5_4_test(int aa, StTy5_1 x, int bb, StTy5_1 y, int n) {int cc = 123; switch(n){case 0: return x.a; case 1: return x.b; case 2: return x.c;case 3: return y.a; case 4: return y.b; case 5: return y.c; default: return -1; int dd=456; }}

// 掺杂浮点的结构体（成员数>=3）
typedef struct {char a;float b;char c;} StTy6_1;

int struct_type_6_1_test(StTy6_1 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; case 2: return x.c;default: return -1; }}

// 掺杂浮点的结构体（成员数==1）
typedef struct {float a;} StTy7_1;
typedef struct {double a;} StTy7_2;

int struct_type_7_1_test(StTy7_1 x) { return x.a; }
int struct_type_7_2_test(StTy7_2 x) { return x.a; }

// 掺杂浮点的结构体（成员数==2）
typedef struct {float a;char b;} StTy8_1;
typedef struct {int a;double b;} StTy8_2;
typedef struct {float a;float b;} StTy8_3;
typedef struct {int a;float b;} StTy8_4;

int struct_type_8_1_test(StTy8_1 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; default: return -1; }}
int struct_type_8_2_test(StTy8_2 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; default: return -1; }}
int struct_type_8_3_test(StTy8_3 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; default: return -1; }}
int struct_type_8_4_test(StTy8_4 x, int n) {switch(n){case 0: return x.a; case 1: return x.b; default: return -1; }}

// 栈传递两个寄存器的结构体（整体）
typedef struct {long a;char b;} StTy9_1;
int struct_type_9_1_test(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, StTy9_1 x, int n) {switch (n) {case 0:return x.a;case 1:return x.b;default:return -1;}}

// 栈传递两个寄存器的结构体（一半）
int struct_type_10_1_test(int a0, int a1, int a2, int a3, int a4, int a5, int a6, StTy9_1 x, int n) { switch (n) { case 0: return x.a; case 1: return x.b; default: return -1; } }
// 栈传递两个寄存器的结构体（整体，含浮点）
int struct_type_11_1_test(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, StTy8_4 x, int n) { switch (n) { case 0: return x.a; case 1: return x.b; default: return -1; } }
int struct_type_11_2_test(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, StTy8_2 x, int n) { switch (n) { case 0: return x.a; case 1: return x.b; default: return -1; } }
int struct_type_11_3_test(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, StTy8_1 x, int n) { switch (n) { case 0: return x.a; case 1: return x.b; default: return -1; } }
// 栈传递两个寄存器的结构体（一半，含浮点）
int struct_type_12_1_test(int a0, int a1, int a2, int a3, int a4, int a5, int a6, StTy8_4 x, int n) { switch (n) { case 0: return x.a; case 1: return x.b; default: return -1; } }

// 结构体包含浮点数组
typedef struct {float a[2];} StTy13_1;
int struct_type_13_1_test(StTy13_1 x, int n) {return x.a[n];}

// 联合体
typedef union {float a; int b;} UnTy1_1;
typedef union {float a; int b; long c;} UnTy1_2;
int union_type_1_1_test(UnTy1_1 x, int n) {switch (n) { case 0: return x.a; case 1: return x.b; default: return -1;}}
int union_type_1_2_test(UnTy1_2 x, int n) {switch (n) { case 0: return x.a; case 1: return x.b; case 2: return x.c; default: return -1;}}

Ty4 struct_test24(void) {
  return (Ty4){10, 20, 30, 40};
}

Ty5 struct_test25(void) {
  return (Ty5){10, 20, 30};
}

Ty6 struct_test26(void) {
  return (Ty6){10, 20, 30};
}

typedef struct { unsigned char a[10]; } Ty20;
typedef struct { unsigned char a[20]; } Ty21;

Ty20 struct_test27(void) {
  return (Ty20){10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
}

Ty21 struct_test28(void) {
  return (Ty21){1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
}

// 支持可变参数函数接受任意数量的参数
int sum2_3(float b, int x, ...)
{
	va_list ap;
	va_start(ap, x);
	x += b;

	for (;;) {
		double y = va_arg(ap, double);
		x += y;

		int z = va_arg(ap, int);
		if (z == 0)
			return x;
		x += z;
	}
}

int sum2_5(int a0, float fa0, int a1, int a2, int a3, int a4, float fa1, int a5,
	   int a6, int a7, int x, ...)
{
	x += fa0;
	x += fa1;

	x += a0;
	x += a1;
	x += a2;
	x += a3;
	x += a4;
	x += a5;
	x += a6;
	x += a7;

	va_list ap;
	va_start(ap, x);

	for (;;) {
		int z = va_arg(ap, int);
		if (z == 0)
			return x;
		x += z;
	}
}
