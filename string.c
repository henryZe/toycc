#include <toycc.h>

#define INIT_CAPACITY 8

// Takes a printf-style format string and returns a formatted string.
const char *format(const char *fmt, ...)
{
	char *buf;
	size_t buflen;
	FILE *out = open_memstream(&buf, &buflen);

	va_list ap;
	va_start(ap, fmt);

	vfprintf(out, fmt, ap);

	va_end(ap);
	fclose(out);

	return buf;
}

void strarray_push(struct StringArray *arr, const char *s)
{
	if (!arr->data) {
		arr->data = malloc(INIT_CAPACITY * sizeof(char *));
		arr->capacity = INIT_CAPACITY;
	}

	if (arr->capacity == arr->len) {
		arr->data = realloc(arr->data, sizeof(char *) * 2 * arr->capacity);
		arr->capacity *= 2;

		for (int i = arr->len; i < arr->capacity; i++)
			arr->data[i] = NULL;
	}

	arr->data[arr->len++] = s;
}
