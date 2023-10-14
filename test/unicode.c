#include "test.h"

#define STR(x) #x

int main()
{
	ASSERT(4, sizeof(L'\0'));
	ASSERT(97, L'a');

	ASSERT(0, strcmp("αβγ", "\u03B1\u03B2\u03B3"));
	ASSERT(0, strcmp("日本語", "\u65E5\u672C\u8A9E"));
	ASSERT(0, strcmp("日本語", "\U000065E5\U0000672C\U00008A9E"));
	ASSERT(0, strcmp("中文", "\u4E2D\u6587"));
	ASSERT(0, strcmp("中文", "\U00004E2D\U00006587"));
	ASSERT(0, strcmp("🌮", "\U0001F32E"));

	ASSERT(-1, L'\xffffffff'>>31);
	ASSERT(946, L'β');
	ASSERT(12354, L'あ');
	ASSERT(20013, L'中');
	ASSERT(127843, L'🍣');

	ASSERT(2, sizeof(u'\0'));
	ASSERT(1, u'\xffff'>>15);
	ASSERT(97, u'a');
	ASSERT(946, u'β');
	ASSERT(12354, u'あ');
	ASSERT(62307, u'🍣');

	ASSERT(0, strcmp(STR(u'a'), "u'a'"));

	ASSERT(4, sizeof(U'\0'));
	ASSERT(1, U'\xffffffff'>>31);
	ASSERT(97, U'a');
	ASSERT(946, U'β');
	ASSERT(12354, U'あ');
	ASSERT(127843, U'🍣');

	ASSERT(0, strcmp(STR(U'a'), "U'a'"));

	pass();
	return 0;
}
