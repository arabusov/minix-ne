/*
 * Simple dirty version of hexdump
 * Written by avr in 2021
 * NO WARRANTY, you own risk, all of this stuff (c)
 * do WTF you want
 */

#include <stdio.h>

void print_buf (char *buf, size_t size)
{
	size_t i=0;
	if (!buf) return;
	for (i=0; i<size; i++) {
		if ((i&0x0f)==0)
			printf ("%04x:   ", i);
		printf ("%02x ", buf[i]&0x00ff);
		if ((i&0x0f)==0xf)
			printf("\n");
	}
	printf ("\n");
}

int main (int argc, char** argv)
{
	FILE *f;
	char buf [16384];
	size_t size=0;
	char *usage = "Usage: hexdump FILE\n";
	if (argc != 2) {
		printf ("%s", usage);
		return 1;
	}
	f = fopen (argv[1], "r");
	if (!f) {
		fprintf (stderr, "Can't open file %s\n", argv[1]);
		return 2;
	}
	while (fread (buf+(size++), 1, 1, f));
	print_buf (buf, size);
	return 0;
}
