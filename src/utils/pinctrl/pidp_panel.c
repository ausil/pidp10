/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Backend selector for the PiDP-10 front panel.
 *
 * One panel, three ways to reach it, chosen at run time from the environment
 * so the same binary runs on the Pi (real panel), on a Fedora laptop (sim) and
 * on Raspberry Pi OS (legacy register mapping):
 *
 *   PIDP10_PANEL=gpiod     kernel GPIO char device via libgpiod  <- Fedora/Pi 5
 *   PIDP10_PANEL=gpiomem   mmap of the GPIO registers            <- Raspberry Pi OS
 *   PIDP10_PANEL=sim       no hardware, blinkenlights go to the log
 *   PIDP10_PANEL=auto      gpiod if a usable chip exists, else sim   (default)
 *
 * PIDP10_SWITCH_HZ=<n> rate-limits the button scan (see pidp_panel.h).
 */

#define _POSIX_C_SOURCE 200809L   /* nanosleep(), clock_gettime() in -std=c99 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pidp_panel.h"

extern const struct pidp_panel_backend pidp_panel_sim;
#if PIDP10_HAVE_GPIOD
extern const struct pidp_panel_backend pidp_panel_gpiod;
#endif
#if PIDP10_HAVE_GPIOMEM
extern const struct pidp_panel_backend pidp_panel_gpiomem;
#endif

static const struct pidp_panel_backend *be;
static pidp_panel_config                cfg;
static long                             scan_hz;      /* 0 = unlimited */
static struct timespec                  last_scan;

/* -------------------------------------------------- config for the backends */

const char *cfg_chip(void)
{
	return cfg.chip;
}

long cfg_hold_ns(void)
{
	return cfg.hold_ns;
}

long cfg_gap_ns(void)
{
	return cfg.gap_ns;
}

int cfg_explicit(void)
{
	return cfg.explicit_hw;
}

/* Diagnostics live in the gpiod backend; without libgpiod there is nothing to
 * probe, which the tool reports as "sim" rather than failing to link. */
#if !PIDP10_HAVE_GPIOD
int pidp_panel_gpiod_probe(char *buf, size_t len)
{
	if (buf && len)
		snprintf(buf, len, "none (built without libgpiod)");
	return -ENOSYS;
}

int pidp_panel_gpiod_list(void)
{
	printf("pidp_panel: this build has no libgpiod support\n");
	return 0;
}
#endif


/* ---------------------------------------------------------------- helpers */

void pidp_panel_delay(long ns)
{
	struct timespec ts;

	if (ns <= 0)
		return;
	ts.tv_sec  = ns / 1000000000L;
	ts.tv_nsec = ns % 1000000000L;
	/* nanosleep granularity on a non-RT kernel is ~ms without a busy wait;
	 * the upstream code accepted the same, so keep it simple and let the
	 * SCHED_FIFO thread be as accurate as the kernel allows. */
	while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
		;
}

static int now_due(void)
{
	struct timespec now;
	long long d;

	if (scan_hz <= 0)
		return 1;
	clock_gettime(CLOCK_MONOTONIC, &now);
	d = (long long)(now.tv_sec  - last_scan.tv_sec) * 1000000000LL +
	    (long long)(now.tv_nsec - last_scan.tv_nsec);
	if (d >= (1000000000LL / scan_hz)) {
		last_scan = now;
		return 1;
	}
	return 0;
}

/* --------------------------------------------------------------- dispatch */

int pidp_panel_open(const pidp_panel_config *newcfg)
{
	const char *want;
	int rv = -EINVAL;

	if (!newcfg || !newcfg->rows || !newcfg->cols)
		return -EINVAL;

	cfg = *newcfg;
	if (cfg.hold_ns <= 0) cfg.hold_ns = 50000;   /* upstream defaults */
	if (cfg.gap_ns  <  0) cfg.gap_ns  = 5000;

	want = cfg.backend ? cfg.backend : getenv("PIDP10_PANEL");
	if (!want || !*want) want = "auto";

	/* A named hardware backend means the operator expects it to be there, so
	 * the backends print why they could not be used. In auto mode they keep
	 * quiet and the single summary line below says everything, because on a
	 * machine with no panel the alternative is a wall of "skipping
	 * /dev/gpiochipN" on every boot. */
	cfg.explicit_hw = strcmp(want, "auto") && strcmp(want, "sim");

	scan_hz = 0;
	{
		const char *hz = getenv("PIDP10_SWITCH_HZ");
		if (hz && *hz)
			scan_hz = strtol(hz, NULL, 10);
	}

	if (0)
		;
#if PIDP10_HAVE_GPIOD
	else if (!strcmp(want, "gpiod"))
		be = &pidp_panel_gpiod;
#endif
#if PIDP10_HAVE_GPIOMEM
	else if (!strcmp(want, "gpiomem"))
		be = &pidp_panel_gpiomem;
#endif
	else if (!strcmp(want, "sim"))
		be = &pidp_panel_sim;
	else if (!strcmp(want, "gpiod") || !strcmp(want, "gpiomem")) {
		fprintf(stderr, "pidp_panel: backend '%s' not available in this "
		        "build (rebuild with libgpiod-devel / on Linux)\n", want);
		return -ENOSYS;
	} else if (strcmp(want, "auto")) {
		fprintf(stderr, "pidp_panel: unknown PIDP10_PANEL '%s' "
		        "(use gpiod|gpiomem|sim|auto)\n", want);
		return -EINVAL;
	} else {
		/* auto: prefer real hardware, fall back to the simulator so a
		 * Fedora laptop can still run `pdpcontrol start 0`. */
#if PIDP10_HAVE_GPIOD
		be = &pidp_panel_gpiod;
		rv = be->open(&cfg);
		if (rv == 0)
			return 0;
#endif
#if PIDP10_HAVE_GPIOMEM
		be = &pidp_panel_gpiomem;
		rv = be->open(&cfg);
		if (rv == 0)
			return 0;
#endif
		/* One line for what was looked for, so "no blinkenlights" is
		 * diagnosable from the journal without PIDP10_PANEL=gpiod. */
		fprintf(stderr, "pidp_panel: no front panel hardware (no Broadcom "
		        "GPIO controller on /dev/gpiochipN, no usable /dev/gpiomem); "
		        "set PIDP10_GPIO_CHIP to override\n");
		be = &pidp_panel_sim;
	}

	rv = be->open(&cfg);
	if (rv != 0) {
		/* Explicit request for hardware must not silently become a toy. */
		fprintf(stderr, "pidp_panel: backend %s failed: %s\n",
		        be->name, strerror(-rv));
		be = NULL;
		return rv;
	}
	return 0;
}

void pidp_panel_close(void)
{
	if (be && be->close)
		be->close();
	be = NULL;
}

const char *pidp_panel_name(void)
{
	return be ? be->name : "none";
}

void pidp_panel_inputs(int on)
{
	if (be && be->inputs)
		be->inputs(on);
}

void pidp_panel_row_write(int row, uint32_t leds)
{
	if (be && be->row_write && row >= 0 && row < PIDP_PANEL_LED_ROWS)
		be->row_write(row, leds);
}

uint32_t pidp_panel_row_read(int row)
{
	if (be && be->row_read && row >= 0 && row < PIDP_PANEL_SW_ROWS)
		return be->row_read(row);
	return 0;
}

void pidp_panel_park(void)
{
	if (be && be->park)
		be->park();
}

int pidp_panel_scan_due(void)
{
	return now_due();
}
