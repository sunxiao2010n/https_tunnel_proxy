#include "libgo.h"

/*
	To test the library, include "libgo.h" from an application project
	and call libgoTest().
	
	Do not forget to add the library to Project Dependencies in Visual Studio.
*/

static int s_Test = 0;

int libgoTest()
{
	return ++s_Test;
}