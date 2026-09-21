/*
 * toy2.c -- a second toy library with its own symbols and data, so a client
 * can map two at once.
 */
int	toy2_count;

int
toy2_mul(a, b)
int a, b;
{
	return (a * b);
}

int
toy2_bump(k)
int k;
{
	toy2_count = toy2_count + k;
	return (toy2_count);
}

char *
toy2_name()
{
	return ("toy2 library v1");
}

char *
toy2_code()
{
	return ((char *)toy2_mul);
}
