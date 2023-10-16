#include "test.h"

int main()
{
#line 500 "foo"
	ASSERT(501, __LINE__);
	ASSERT(0, strcmp(__FILE__, "foo"));

#line 800 "bar"
	ASSERT(801, __LINE__);
	ASSERT(0, strcmp(__FILE__, "bar"));

#line 1
	ASSERT(2, __LINE__);

	pass();
	return 0;
}
