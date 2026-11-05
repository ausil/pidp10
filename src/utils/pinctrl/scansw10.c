/*
 * scansw10 - read the PiDP-10 maintenance panel switch register.
 *
 * The original lived in src/scansw10/ and drove the matrix by mmap()ing the
 * GPIO controller registers, which is why it printed "MUST BE RUN WITH SUDO"
 * and worked on exactly one distribution. Reading 90 switches through the
 * kernel GPIO character device needs no privileges at all, and the same
 * program then also runs on a Pi 5 (RP1 chip) and on a machine with no
 * cabinet at all (sim backend), which the register mapping never could.
 *
 * Contract, relied on by install/fedora/libexec/pidp10-engine:
 *
 *   scansw10           stdout: the 18-bit switch register, decimal, active
 *                      high (a bit set means that switch is pressed)
 *   scansw10 --matrix  human readable dump of all five rows
 *   exit status        0 = the matrix was read, nonzero = no panel available
 *
 * (The old tool reported the register in its exit status. A shell cannot read
 * 18 bits out of an exit code - it truncates to 8 - and the only consumer did
 * exactly that, so the value is on stdout now and the exit status means what
 * an exit status is for.)
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pidp_panel.h"

#define SW_MASK ((1u << PIDP_PANEL_COLS) - 1)

static void usage(const char *argv0)
{
	printf("usage: %s [--matrix]\n"
	       "  default   print the switch register (row 0) as a decimal number\n"
	       "  --matrix  print all %d rows of %d switches as octal\n"
	       "\n"
	       "Reads the maintenance panel switches. Honours PIDP10_PANEL and\n"
	       "PIDP10_GPIO_CHIP; PIDP10_PANEL=sim reports an all-pressed matrix.\n",
	       argv0, PIDP_PANEL_SW_ROWS, PIDP_PANEL_COLS);
}

int main(int argc, char **argv)
{
	uint32_t rowbits[PIDP_PANEL_SW_ROWS];
	pidp_panel_config c = { NULL, NULL, NULL, 0, 50000, 5000, NULL, 0 };
	int matrix = 0, row, rc;

	if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		usage(argv[0]);
		return 0;
	}
	if (argc > 1) {
		if (strcmp(argv[1], "--matrix")) {
			fprintf(stderr, "scansw10: unknown option '%s'\n", argv[1]);
			usage(argv[0]);
			return 2;
		}
		matrix = 1;
	}

	pidp_panel_default_config(&c);
	if (pidp_panel_open(&c) != 0) {
		fprintf(stderr, "scansw10: no front panel available "
		        "(PIDP10_PANEL=%s)\n", getenv("PIDP10_PANEL") ?
		        getenv("PIDP10_PANEL") : "auto");
		return 1;
	}

	/* Columns become inputs with pull-ups; a closed switch pulls the line
	 * down, so a low bit means pressed and the register is the inverse of
	 * what the backend reports. */
	pidp_panel_inputs(1);
	for (row = 0; row < PIDP_PANEL_SW_ROWS; row++)
		rowbits[row] = ~pidp_panel_row_read(row) & SW_MASK;

	if (matrix) {
		printf("panel      : %s\n", pidp_panel_name());
		for (row = 0; row < PIDP_PANEL_SW_ROWS; row++)
			printf("row %d (%02o): %07o\n", row, row, rowbits[row]);
	} else {
		printf("%u\n", rowbits[0]);
	}

	rc = 0;
	pidp_panel_park();
	pidp_panel_close();
	return rc;
}
