#include "test.h"
#include <stdarg.h>

int sum1(int x, ...)
{
	va_list ap;
	va_start(ap, x);

	for (;;) {
		int y = va_arg(ap, int);
		if (y == 0)
			return x;
		x += y;
	}
}

int sum2(int x, ...)
{
	va_list ap;
	va_start(ap, x);

	for (;;) {
		double y = va_arg(ap, double);
		x += y;

		int z = va_arg(ap, int);
		if (z == 0)
			return x;
		x += z;
	}
}

void fmt(char *buf, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	va_list ap2;
	va_copy(ap2, ap);
	vsprintf(buf, fmt, ap2);
	va_end(buf);
}

int sum2_2(int a, int x, ...)
{
	va_list ap;
	va_start(ap, x);
	x += a;

	for (;;) {
		double y = va_arg(ap, double);
		x += y;

		int z = va_arg(ap, int);
		if (z == 0)
			return x;
		x += z;
	}
	return x;
}

int sum2_3(float b, int x, ...);

int sum2_4(float b, int x, ...)
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
           int a6, int x, ...);

int sum2_6(int a0, float fa0, int a1, int a2, int a3, int a4, float fa1, int a5,
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

int main()
{
	ASSERT(6, sum1(1, 2, 3, 0));
	ASSERT(55, sum1(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0));
	ASSERT(81, sum1(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
			1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
			1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
			1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0));

	ASSERT(21, sum2(1, 2.0, 3, 4.0, 5, 6.0, 0));
	ASSERT(210, sum2(1, 2.0, 3, 4.0, 5, 6.0, 7, 8.0, 9, 10.0, 11, 12.0, 13, 14.0,
			15, 16.0, 17, 18.0, 19, 20.0, 0));
	ASSERT(211, sum2_2(1, 1, 2.0, 3, 4.0, 5, 6.0, 7, 8.0, 9, 10.0, 11, 12.0, 13,
			14.0, 15, 16.0, 17, 18.0, 19, 20.0, 0));
	ASSERT(211, sum2_3(1.0, 1, 2.0, 3, 4.0, 5, 6.0, 7, 8.0, 9, 10.0, 11, 12.0, 13,
			14.0, 15, 16.0, 17, 18.0, 19, 20.0, 0));
	ASSERT(211, sum2_4(1.0, 1, 2.0, 3, 4.0, 5, 6.0, 7, 8.0, 9, 10.0, 11, 12.0, 13,
			14.0, 15, 16.0, 17, 18.0, 19, 20.0, 0));
	ASSERT(302, sum2_5(11.0, 12, 13, 14.0, 15, 16.0, 17, 18.0, 19, 1, 1, 10, 11,
			12, 13, 14, 15, 16, 17, 18, 19, 20, 0));
	ASSERT(302, sum2_6(11.0, 12, 13, 14.0, 15, 16.0, 17, 18.0, 19, 1, 1, 10, 11,
			12, 13, 14, 15, 16, 17, 18, 19, 20, 0));

	ASSERT(0, ({ char buf[100]; fmt(buf, "%d %d", 2, 3); strcmp(buf, "2 3"); }));

	pass();
	return 0;
}
