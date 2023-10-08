#include "toycc.h"

// Encode a given character in UTF-8.
// wide character => utf-8 code
int encode_utf8(char *buf, uint32_t c)
{
	// for ASCII
	if (c <= 0x7F) {
		// bit 0-6
		buf[0] = c;
		return 1;
	}

	// for \u
	if (c <= 0x7FF) {
		// bit 6-10
		buf[0] = 0b11000000 | (c >> 6);
		// bit 0-5
		buf[1] = 0b10000000 | (c & 0b00111111);
		return 2;
	}

	if (c <= 0xFFFF) {
		// bit 12-15
		buf[0] = 0b11100000 | (c >> 12);
		// bit 6-11
		buf[1] = 0b10000000 | ((c >> 6) & 0b00111111);
		// bit 0-5
		buf[2] = 0b10000000 | (c & 0b00111111);
		return 3;
	}

	// for \U
	// bit 18-21
	buf[0] = 0b11110000 | (c >> 18);
	// bit 12-17
	buf[1] = 0b10000000 | ((c >> 12) & 0b00111111);
	// bit 6-11
	buf[2] = 0b10000000 | ((c >> 6) & 0b00111111);
	// bit 0-5
	buf[3] = 0b10000000 | (c & 0b00111111);
	return 4;
}

// Read a UTF-8-encoded Unicode code point from a source file.
// We assume that source files are always in UTF-8.
//
// UTF-8 is a variable-width encoding in which one code point is
// encoded in one to four bytes. One byte UTF-8 code points are
// identical to ASCII. Non-ASCII characters are encoded using more
// than one byte.
//
// utf-8 code => wide character
uint32_t decode_utf8(const char **new_pos, const char *p)
{
	// for ASCII
	if ((unsigned char)*p < 128) {
		*new_pos = p + 1;
		return *p;
	}

	const char *start = p;
	int len;
	uint32_t c;

	if ((unsigned char)*p >= 0b11110000) {
		// 4-byte utf-8, the first byte is 0b11110xxx
		len = 4;
		c = *p & 0b111;
	} else if ((unsigned char)*p >= 0b11100000) {
		// 3-byte utf-8, the first byte is 0b1110xxxx
		len = 3;
		c = *p & 0b1111;
	} else if ((unsigned char)*p >= 0b11000000) {
		// 2-byte utf-8, the first byte is 0b110xxxxx
		len = 2;
		c = *p & 0b11111;
	} else {
		error_at(start, "invalid UTF-8 sequence");
	}

	// the following byte is 0b10xxxxxx
	for (int i = 1; i < len; i++) {
		if ((unsigned char)p[i] >> 6 != 0b10)
			error_at(start, "invalid UTF-8 sequence");

		c = (c << 6) | (p[i] & 0b111111);
	}

	*new_pos = p + len;
	return c;
}
