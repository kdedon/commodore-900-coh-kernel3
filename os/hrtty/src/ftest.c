#include <stdio.h>
#define START 0x80
#define END 0xcf

main()
{
	unsigned	char c;
	for(c = START; c <= END; c++) putchar(c);
}

