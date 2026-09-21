/*
 * toydf.c -- the library's code reaching its own exported data, through
 * slots ld fills and the fixup list relocates.
 */
struct toybox {
	int	tb_a;
	int	tb_b;
};

extern struct toybox	toyd_box;
extern char		toyd_arr[];
extern readonly char	toyd_tab[];

int
toyd_sum()
{
	return (toyd_box.tb_a + toyd_box.tb_b + toyd_arr[0]);
}

int
toyd_set(v)
int v;
{
	toyd_box.tb_b = v;
	toyd_arr[1] = v + 1;
	return (toyd_box.tb_a);
}

char *
toyd_where()
{
	return ((char *)&toyd_box);
}

/*
 * The same, for the shared table.
 */
char *
toyd_rwhere()
{
	return ((char *)toyd_tab);
}

int
toyd_rsum()
{
	int i, n;

	n = 0;
	for (i = 0; i < 8; i++)
		n += toyd_tab[i];
	return (n);
}
