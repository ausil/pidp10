/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Front panel backend: no hardware.
 *
 * Lets the same binary that drives the PiDP-10 panel run on a Fedora laptop
 * (or a Pi with the case open): the multiplexing thread keeps running, the
 * LED state of each row is remembered for debugging and everything is dropped.
 * Selected by PIDP10_PANEL=sim, or by PIDP10_PANEL=auto when no gpiochip can
 * be opened.
 *
 * Not a toy for shipping broken hardware: with PIDP10_PANEL=gpiod you get a
 * hard error instead of this, so a dead panel can never masquerade as alive.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pidp_panel.h"

static pidp_panel_config cfg;
static uint32_t          row_leds[PIDP_PANEL_LED_ROWS];
static uint32_t          reported[PIDP_PANEL_LED_ROWS];
static int               open_count;
static unsigned long long frames;
static time_t            last_report;

static int sim_open(const pidp_panel_config *c)
{
	cfg = *c;
	open_count = 1;
	frames = 0;
	last_report = time(NULL);
	memset(row_leds, 0, sizeof(row_leds));
	memset(reported, 0, sizeof(reported));
	fprintf(stderr, "pidp_panel: SIMULATED front panel (no GPIO hardware). "
	                "Buttons do nothing; use the simh console instead.\n");
	return 0;
}

static void sim_close(void)
{
	open_count = 0;
}

static void sim_inputs(int on)
{
	(void)on;
}

static void sim_row_write(int row, uint32_t leds)
{
	time_t now;

	row_leds[row] = leds;
	pidp_panel_delay(cfg.hold_ns);
	pidp_panel_delay(cfg.gap_ns);

	frames++;
	now = time(NULL);
	/* Journal-friendly: an identical picture is not news, so print on change
	 * (at most once a second, which is what makes this usable as a virtual
	 * panel while watching `journalctl -f`) plus a liveness line once a
	 * minute that also carries the multiplexing rate. The point is to prove
	 * the loop runs and how fast, not to fill the journal. */
	if (now != last_report &&
	    (memcmp(row_leds, reported, sizeof(row_leds)) != 0 ||
	     now - last_report >= 60)) {
		long rate = (long)(frames / (now - last_report > 0 ?
		                             (unsigned long)(now - last_report) : 1));

		memcpy(reported, row_leds, sizeof(reported));
		last_report = now;
		fprintf(stderr, "pidp_panel: sim ~%ld rows/s (7 per frame) "
		        "r0=%06o r1=%06o r2=%06o\n",
		        rate, row_leds[0] & 0777777, row_leds[1] & 0777777,
		        row_leds[2] & 0777777);
		frames = 0;
	}
}

static uint32_t sim_row_read(int row)
{
	(void)row;
	/* Buttons read as "nothing pressed".
	 *
	 * A column line HIGH means not pressed: callers take ~sw. Returning 0
	 * here would therefore mean every switch is held down at once, which is
	 * not "a dead panel" but "STOP+RESET+START+everything, forever". */
	return (1u << PIDP_PANEL_COLS) - 1;
}

static void sim_park(void)
{
	memset(row_leds, 0, sizeof(row_leds));
}

const struct pidp_panel_backend pidp_panel_sim = {
	.name      = "sim",
	.open      = sim_open,
	.close     = sim_close,
	.inputs    = sim_inputs,
	.row_write = sim_row_write,
	.row_read  = sim_row_read,
	.park      = sim_park,
};
