/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Front panel backend: legacy register mapping via pinctrl's gpiolib.
 *
 * This is what ka10_pipanel.c used to do directly: mmap the GPIO controller
 * registers out of /dev/gpiomem (falling back to /dev/mem) and poke the
 * function-select, direction, pull and level registers.
 *
 * It only works where that mapping is permitted, i.e. Raspberry Pi OS with
 * its own kernel: /dev/gpiomem exists, and CONFIG_STRICT_DEVMEM is off so the
 * /dev/mem fallback still works. On Fedora it fails in open() with ENODEV and
 * the selector falls back to the simulator - which is exactly why this file
 * exists next to pidp_panel_gpiod.c instead of being the only path.
 *
 * Kept because it is measurably faster than the character device (no ioctl per
 * line change) and the PiDP-10 cabinets in the field all run the vendor OS.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gpiolib.h"
#include "pidp_panel.h"

static pidp_panel_config cfg;
static int               opened;
static int               cols_input;

static void gpiomem_park(void);

static int gpiomem_open(const pidp_panel_config *c)
{
	int row, col, num_gpios, ret;

	if (opened)
		return 0;
	cfg = *c;

	/* gpiolib_init() parses the device tree and returns the number of GPIOs
	 * (negative on error, 0 when no known Broadcom pinctrl node was found).
	 * PIDP10_GPIO_CHIP means the compatible/name for this backend and the
	 * gpiochip device for the gpiod one; an unrecognised value is not fatal,
	 * it just falls back to probing, so one conf file serves both.
	 */
	num_gpios = 0;
	if (cfg.chip && *cfg.chip && strncmp(cfg.chip, "/dev/", 5))
		num_gpios = gpiolib_init_by_name(cfg.chip);
	if (num_gpios <= 0) {
		if (cfg.chip && *cfg.chip && cfg_explicit())
			fprintf(stderr, "pidp_panel: gpiomem: no chip '%s', "
			        "probing the device tree\n", cfg.chip);
		num_gpios = gpiolib_init();
	}
	if (num_gpios <= 0) {
		if (cfg_explicit())
			fprintf(stderr, "pidp_panel: gpiomem: no Broadcom GPIO "
			        "controller in the device tree (%d)\n", num_gpios);
		return -ENODEV;
	}

	ret = gpiolib_mmap();
	if (ret) {
		if (!cfg_explicit())
			return -ENODEV;
		if (ret == EACCES && geteuid())
			fprintf(stderr, "pidp_panel: gpiomem: no permission on "
			        "/dev/gpiomem or /dev/mem (are you root, or in the "
			        "right group?)\n");
		else
			fprintf(stderr, "pidp_panel: gpiomem: cannot map GPIO "
			        "registers (%s)\n", strerror(ret));
		return -ENODEV;
	}

	/* Same initial state as the gpiod backend: everything parked, LEDs dark,
	 * column drivers unstressed. */
	gpio_set_fsel(cfg.latch, GPIO_FSEL_OUTPUT);
	gpio_set_dir(cfg.latch, DIR_OUTPUT);
	gpio_set_drive(cfg.latch, DRIVE_HIGH);

	for (row = 0; row < PIDP_PANEL_ADDR; row++) {
		gpio_set_fsel(cfg.rows[row], GPIO_FSEL_OUTPUT);
		gpio_set_dir(cfg.rows[row], DIR_OUTPUT);
		gpio_set_drive(cfg.rows[row], DRIVE_HIGH);
	}
	for (col = 0; col < PIDP_PANEL_COLS; col++) {
		gpio_set_fsel(cfg.cols[col], GPIO_FSEL_INPUT);
		gpio_set_pull(cfg.cols[col], PULL_UP);
	}
	cols_input = 1;
	opened = 1;
	return 0;
}

static void gpiomem_close(void)
{
	if (opened)
		gpiomem_park();
	opened = 0;
}

static void gpiomem_inputs(int on)
{
	int i;

	if (!opened || cols_input == on)
		return;
	for (i = 0; i < PIDP_PANEL_COLS; i++)
		gpio_set_dir(cfg.cols[i], on ? DIR_INPUT : DIR_OUTPUT);
	cols_input = on;
}

static void gpiomem_row_write(int row, uint32_t leds)
{
	int i;

	if (!opened)
		return;
	/* Deselect while the data changes (identical order to upstream). */
	gpio_set_drive(cfg.latch, DRIVE_HIGH);
	for (i = 0; i < PIDP_PANEL_ADDR; i++)
		gpio_set_drive(cfg.rows[i],
		                 (row & (1 << i)) ? DRIVE_HIGH : DRIVE_LOW);
	for (i = 0; i < PIDP_PANEL_COLS; i++)
		gpio_set_drive(cfg.cols[i],
		                 (leds & (1u << i)) ? DRIVE_LOW : DRIVE_HIGH);
	gpio_set_drive(cfg.latch, DRIVE_LOW);                  /* strobe */
	pidp_panel_delay(cfg.hold_ns);
	gpio_set_drive(cfg.latch, DRIVE_HIGH);                 /* blank */
	pidp_panel_delay(cfg.gap_ns);
}

static uint32_t gpiomem_row_read(int row)
{
	uint32_t sw = 0;
	int i;

	if (!opened)
		return (1u << PIDP_PANEL_COLS) - 1;

	for (i = 0; i < PIDP_PANEL_ADDR; i++)
		gpio_set_drive(cfg.rows[i],
		                 (row & (1 << i)) ? DRIVE_HIGH : DRIVE_LOW);
	pidp_panel_delay(cfg.hold_ns / 10);
	for (i = 0; i < PIDP_PANEL_COLS; i++)
		if (gpio_get_level(cfg.cols[i]))
			sw |= 1u << i;
	return sw;
}

static void gpiomem_park(void)
{
	int i;

	if (!opened)
		return;
	gpio_set_drive(cfg.latch, DRIVE_HIGH);
	for (i = 0; i < PIDP_PANEL_COLS; i++)
		gpio_set_dir(cfg.cols[i], DIR_INPUT);
	for (i = 0; i < PIDP_PANEL_ADDR; i++)
		gpio_set_drive(cfg.rows[i], DRIVE_HIGH);
	cols_input = 1;
}

const struct pidp_panel_backend pidp_panel_gpiomem = {
	.name      = "gpiomem",
	.open      = gpiomem_open,
	.close     = gpiomem_close,
	.inputs    = gpiomem_inputs,
	.row_write = gpiomem_row_write,
	.row_read  = gpiomem_row_read,
	.park      = gpiomem_park,
};
