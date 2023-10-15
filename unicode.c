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

static bool in_range(uint32_t *range, uint32_t c)
{
	for (int i = 0; range[i] != (uint32_t)-1; i += 2)
		if (range[i] <= c && c <= range[i + 1])
			return true;
	return false;
}

// [https://www.sigbus.info/n1570#D]
// C11 allows not only ASCII but some multibyte characters in certain
// Unicode ranges to be used in an identifier.
//
// This function returns true if a given character is acceptable as
// the first character of an identifier.
//
// For example, ¾ (U+00BE) is a valid identifier because characters in
// 0x00BE-0x00C0 are allowed, while neither ⟘ (U+27D8) nor '　'
// (U+3000, full-width space) are allowed because they are out of range.
bool is_ident1(uint32_t c)
{
	static uint32_t range1[] = {
		'_', '_',
		'a', 'z',
		'A', 'Z',
		0x00A8, 0x00A8,
		0x00AA, 0x00AA,
		0x00AD, 0x00AD,
		0x00AF, 0x00AF,
		0x00B2, 0x00B5,
		0x00B7, 0x00BA,
		0x00BC, 0x00BE,
		0x00C0, 0x00D6,
		0x00D8, 0x00F6,
		0x00F8, 0x00FF,
		0x0100, 0x02FF,
		0x0370, 0x167F,
		0x1681, 0x180D,
		0x180F, 0x1DBF,
		0x1E00, 0x1FFF,
		0x200B, 0x200D,
		0x202A, 0x202E,
		0x203F, 0x2040,
		0x2054, 0x2054,
		0x2060, 0x206F,
		0x2070, 0x20CF,
		0x2100, 0x218F,
		0x2460, 0x24FF,
		0x2776, 0x2793,
		0x2C00, 0x2DFF,
		0x2E80, 0x2FFF,
		0x3004, 0x3007,
		0x3021, 0x302F,
		0x3031, 0x303F,
		0x3040, 0xD7FF,
		0xF900, 0xFD3D,
		0xFD40, 0xFDCF,
		0xFDF0, 0xFE1F,
		0xFE30, 0xFE44,
		0xFE47, 0xFFFD,
		0x10000, 0x1FFFD,
		0x20000, 0x2FFFD,
		0x30000, 0x3FFFD,
		0x40000, 0x4FFFD,
		0x50000, 0x5FFFD,
		0x60000, 0x6FFFD,
		0x70000, 0x7FFFD,
		0x80000, 0x8FFFD,
		0x90000, 0x9FFFD,
		0xA0000, 0xAFFFD,
		0xB0000, 0xBFFFD,
		0xC0000, 0xCFFFD,
		0xD0000, 0xDFFFD,
		0xE0000, 0xEFFFD,
		-1,
	};

	return in_range(range1, c);
}

// Returns true if a given character is acceptable as a non-first
// character of an identifier.
bool is_ident2(uint32_t c)
{
	static uint32_t range2[] = {
		'0', '9',
		0x0300, 0x036F,
		0x1DC0, 0x1DFF,
		0x20D0, 0x20FF,
		0xFE20, 0xFE2F,
		-1,
	};

	return is_ident1(c) || in_range(range2, c);
}
