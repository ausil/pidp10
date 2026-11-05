/* SPDX-License-Identifier: BSD-2-Clause
 *
 * pidp-gpio-probe - "is there actually a front panel on this machine?"
 *
 * The answer used to be a shell guess (`gpioinfo | grep gpio`), which is true
 * on every machine that has GPIO at all: a Qualcomm laptop has fourteen
 * gpiochips, several of them PMIC power rails. That made the systemd engine
 * unit believe it had a panel and wait forever for a READ IN button press that
 * nobody could press.
 *
 * So the one copy of the truth lives in pidp_panel_gpiod.c (chip_usable():
 * Broadcom controller, enough lines, all panel lines free) and both the
 * simulator and these scripts ask it.
 *
 * Non-destructive by design: it opens, inspects and closes chips, never
 * requests a line, so it is safe to run while an engine owns the panel.
 *
 * usage:
 *   pidp-gpio-probe              same as --check
 *   pidp-gpio-probe --check      exit 0 iff the panel would use real hardware
 *   pidp-gpio-probe --backend    print the backend the panel would pick
 *   pidp-gpio-probe --list       list gpiochips and whether our lines are free
 *   pidp-gpio-probe --pins       print the panel pin table
 */

#define _POSIX_C_SOURCE 200809L   /* clock_gettime() in -std=c99 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pidp_panel.h"

static const int rows[PIDP_PANEL_ADDR] = { 4, 17, 27 };
static const int cols[PIDP_PANEL_COLS] = { 21,20,16,12,7,8,25,24,23,
                                           18,10,9,11,5,6,13,19,26 };
static const int latch = 22;

static int backend_says_hardware(char *buf, size_t len)
{
	const char *want = getenv("PIDP10_PANEL");

	if (want && !strcmp(want, "sim")) {
		snprintf(buf, len, "sim (PIDP10_PANEL=sim)");
		return 0;
	}
	if (want && !strcmp(want, "gpiomem")) {
		/* Not probeable without mapping registers (and we must not map
		 * them just to ask); trust the operator's explicit choice. */
		snprintf(buf, len, "gpiomem (requested)");
		return 1;
	}
	return pidp_panel_gpiod_probe(buf, len) == 0;
}

static int do_pins(void)
{
	int i;

	printf("row select :");
	for (i = 0; i < PIDP_PANEL_ADDR; i++)
		printf(" GPIO%d", rows[i]);
	printf("\ncolumns    :");
	for (i = 0; i < PIDP_PANEL_COLS; i++)
		printf(" GPIO%d", cols[i]);
	printf("\nlatch      : GPIO%d\n", latch);
	printf("(legacy BCM numbers; == gpiochip line offsets on "
	       "pinctrl-bcm2835 and pinctrl-rp1)\n");
	return 0;
}

/*
 * Drive the multiplexing loop for a while against the simulated backend.
 *
 * This is the answer to "does the front panel code work on a machine with no
 * front panel", and the only way to see the mux loop's real rate away from a
 * cabinet: the theoretical 2.3k frames/s assumes nanosleep() is exact, which
 * without high-resolution timers - and without the SCHED_FIFO priority the mux
 * thread asks for, which needs root - it is not.
 *
 * Forced to the sim backend on purpose: a diagnostic that could claim the
 * panel lines would be a way to knock a running engine's blinkenlights off.
 */
static int do_selftest(int secs)
{
	const uint32_t released = (1u << PIDP_PANEL_COLS) - 1;
	unsigned long rows_done = 0, frames_done = 0, bad_reads = 0, due = 0;
	uint32_t pattern = 0;
	struct timespec t0, now;
	pidp_panel_config c = { NULL, rows, cols, 22, 50000, 5000, "sim", 0 };
	int row;

	if (secs < 1)
		secs = 3;

	if (pidp_panel_open(&c) != 0) {
		fprintf(stderr, "pidp-gpio-probe: cannot open the simulated panel\n");
		return 1;
	}

	/* A backend that reads the buttons as "held down" is worse than none:
	 * the engine would sit there pressing STOP and START forever. */
	pidp_panel_inputs(1);
	for (row = 0; row < 5; row++)
		if (pidp_panel_row_read(row) != released)
			bad_reads++;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		if ((now.tv_sec - t0.tv_sec) >= secs)
			break;
		pidp_panel_inputs(0);
		for (row = 0; row < 7; row++, rows_done++)
			pidp_panel_row_write(row, pattern++ & 0777777u);
		frames_done++;
		pidp_panel_inputs(1);
		for (row = 0; row < 5; row++)
			if (pidp_panel_row_read(row) != released)
				bad_reads++;
	}
	/* And the button-scan rate limiter: it is the only knob that keeps the
	 * character-device backend from spending all its time reconfiguring 18
	 * lines, so check that it does what it claims. */
	clock_gettime(CLOCK_MONOTONIC, &t0);
	due = 0;
	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		if ((now.tv_sec - t0.tv_sec) >= 1)
			break;
		if (pidp_panel_scan_due())
			due++;
	}
	pidp_panel_park();
	pidp_panel_close();

	printf("pidp-gpio-probe: selftest %ds: %lu frames, %lu rows, "
	       "~%lu rows/s, ~%lu frames/s\n",
	       secs, frames_done, rows_done,
	       rows_done / (unsigned long)secs, frames_done / (unsigned long)secs);
	if (bad_reads) {
		printf("pidp-gpio-probe: FAIL: %lu button scans did not read "
		       "'all released'\n", bad_reads);
		return 1;
	}
	printf("pidp-gpio-probe: button scans allowed: %lu/s (PIDP10_SWITCH_HZ=%s)\n",
	       due, getenv("PIDP10_SWITCH_HZ") ? getenv("PIDP10_SWITCH_HZ") : "unset");
	printf("pidp-gpio-probe: OK: multiplexing loop runs, buttons read "
	       "all-released\n");
	if (frames_done / (unsigned long)secs < 100)
		printf("pidp-gpio-probe: note: that is flicker-slow; run as root "
		       "(or grant CAP_SYS_NICE) so the mux thread gets SCHED_FIFO\n");
	return 0;
}

int main(int argc, char **argv)
{
	char buf[128] = "none";
	const char *mode = (argc > 1) ? argv[1] : "--check";
	int hw;

	if (!strcmp(mode, "--list"))
		return pidp_panel_gpiod_list() ? 0 : 1;
	if (!strcmp(mode, "--pins"))
		return do_pins();
	if (!strcmp(mode, "--selftest"))
		return do_selftest(argc > 2 ? atoi(argv[2]) : 3);
	if (!strcmp(mode, "--check") || !strcmp(mode, "--backend")) {
		hw = backend_says_hardware(buf, sizeof(buf));
		if (!strcmp(mode, "--backend"))
			printf("%s\n", hw ? buf : (strstr(buf, "sim") ? buf : "sim"));
		if (!strcmp(mode, "--check"))
			fprintf(stderr, "pidp-gpio-probe: front panel backend: %s\n",
			        hw ? buf : "sim (no panel hardware)");
		return hw ? 0 : 1;
	}

	fprintf(stderr,
	        "usage: pidp-gpio-probe [--check|--backend|--list|--pins|--selftest [s]]\n"
	        "\n"
	        "  --check    exit 0 if the front panel would use real hardware\n"
	        "  --backend  print the backend the panel would pick\n"
	        "  --list     list /dev/gpiochipN and whether the panel lines are free\n"
	        "  --pins     print the panel pin table\n"
	        "  --selftest drive the multiplexing loop on the simulated panel\n"
	        "             (default 3 seconds; never touches hardware)\n"
	        "\n"
	        "Honours PIDP10_PANEL and PIDP10_GPIO_CHIP from the environment.\n");
	return 2;
}
