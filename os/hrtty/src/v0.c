/*
 * Fake video driver to define the
 * entry points.
 */

v0in(c)
int c;
{
	printf("char %x '%c'\n", c, c);
}
v0load()
{
	printf("v0 load\n");
}
v0uload()
{
	printf("v0 unload\n");
}
v0open()
{
	printf("v0 open\n");
}
v0close()
{
	printf("v0 close\n");
}
v0read()
{
	printf("v0 read\n");
}
v0write()
{
	printf("v0 write\n");
}
v0ioctl(dev, com, p)
{
	printf("v0 ioctl\n");
}
