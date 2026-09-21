/*
 * toyd.c -- the toy data library's objects, apart from toydf.c so its
 * references cross a module boundary.
 *
 * toyd_box and toyd_arr are per client, in the private image; toyd_tab is
 * `readonly', one copy in the shared segment.
 */
struct toybox {
	int	tb_a;
	int	tb_b;
};

struct toybox	toyd_box = { 11, 22 };
char		toyd_arr[8];
readonly char	toyd_tab[8] = { 2, 3, 5, 7, 11, 13, 17, 19 };
