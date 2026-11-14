/* SPDX-License-Identifier: MIT */
/* The few kernel definitions the colour console uses, for the host check. */
#include <stddef.h>
void outb(unsigned, unsigned);
typedef struct tim { int t_x; } TIM;
extern long lbolt;
extern unsigned utimer;
