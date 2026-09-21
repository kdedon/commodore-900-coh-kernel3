/*
 * toyup.c -- a library importing a function, a data object and a readonly
 * table from libtoyd.1.  Its `toyd_box' is the same object a client
 * importing it directly sees.
 */
struct toybox {
	int	tb_a;
	int	tb_b;
};

extern struct toybox	toyd_box;
extern readonly char	toyd_tab[];
extern int		toyd_sum();

static int	up_count;

int
up_sum()
{
	return (toyd_sum() + 100);
}

int
up_bump()
{
	return (++up_count);
}

char *
up_where()
{
	return ((char *)&toyd_box);
}

int
up_tab(i)
int i;
{
	return (toyd_tab[i]);
}
