/* SPDX-License-Identifier: BSD-2-Clause
 *
 * pidp_panel.h - front panel (blinkenlights + buttons) access for the PiDP-10.
 *
 * WHY THIS EXISTS
 *   ka10_pipanel.c used to poke the panel through pinctrl's gpiolib, which
 *   mmaps the GPIO controller registers out of /dev/gpiomem (falling back to
 *   /dev/mem). That is fine on Raspberry Pi OS and dead on Fedora:
 *     * Fedora's generic aarch64 kernel builds no gpiomem driver at all
 *       (grep -i gpiomem /boot/config-$(uname -r) -> nothing),
 *     * CONFIG_STRICT_DEVMEM=y and CONFIG_IO_STRICT_DEVMEM=y refuse to map
 *       device memory into a userspace process,
 *     * on a Pi 5 the panel GPIOs live on the RP1 I/O chip, which is a PCIe
 *       endpoint; the register address cannot even be derived from the
 *       SoC-side device tree that gpiolib_init() parses.
 *   The kernel GPIO character device (/dev/gpiochipN, libgpiod) is the only
 *   portable way in, so all panel I/O goes behind this API and gets a
 *   per-backend implementation. Nothing else in the simulator should call
 *   gpiolib_* or gpiod_* directly.
 *
 * HARDWARE
 *   The panel is a 22-line GPIO matrix, scanned by a SCHED_FIFO thread:
 *     rows[3]  row select (binary, active low per bit)
 *     cols[18] shared column lines: outputs while driving LEDs (active low),
 *              inputs with pull-up while reading the button matrix
 *     latch    strobes the column latch into the LED/switch drivers
 *   GPIO numbers are the legacy BCM numbers (4, 17, 27, 22, 21, 20, ...) as
 *   wired on the Pi header; on both Pi 4 (pinctrl-bcm2711) and Pi 5
 *   (pinctrl-rp1) those are also the line offsets of the gpiochip, which is
 *   what makes the libgpiod backend possible without a pin table.
 *
 * TIMING
 *   One frame = 7 rows, each lit for hold_ns with gap_ns of dead time, then a
 *   switch scan. At the upstream defaults (50us/5us) that is ~2.3k frames/s
 *   and ~270 line operations - on paper. In practice nanosleep() on a kernel
 *   without high-resolution timers (and without the SCHED_FIFO the mux thread
 *   asks for, which needs root) rounds every delay up to roughly 200us, which
 *   caps the real rate near 600 frames/s. That is still comfortably past the
 *   ~50 Hz at which the LEDs stop flickering, but measure before believing
 *   any timing claim: `pidp-gpio-probe --selftest` reports what the loop
 *   actually achieves. PIDP10_SWITCH_HZ lets the (much more expensive)
 *   direction reconfiguration for the switch scan be rate-limited.
 */

#ifndef PIDP_PANEL_H
#define PIDP_PANEL_H

#include <stdint.h>
#include <stddef.h>

/* Build-time capability, set by the makefile (see src/pdp10/makefile). They
 * are defaulted here rather than per-inclusion so every translation unit
 * agrees on which backends exist - getting this wrong would link a selector
 * that disagrees with the objects that were compiled. */
#ifndef PIDP10_HAVE_GPIOD
#define PIDP10_HAVE_GPIOD 0
#endif
#ifndef PIDP10_HAVE_GPIOMEM
#define PIDP10_HAVE_GPIOMEM 0
#endif

/*
 * The matrix is 3 row-select lines x 18 shared column lines, which is NOT the
 * same as the number of rows the software multiplexes:
 *
 *   PIDP_PANEL_ADDR     3 binary select lines, driven from the row number
 *   PIDP_PANEL_COLS    18 lines, outputs (LEDs) or inputs (buttons)
 *   PIDP_PANEL_LED_ROWS 7 LED rows, scanned as frames (ka10_pipanel.c rows 0-6)
 *   PIDP_PANEL_SW_ROWS  5 switch rows, scanned after the LEDs (rows 0-4)
 *
 * Getting these apart matters: row 3 is the momentary pushbuttons and row 4
 * the stop conditions, so a loop that stops at the address-line count silently
 * reads "all buttons held down".
 */
#define PIDP_PANEL_ADDR      3
#define PIDP_PANEL_COLS     18
#define PIDP_PANEL_LED_ROWS  7
#define PIDP_PANEL_SW_ROWS   5

typedef struct pidp_panel_config {
	const char *chip;      /* gpiochip name or /dev path; NULL = auto-detect */
	const int  *rows;      /* PIDP_PANEL_ADDR legacy GPIO numbers            */
	const int  *cols;      /* PIDP_PANEL_COLS legacy GPIO numbers            */
	int         latch;     /* latch/strobe line                              */
	long        hold_ns;   /* how long each row stays lit                    */
	long        gap_ns;    /* dead time after unlatching, before next row    */
	const char *backend;   /* "gpiod"|"gpiomem"|"sim"|NULL=auto              */
	int         explicit_hw;/* the operator named a hardware backend, so a
	                         * failure is news; in auto mode it is not      */
} pidp_panel_config;

/* Backend vtable: one file per way of reaching the hardware, dispatched to by
 * the selector in pidp_panel.c. Every operation must tolerate being called
 * when the hardware went away mid-run (a USB GPIO dongle, or simply an ioctl
 * that starts failing): the panel must be able to go dark without taking the
 * simulated PDP-10 down with it. */
struct pidp_panel_backend {
	const char *name;
	int  (*open)(const pidp_panel_config *cfg);
	void (*close)(void);
	void (*inputs)(int on);
	void (*row_write)(int row, uint32_t leds);
	uint32_t (*row_read)(int row);
	void (*park)(void);
};

/* Open the panel. Returns 0 on success, negative errno on failure.
 * Never aborts the simulator: callers print a message and continue headless. */
int  pidp_panel_open(const pidp_panel_config *cfg);

/* Release everything acquired by pidp_panel_open(). Idempotent. */
void pidp_panel_close(void);

/* Name of the active backend ("gpiod(/dev/gpiochip1)", ...), for banners. */
const char *pidp_panel_name(void);

/* Direction of the column lines.
 *   on = 0 : columns are outputs (LED driving)
 *   on = 1 : columns are inputs with pull-up (button matrix read)
 * Cheap if the requested state matches the current one. */
void pidp_panel_inputs(int on);

/* Light one row: bit i of `leds` set == LED i of that row ON. Includes the
 * latch strobe and the hold/gap delays, i.e. exactly one iteration of the
 * multiplexing loop. `row` is a LED row, 0 .. PIDP_PANEL_LED_ROWS-1. */
void pidp_panel_row_write(int row, uint32_t leds);

/* Raw column levels for the given row select: bit i set == column line HIGH.
 * Callers keep the active-low inversion (the buttons are active low).
 * `row` is a switch row, 0 .. PIDP_PANEL_SW_ROWS-1, which is more rows than
 * the LED scan's 3 select-line count. Only valid after pidp_panel_inputs(1). */
uint32_t pidp_panel_row_read(int row);

/* Park the panel on shutdown: latch high, columns released to inputs, rows
 * high - the state that leaves the LEDs dark and the drivers unstressed. */
void pidp_panel_park(void);

/* Rate limiter for the button scan, which is the only expensive part on
 * libgpiod (it needs a direction reconfiguration). Returns non-zero when the
 * caller should call read_sw(). Disabled (always returns 1, upstream
 * behaviour) unless PIDP10_SWITCH_HZ is set in the environment. */
int  pidp_panel_scan_due(void);

/* Shared helper for backends: sleep ns (restarts on EINTR). */
void pidp_panel_delay(long ns);

/* Backends read the open request through these: the selector owns the copy,
 * so a backend never keeps a pointer into the caller's stack. */
const char *cfg_chip(void);
long        cfg_hold_ns(void);
long        cfg_gap_ns(void);
int         cfg_explicit(void);   /* hardware was requested by name */

/* Diagnostics for pidp-gpio-probe (implemented by the gpiod backend). Both
 * are side-effect free: they open, inspect and close chips without ever
 * claiming a line, so they can be run while an engine owns the panel. */
int  pidp_panel_gpiod_probe(char *buf, size_t len);   /* 0 = panel chip found */
int  pidp_panel_gpiod_list(void);                     /* prints chips         */

#endif /* PIDP_PANEL_H */
