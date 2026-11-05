/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Front panel backend: kernel GPIO character device (libgpiod).
 *
 * This is the only backend that can work on a distribution kernel: Fedora's
 * aarch64 kernel ships no gpiomem driver and CONFIG_STRICT_DEVMEM forbids
 * mapping the GPIO registers from userspace, so the register-poking backend
 * is dead there. /dev/gpiochipN is the supported interface.
 *
 * HARD SAFETY RULE - auto-detection must say no.
 *   The panel is wired to legacy BCM numbers 4,17,27,22,21,20,... which are
 *   only "the same thing" on a Raspberry Pi controller. /dev/gpiochipN exists
 *   on plenty of machines that are not PiDP-10 cabinets (a Qualcomm laptop has
 *   fourteen of them, most of them PMIC rails that drive real power
 *   hardware). Claiming line 22 of some random PMIC gpiochip because it
 *   "looks free" would be a bug with smoke coming out of the machine, so a
 *   chip is only auto-selected when its name/label identifies a Broadcom
 *   controller (see pi_controller()). Everything else needs an explicit
 *   PIDP10_GPIO_CHIP, which is the operator overriding the check on purpose.
 *
 * Line offsets are used verbatim as legacy BCM GPIO numbers, which holds for
 * both pinctrl-bcm2835 (Pi 0-4) and pinctrl-rp1 (Pi 5, 40-pin header bank).
 *
 * One request holds all 22 lines for the lifetime of the panel; a whole
 * frame row is driven with as few LINE_SET_VALUES ioctls as the API allows.
 * The button scan needs a direction change (LINE_CONFIG reconfigure), which
 * is why pidp_panel_scan_due() exists.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gpiod.h>

#include "pidp_panel.h"

#define PANEL_LINES   (PIDP_PANEL_ADDR + PIDP_PANEL_COLS + 1)

struct gpiod_state {
	struct gpiod_chip         *chip;
	struct gpiod_line_request *req;
	struct gpiod_line_config  *lc_out;   /* cols as outputs (LEDs)      */
	struct gpiod_line_config  *lc_in;    /* cols as inputs, pull-up     */
	unsigned int               offs[PANEL_LINES];
	unsigned int               offs_rows[PIDP_PANEL_ADDR];
	unsigned int               offs_cols[PIDP_PANEL_COLS];
	char                       path[64];
	int                        cols_input;  /* cached direction of cols  */
	int                        dead;        /* ioctl failure: stay quiet */
	unsigned long              errors;
};

static struct gpiod_state S;

/* ------------------------------------------------------------ chip probe */

/*
 * Operator override for a panel that is genuinely not on a Broadcom
 * controller (a GPIO expander, a non-Pi SBC). Deliberately a *second* setting
 * besides PIDP10_GPIO_CHIP: one typo in a conf file must not be enough to
 * start driving a PMIC power rail.
 */
static int chip_forced(void)
{
	const char *f = getenv("PIDP10_PANEL_FORCE");

	/* Lower-case spellings only; this is a footgun-disarmament flag, not a
	 * place to be generous about typing. */
	return f && (!strcmp(f, "1") || !strcmp(f, "y") || !strcmp(f, "yes") ||
	             !strcmp(f, "on") || !strcmp(f, "true"));
}

/*
 * Does this chip belong to a Broadcom GPIO controller, i.e. is a legacy BCM
 * GPIO number also a valid line offset on it? Matched on both name and label
 * because the kernel fills them differently per platform (and on non-Pi
 * machines the label is an OF node path like "c42d000.spmi:pmic@0:gpio@8800",
 * which must NOT match).
 */
static int pi_controller(const char *name, const char *label)
{
	static const char *const hits[] = {
		"brcm", "bcm2835", "bcm2708", "bcm2709", "bcm2711", "bcm2712",
		"raspberry", "rp1", NULL
	};
	const char *const *p;

	for (p = hits; *p; p++) {
		if ((name  && strstr(name,  *p)) ||
		    (label && strstr(label, *p)))
			return 1;
	}
	return 0;
}

/* Free the lines we need and belong to a Pi controller? */
static int chip_usable(struct gpiod_chip *chip, const unsigned int *offs,
                       unsigned int want_lines, int maxoff,
                       int forced, int verbose)
{
	struct gpiod_chip_info *ci;
	const char *name, *label;
	unsigned int nlines, i;
	int needed = maxoff + 1;

	ci = gpiod_chip_get_info(chip);
	if (!ci)
		return 0;
	name  = gpiod_chip_info_get_name(ci);
	label = gpiod_chip_info_get_label(ci);
	nlines = gpiod_chip_info_get_num_lines(ci);

	if ((int)nlines < needed) {
		if (verbose)
			fprintf(stderr, "pidp_panel: %s has %u lines, need %d\n",
			        gpiod_chip_get_path(chip), nlines, needed);
		gpiod_chip_info_free(ci);
		return 0;
	}
	if (!forced && !pi_controller(name, label)) {
		if (verbose)
			fprintf(stderr, "pidp_panel: refusing %s (%s / %s): not a "
			        "Broadcom GPIO controller. If this chip really does "
			        "carry the panel, say so twice: PIDP10_GPIO_CHIP plus "
			        "PIDP10_PANEL_FORCE=1\n",
			        gpiod_chip_get_path(chip), name, label);
		gpiod_chip_info_free(ci);
		return 0;
	}
	gpiod_chip_info_free(ci);

	/* Someone else already owns one of our pins (a kernel driver, a
	 * leftover pinctrl-gpio, the previous engine that did not die):
	 * better to run headless than to fight over a line. */
	for (i = 0; i < want_lines; i++) {
		struct gpiod_line_info *li = gpiod_chip_get_line_info(chip, offs[i]);

		if (!li) {
			if (verbose)
				fprintf(stderr, "pidp_panel: %s has no line %u\n",
				        gpiod_chip_get_path(chip), offs[i]);
			return 0;
		}
		if (gpiod_line_info_is_used(li)) {
			if (verbose)
				fprintf(stderr, "pidp_panel: %s line %u is busy "
				        "(consumer %s)\n",
				        gpiod_chip_get_path(chip), offs[i],
				        gpiod_line_info_get_consumer(li) ? gpiod_line_info_get_consumer(li) : "?");
			gpiod_line_info_free(li);
			return 0;
		}
		gpiod_line_info_free(li);
	}
	return 1;
}

static struct gpiod_chip *open_named(const char *want)
{
	char path[64];
	struct gpiod_chip *chip;

	if (!want || !*want)
		return NULL;
	if (!strncmp(want, "/dev/", 5))
		return gpiod_chip_open(want);
	snprintf(path, sizeof path, "/dev/%s", want);           /* "gpiochip0" */
	chip = gpiod_chip_open(path);
	if (chip)
		return chip;
	snprintf(path, sizeof path, "/dev/gpiochip%s", want);  /* "0"         */
	return gpiod_chip_open(path);
}

/* Find the chip carrying the panel. Returns it open, or NULL. */
static struct gpiod_chip *find_chip(const unsigned int *offs, unsigned int n,
                                    int maxoff, const char *want)
{
	struct gpiod_chip *chip;
	char path[64];
	/* Naming the chip is necessary but not sufficient: the vendor check only
	 * steps aside when the operator also sets PIDP10_PANEL_FORCE. */
	int forced = (want && *want) && chip_forced();
	int i;

	chip = open_named(want);
	if (chip) {
		if (chip_usable(chip, offs, n, maxoff, forced, 1))
			return chip;
		gpiod_chip_close(chip);
		return NULL;                   /* operator asked, operator errs */
	}
	if (forced) {
		fprintf(stderr, "pidp_panel: cannot open gpiochip '%s'\n", want);
		return NULL;
	}

	for (i = 0; i < 64; i++) {
		snprintf(path, sizeof path, "/dev/gpiochip%d", i);
		chip = gpiod_chip_open(path);
		if (!chip)
			continue;
		if (chip_usable(chip, offs, n, maxoff, 0, 0))
			return chip;
		gpiod_chip_close(chip);
	}
	return NULL;
}

/* ------------------------------------------------------------- line config */

static struct gpiod_line_config *mk_config(struct gpiod_line_settings *out,
                                           struct gpiod_line_settings *in,
                                           int cols_input)
{
	struct gpiod_line_config *lc;
	enum gpiod_line_value vals[PANEL_LINES];
	unsigned int i;

	lc = gpiod_line_config_new();
	if (!lc)
		return NULL;

	if (gpiod_line_config_add_line_settings(lc, S.offs_rows,
	                                        PIDP_PANEL_ADDR, out) ||
	    gpiod_line_config_add_line_settings(lc, &S.offs[PIDP_PANEL_ADDR +
	                                          PIDP_PANEL_COLS], 1, out) ||
	    gpiod_line_config_add_line_settings(lc, S.offs_cols,
	                                        PIDP_PANEL_COLS,
	                                        cols_input ? in : out)) {
		gpiod_line_config_free(lc);
		return NULL;
	}

	/* Park state: rows high (nothing selected), latch high (drivers
	 * deselected), columns high (no LED current, buttons released).
	 * Applies the instant the lines are claimed or reconfigured, so a
	 * probe never flashes the panel. */
	for (i = 0; i < PANEL_LINES; i++)
		vals[i] = GPIOD_LINE_VALUE_ACTIVE;
	if (gpiod_line_config_set_output_values(lc, vals, PANEL_LINES)) {
		gpiod_line_config_free(lc);
		return NULL;
	}
	return lc;
}

static void free_state(void)
{
	if (S.req) { gpiod_line_request_release(S.req); S.req = NULL; }
	if (S.lc_out) { gpiod_line_config_free(S.lc_out); S.lc_out = NULL; }
	if (S.lc_in)  { gpiod_line_config_free(S.lc_in);  S.lc_in  = NULL; }
	if (S.chip)   { gpiod_chip_close(S.chip);         S.chip   = NULL; }
	S.cols_input = 0;
	S.dead = 0;
	S.errors = 0;
}

static int fail(int rv, const char *what)
{
	fprintf(stderr, "pidp_panel: gpiod %s failed: %s\n", what, strerror(errno));
	free_state();
	return rv;
}

/* -------------------------------------------------------------- operations */

static void gpiod_fail(const char *what)
{
	S.errors++;
	if (!S.dead) {
		S.dead = 1;
		fprintf(stderr, "pidp_panel: gpiod %s failed (%s); panel dark\n",
		        what, strerror(errno));
	}
}

static void gpiod_inputs(int on)
{
	if (S.dead || !S.req || S.cols_input == on)
		return;
	if (gpiod_line_request_reconfigure_lines(S.req, on ? S.lc_in : S.lc_out)) {
		gpiod_fail(on ? "reconfigure to input" : "reconfigure to output");
		return;
	}
	S.cols_input = on;
}

static void gpiod_row_write(int row, uint32_t leds)
{
	enum gpiod_line_value v[PIDP_PANEL_COLS];
	unsigned int i;

	if (S.dead || !S.req)
		return;

	/* Rows first while the latch is still high, then the columns: the
	 * drivers only look at the data when the strobe is asserted, so
	 * changing both "at once" cannot ghost a neighbouring row. */
	for (i = 0; i < PIDP_PANEL_ADDR; i++)
		if (gpiod_line_request_set_value(S.req, S.offs_rows[i],
		    (row & (1 << i)) ? GPIOD_LINE_VALUE_ACTIVE
		                     : GPIOD_LINE_VALUE_INACTIVE)) {
			gpiod_fail("row select");
			return;
		}
	for (i = 0; i < PIDP_PANEL_COLS; i++)
		v[i] = (leds & (1u << i)) ? GPIOD_LINE_VALUE_INACTIVE   /* lit  */
		                          : GPIOD_LINE_VALUE_ACTIVE;   /* dark */
	if (gpiod_line_request_set_values_subset(S.req, PIDP_PANEL_COLS,
	                                         S.offs_cols, v)) {
		gpiod_fail("column write");
		return;
	}

	if (gpiod_line_request_set_value(S.req, S.offs[PANEL_LINES - 1],
	                                 GPIOD_LINE_VALUE_INACTIVE))   /* strobe */
		{ gpiod_fail("latch assert"); return; }
	pidp_panel_delay(cfg_hold_ns());
	if (gpiod_line_request_set_value(S.req, S.offs[PANEL_LINES - 1],
	                                 GPIOD_LINE_VALUE_ACTIVE))     /* release */
		{ gpiod_fail("latch release"); return; }
	pidp_panel_delay(cfg_gap_ns());
}

static uint32_t gpiod_row_read(int row)
{
	enum gpiod_line_value v[PIDP_PANEL_COLS];
	uint32_t sw = 0;
	unsigned int i;

	if (S.dead || !S.req || !S.cols_input)
		return (1u << PIDP_PANEL_COLS) - 1;   /* nothing pressed */

	for (i = 0; i < PIDP_PANEL_ADDR; i++)
		if (gpiod_line_request_set_value(S.req, S.offs_rows[i],
		    (row & (1 << i)) ? GPIOD_LINE_VALUE_ACTIVE
		                     : GPIOD_LINE_VALUE_INACTIVE)) {
			gpiod_fail("row select (read)");
			return (1u << PIDP_PANEL_COLS) - 1;
		}
	pidp_panel_delay(cfg_hold_ns() / 10);          /* let the matrix settle */

	if (gpiod_line_request_get_values_subset(S.req, PIDP_PANEL_COLS,
	                                         S.offs_cols, v)) {
		gpiod_fail("column read");
		return (1u << PIDP_PANEL_COLS) - 1;
	}
	for (i = 0; i < PIDP_PANEL_COLS; i++)
		if (v[i] == GPIOD_LINE_VALUE_ACTIVE)
			sw |= 1u << i;                 /* high == not pressed */
	return sw;
}

static void gpiod_park(void)
{
	unsigned int i;

	if (!S.req)
		return;
	for (i = 0; i < PIDP_PANEL_ADDR; i++)
		(void) gpiod_line_request_set_value(S.req, S.offs_rows[i],
		                                    GPIOD_LINE_VALUE_ACTIVE);
	(void) gpiod_line_request_set_value(S.req, S.offs[PANEL_LINES - 1],
	                                    GPIOD_LINE_VALUE_ACTIVE);
	gpiod_inputs(1);                               /* columns tri-stated */
}

/* ------------------------------------------------------------------ open */

static int gpiod_open(const pidp_panel_config *c)
{
	struct gpiod_line_settings *out, *in;
	struct gpiod_request_config *rq;
	int i, maxoff;

	if (S.req)
		return 0;                                       /* already open */

	for (i = 0; i < PIDP_PANEL_ADDR; i++)
		S.offs[i] = (unsigned int)c->rows[i];
	for (i = 0; i < PIDP_PANEL_COLS; i++) {
		S.offs[PIDP_PANEL_ADDR + i] = (unsigned int)c->cols[i];
		S.offs_cols[i] = S.offs[PIDP_PANEL_ADDR + i];
	}
	S.offs[PANEL_LINES - 1] = (unsigned int)c->latch;
	for (i = 0; i < PIDP_PANEL_ADDR; i++)
		S.offs_rows[i] = S.offs[i];

	maxoff = 0;
	for (i = 0; i < PANEL_LINES; i++)
		if ((int)S.offs[i] > maxoff)
			maxoff = S.offs[i];
	if (maxoff < 0 || maxoff > 511)
		return -EINVAL;

	S.chip = find_chip(S.offs, PANEL_LINES, maxoff, cfg_chip());
	if (!S.chip)
		return -ENODEV;
	snprintf(S.path, sizeof S.path, "%s", gpiod_chip_get_path(S.chip));

	out = gpiod_line_settings_new();
	in  = gpiod_line_settings_new();
	rq  = gpiod_request_config_new();
	if (!out || !in || !rq) {
		if (out) gpiod_line_settings_free(out);
		if (in)  gpiod_line_settings_free(in);
		if (rq)  gpiod_request_config_free(rq);
		return fail(-ENOMEM, "out of memory for line settings");
	}

	gpiod_line_settings_set_direction(out, GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_settings_set_drive(out, GPIOD_LINE_DRIVE_PUSH_PULL);
	gpiod_line_settings_set_bias(out, GPIOD_LINE_BIAS_DISABLED);
	gpiod_line_settings_set_output_value(out, GPIOD_LINE_VALUE_ACTIVE);

	gpiod_line_settings_set_direction(in, GPIOD_LINE_DIRECTION_INPUT);
	gpiod_line_settings_set_bias(in, GPIOD_LINE_BIAS_PULL_UP);
	gpiod_line_settings_set_edge_detection(in, GPIOD_LINE_EDGE_NONE);

	gpiod_request_config_set_consumer(rq, "pidp10-panel");

	S.lc_out = mk_config(out, in, 0);
	S.lc_in  = mk_config(out, in, 1);
	gpiod_line_settings_free(out);
	gpiod_line_settings_free(in);

	if (!S.lc_out || !S.lc_in) {
		gpiod_request_config_free(rq);
		return fail(-ENOMEM, "cannot build line config");
	}

	S.req = gpiod_chip_request_lines(S.chip, rq, S.lc_out);
	gpiod_request_config_free(rq);
	if (!S.req) {
		int rv = errno;

		/* EBUSY here means a race with another claimant: chip_usable()
		 * saw the lines free and someone else won. Report it as "no
		 * panel" so auto mode degrades to the simulator instead of
		 * killing the engine, but always say so: a half-claimed panel is
		 * the one failure mode that is otherwise invisible. */
		if (cfg_explicit() || rv == EBUSY || rv == EPERM || rv == EACCES)
			fprintf(stderr, "pidp_panel: %s: cannot claim panel lines (%s)\n",
			        S.path, strerror(rv));
		free_state();
		return (rv == EBUSY || rv == EPERM || rv == EACCES) ? -ENODEV : -EIO;
	}
	S.cols_input = 0;

	/* Start from the parked, blank state (also leaves the columns ready to
	 * become inputs on the first button scan). */
	gpiod_park();
	return 0;
}

static void gpiod_close(void)
{
	if (S.req)
		gpiod_park();
	free_state();
}

/* ------------------------------------------------------------- diagnostics */

/* Default PiDP-10 wiring, used by the standalone probe tool when no panel is
 * open. Deliberately does not touch S - probing must never disturb a live
 * panel. */
static void default_offs(unsigned int *offs)
{
	static const int rows[PIDP_PANEL_ADDR] = { 4, 17, 27 };
	static const int cols[PIDP_PANEL_COLS] = { 21,20,16,12,7,8,25,24,23,
	                                           18,10,9,11,5,6,13,19,26 };
	unsigned int i;

	for (i = 0; i < PIDP_PANEL_ADDR; i++)
		offs[i] = rows[i];
	for (i = 0; i < PIDP_PANEL_COLS; i++)
		offs[PIDP_PANEL_ADDR + i] = cols[i];
	offs[PANEL_LINES - 1] = 22;
}

int pidp_panel_gpiod_probe(char *buf, size_t len)
{
	unsigned int offs[PANEL_LINES], i, maxoff = 0;
	struct gpiod_chip *chip;
	const char *want;
	int rv = -ENODEV;

	default_offs(offs);
	for (i = 0; i < PANEL_LINES; i++)
		if ((int)offs[i] > (int)maxoff)
			maxoff = offs[i];

	/* May run without an open panel, so read the environment itself rather
	 * than the selector's copy of the open request. */
	want = getenv("PIDP10_GPIO_CHIP");
	if (want && !*want)
		want = NULL;
	chip = find_chip(offs, PANEL_LINES, (int)maxoff, want);
	if (chip) {
		if (buf && len)
			snprintf(buf, len, "gpiod(%s)", gpiod_chip_get_path(chip));
		rv = 0;
		gpiod_chip_close(chip);
	} else if (buf && len)
		snprintf(buf, len, "none");
	return rv;
}

int pidp_panel_gpiod_list(void)
{
	unsigned int offs[PANEL_LINES];
	char path[64];
	int n, found = 0;

	default_offs(offs);
	for (n = 0; n < 64; n++) {
		struct gpiod_chip_info *ci;
		struct gpiod_chip *chip;
		unsigned int i, used = 0, nlines = 0;
		const char *name = "?", *label = "?";

		snprintf(path, sizeof path, "/dev/gpiochip%d", n);
		chip = gpiod_chip_open(path);
		if (!chip)
			continue;
		found++;
		ci = gpiod_chip_get_info(chip);
		if (ci) {
			name  = gpiod_chip_info_get_name(ci);
			label = gpiod_chip_info_get_label(ci);
			nlines = (unsigned int)gpiod_chip_info_get_num_lines(ci);
		}
		for (i = 0; i < PANEL_LINES; i++) {
			struct gpiod_line_info *li =
				gpiod_chip_get_line_info(chip, offs[i]);

			if (li) {
				if (gpiod_line_info_is_used(li))
					used++;
				gpiod_line_info_free(li);
			}
		}
		printf("%-16s name=%-14s lines=%3u panel-lines-busy=%2u %-34s %s\n",
		       path, name, nlines, used,
		       pi_controller(name, label) ? "[Broadcom GPIO controller]"
		                                  : chip_forced()
		                                    ? "[forced: PIDP10_PANEL_FORCE]"
		                                    : "[refused: not a Pi controller]",
		       label);
		if (ci)
			gpiod_chip_info_free(ci);
		gpiod_chip_close(chip);
	}
	if (!found)
		printf("no /dev/gpiochipN device found (kernel without GPIO cdev?)\n");
	return found;
}

const struct pidp_panel_backend pidp_panel_gpiod = {
	.name      = "gpiod",
	.open      = gpiod_open,
	.close     = gpiod_close,
	.inputs    = gpiod_inputs,
	.row_write = gpiod_row_write,
	.row_read  = gpiod_row_read,
	.park      = gpiod_park,
};
