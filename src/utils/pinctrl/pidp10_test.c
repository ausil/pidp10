/*
 * pidp10-test - front panel bring-up test.
 *
 * Lights every lamp with random patterns for a while and reports every switch
 * that answers. That is the test for a freshly cabled cabinet: "do all the
 * lamps light up, and does every switch generate a response".
 *
 * The copy this replaces lived in src/pidp10-test/ and reached the hardware by
 * mmap()ing the GPIO controller registers under sudo, so it printed
 * "MUST BE RUN WITH SUDO!", needed a Raspberry-Pi-OS-specific device node,
 * and on a machine with no cabinet it exited with "No GPIO chips found"
 * without telling you anything about the panel code. This version goes through
 * pidp_panel like the simulator does, which means the same binary drives a Pi
 * 4 (gpiod or gpiomem) and a Pi 5 (RP1), and on a laptop reports the frame
 * rate it achieves against the simulated panel - so a dead driver can be told
 * apart from a dead panel.
 *
 *   usage: pidp10-test [SECONDS]        default 120, ^C to stop
 */

#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pidp_panel.h"

#define SW_MASK ((1u << PIDP_PANEL_COLS) - 1)

static volatile uint32_t   led[PIDP_PANEL_LED_ROWS];  /* pattern per LED row   */
static volatile uint32_t   sw[PIDP_PANEL_SW_ROWS];    /* raw column bits       */
static volatile int        terminate;
static volatile unsigned long frames;
static pthread_mutex_t     led_lock = PTHREAD_MUTEX_INITIALIZER;

/* One frame = latch released, 7 rows written, buttons sampled when due. Same
 * sequence as the simulator's mux thread (ka10_pipanel.c), so what this
 * measures is the timing the engine actually gets. */
static void *mux(void *unused)
{
	int row;

	(void)unused;
	while (!terminate) {
		pidp_panel_inputs(0);
		for (row = 0; row < PIDP_PANEL_LED_ROWS; row++) {
			uint32_t v;

			pthread_mutex_lock(&led_lock);
			v = led[row];
			pthread_mutex_unlock(&led_lock);
			pidp_panel_row_write(row, v);
		}
		frames++;

		if (pidp_panel_scan_due()) {
			pidp_panel_inputs(1);
			for (row = 0; row < PIDP_PANEL_SW_ROWS; row++)
				sw[row] = pidp_panel_row_read(row);
		}
	}
	return NULL;
}

static void on_sigint(int sig)
{
	(void)sig;
	terminate = 1;
}

int main(int argc, char **argv)
{
	uint32_t last[PIDP_PANEL_SW_ROWS];
	pidp_panel_config c = { NULL, NULL, NULL, 0, 50000, 5000, NULL, 0 };
	unsigned rows_seen = 0;
	struct timespec t0, now;
	pthread_t thread;
	double elapsed;
	int secs = 120, row, i, rc;

	if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		printf("usage: %s [SECONDS]\n"
		       "Drives the front panel with random LED patterns and prints every\n"
		       "switch that changes state, for SECONDS (default %d).\n"
		       "Honours PIDP10_PANEL and PIDP10_GPIO_CHIP.\n",
		       argv[0], secs);
		return 0;
	}
	if (argc > 1) {
		secs = atoi(argv[1]);
		if (secs < 1 || secs > 3600) {
			fprintf(stderr, "pidp10-test: SECONDS must be 1..3600\n");
			return 2;
		}
	}

	pidp_panel_default_config(&c);
	if (pidp_panel_open(&c) != 0) {
		fprintf(stderr, "pidp10-test: cannot open a front panel "
		        "(PIDP10_PANEL=%s)\n",
		        getenv("PIDP10_PANEL") ? getenv("PIDP10_PANEL") : "auto");
		return 1;
	}

	printf("\nPiDP-10 panel test on the %s backend, %d seconds\n\n",
	       pidp_panel_name(), secs);
	if (!strcmp(pidp_panel_name(), "sim"))
		printf("(simulated panel: the switches below are synthetic noise, "
		       "which is the\n only thing this backend can show you - it "
		       "proves the panel code runs,\n not that the cabinet works)\n\n");
	else
		printf("The lamps should flicker randomly; every switch you toggle "
		       "must\nproduce a line of output. A row that never prints has "
		       "dead\nswitches, a column missing from an otherwise live row "
		       "is a dead\nswitch in that column.\n\n");

	signal(SIGINT, on_sigint);
	if (pthread_create(&thread, NULL, mux, NULL) != 0) {
		perror("pidp10-test: pthread_create");
		pidp_panel_close();
		return 1;
	}

	for (i = 0; i < PIDP_PANEL_SW_ROWS; i++)
		last[i] = 0;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	while (!terminate) {
		int changed = 0;

		pthread_mutex_lock(&led_lock);
		for (row = 0; row < PIDP_PANEL_LED_ROWS; row++)
			led[row] = (uint32_t)random() & SW_MASK;
		pthread_mutex_unlock(&led_lock);

		for (row = 0; row < PIDP_PANEL_SW_ROWS; row++) {
			uint32_t pressed = ~sw[row] & SW_MASK;

			if (pressed == last[row])
				continue;
			last[row] = pressed;
			changed = 1;
			if (pressed)
				rows_seen |= 1u << row;
			printf("row %d: pressed 0%07o\n", row, pressed);
		}
		if (!changed)
			pidp_panel_delay(50000);       /* 20 checks/s is plenty */

		clock_gettime(CLOCK_MONOTONIC, &now);
		elapsed = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;
		if (elapsed >= secs)
			terminate = 1;
	}
	terminate = 1;
	pthread_join(thread, NULL);

	pidp_panel_inputs(0);
	for (row = 0; row < PIDP_PANEL_LED_ROWS; row++)
		pidp_panel_row_write(row, 0);
	pidp_panel_park();
	pidp_panel_close();

	clock_gettime(CLOCK_MONOTONIC, &now);

	elapsed = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;
	if (elapsed <= 0)
		elapsed = 1;
	printf("\n%lu frames in %.0fs = %.0f frames/s (2300 theoretical)\n",
	       frames, elapsed, (double)frames / elapsed);
	for (row = 0; row < PIDP_PANEL_SW_ROWS; row++)
		if (!(rows_seen & (1u << row)))
			printf("row %d: no switch in this row was ever pressed\n", row);
	rc = 0;
	printf("done\n");
	return rc;
}
