/*
 * toy.c -- the toy shared library.
 *
 *	toy_add		a leaf, also called internally
 *	toy_bump	writes per-client private data
 *	toy_name	a string literal, which lands in the private image
 *	toy_where	address of the private counter
 *	toy_code	address of toy_add, the same in every client
 */
int	toy_count;

int
toy_add(a, b)
int a, b;
{
	return (a + b);
}

int
toy_bump(k)
int k;
{
	toy_count = toy_add(toy_count, k);
	return (toy_count);
}

char *
toy_name()
{
	return ("toy library v1");
}

char *
toy_where()
{
	return ((char *)&toy_count);
}

char *
toy_code()
{
	return ((char *)toy_add);
}
