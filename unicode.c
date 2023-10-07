#include "toycc.h"

// Encode a given character in UTF-8.
// universal character => utf-8 code
int encode_utf8(char *buf, uint32_t c)
{
	// for char
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
