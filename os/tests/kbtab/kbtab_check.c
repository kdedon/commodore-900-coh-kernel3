/*
 * Static, host-buildable check of the Commodore 900 keyboard scancode
 * tables (kbtab.c / kbibmtab.c under sys/z8001/rec and hrtty/src).
 *
 * Links against exactly one of the four ktab[] definitions (selected by
 * run.sh) and asserts, entry by entry, that every scan code in the
 * shared alphanumeric/punctuation block (SC01-SC35, plus SC39 space)
 * decodes to the ASCII byte the US keyboard layout requires -- most
 * pointedly SC28 (apostrophe/quote) and SC29 (backtick/tilde), the pair
 * a mixed-up entry would most easily swap or corrupt.
 *
 * This never touches a display or a font glyph: it reads the same
 * table the kernel's kbintr() reads, so it can tell a keymap byte fault
 * apart from a font glyph that merely looks odd on screen.
 */
#include <stdio.h>
#include <kbtab.h>

struct expect {
	unsigned char	sc;		/* scan code = index into ktab[] */
	unsigned char	lower;
	unsigned char	upper;
	const char	*name;
};

static struct expect table[] = {
	{ 0x01, CESC,	CESC,	"ESC" },
	{ 0x02, '1',	'!',	"1/!" },
	{ 0x03, '2',	'@',	"2/@" },
	{ 0x04, '3',	'#',	"3/#" },
	{ 0x05, '4',	'$',	"4/$" },
	{ 0x06, '5',	'%',	"5/%" },
	{ 0x07, '6',	'^',	"6/^" },
	{ 0x08, '7',	'&',	"7/&" },
	{ 0x09, '8',	'*',	"8/*" },
	{ 0x0A, '9',	'(',	"9/(" },
	{ 0x0B, '0',	')',	"0/)" },
	{ 0x0C, '-',	'_',	"minus/underscore" },
	{ 0x0D, '=',	'+',	"equals/plus" },
	{ 0x0E, '\b',	'\b',	"backspace" },
	{ 0x0F, '\t',	'\t',	"tab" },
	{ 0x10, 'q',	'Q',	"q" },
	{ 0x11, 'w',	'W',	"w" },
	{ 0x12, 'e',	'E',	"e" },
	{ 0x13, 'r',	'R',	"r" },
	{ 0x14, 't',	'T',	"t" },
	{ 0x15, 'y',	'Y',	"y" },
	{ 0x16, 'u',	'U',	"u" },
	{ 0x17, 'i',	'I',	"i" },
	{ 0x18, 'o',	'O',	"o" },
	{ 0x19, 'p',	'P',	"p" },
	{ 0x1A, '[',	'{',	"bracket-left" },
	{ 0x1B, ']',	'}',	"bracket-right" },
	{ 0x1C, '\r',	'\r',	"enter" },
	/* 0x1D CTRL: shift key, no character */
	{ 0x1E, 'a',	'A',	"a" },
	{ 0x1F, 's',	'S',	"s" },
	{ 0x20, 'd',	'D',	"d" },
	{ 0x21, 'f',	'F',	"f" },
	{ 0x22, 'g',	'G',	"g" },
	{ 0x23, 'h',	'H',	"h" },
	{ 0x24, 'j',	'J',	"j" },
	{ 0x25, 'k',	'K',	"k" },
	{ 0x26, 'l',	'L',	"l" },
	{ 0x27, ';',	':',	"semicolon/colon" },
	{ 0x28, '\'',	'"',	"apostrophe/quote" },
	{ 0x29, '`',	'~',	"backtick/tilde" },
	/* 0x2A left shift: shift key, no character */
	{ 0x2B, '\\',	'|',	"backslash/bar" },
	{ 0x2C, 'z',	'Z',	"z" },
	{ 0x2D, 'x',	'X',	"x" },
	{ 0x2E, 'c',	'C',	"c" },
	{ 0x2F, 'v',	'V',	"v" },
	{ 0x30, 'b',	'B',	"b" },
	{ 0x31, 'n',	'N',	"n" },
	{ 0x32, 'm',	'M',	"m" },
	{ 0x33, ',',	'<',	"comma/less" },
	{ 0x34, '.',	'>',	"period/greater" },
	{ 0x35, '/',	'?',	"slash/question" },
	/* 0x36 right shift, 0x37 keypad *, 0x38 alt: not in the shared
	 * block -- keypad-* upper differs between the two table variants
	 * and is out of scope for this ASCII-mapping gate. */
	{ 0x39, ' ',	' ',	"space" },
};

int
main(void)
{
	unsigned int i;
	int fails = 0;
	unsigned int n = sizeof(table) / sizeof(table[0]);

	for (i = 0; i < n; i++) {
		struct expect *e = &table[i];
		KEY *kp = &ktab[e->sc];

		if (kp->k_lower != e->lower) {
			printf("FAIL SC%02X (%s) lower: got 0x%02x want 0x%02x\n",
			    e->sc, e->name,
			    (unsigned)kp->k_lower, (unsigned)e->lower);
			fails++;
		}
		if (kp->k_upper != e->upper) {
			printf("FAIL SC%02X (%s) upper: got 0x%02x want 0x%02x\n",
			    e->sc, e->name,
			    (unsigned)kp->k_upper, (unsigned)e->upper);
			fails++;
		}
	}
	if (fails == 0) {
		printf("ok: %u scan codes match the US ASCII layout\n", n);
		return 0;
	}
	printf("%d mismatch(es) out of %u scan codes\n", fails, n);
	return 1;
}
