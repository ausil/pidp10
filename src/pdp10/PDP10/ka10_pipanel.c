/* kx10_pipanel.c: PDP-10 PiDP10 front panel.

   Copyright (c) 2022, Richard Cornwell
           Based on code by Oscar Vermeulen

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell

*/

#if PIDP10

/* The following includes are ok since this code can only be run on a
 * Rasberry PI under Linux.
 *
 * To Build this:
 *    make PIDP10=1 pdp10-ka
 *
 * All panel access goes through pinctrl/pidp_panel.h, which picks a backend
 * at run time (gpiod / gpiomem / sim). Do not call gpiolib_* or gpiod_* from
 * here: that is what made this file unusable on any kernel that does not hand
 * out /dev/gpiomem.
 */
#include <pthread.h>                            /* Needed for pthread */
#include <unistd.h>                             /* Needed for sleep/geteuid */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Console line editing for vm_read(). Upstream assumed libedit because that is
 * what Raspberry Pi OS ships; GNU readline offers the same rl_callback_* API
 * and is the one that is actually installed on Fedora, so accept either. With
 * neither, vm_read() falls back to plain line input (see plain_read_char).
 */
#if defined(HAVE_EDITLINE)
#include <editline/readline.h>
#define PANEL_LINE_EDITING      "libedit"
#define PANEL_USE_CALLBACK_EDIT 1
#elif defined(HAVE_READLINE)
#include <readline/readline.h>
#include <readline/history.h>
#define PANEL_LINE_EDITING      "GNU readline"
#define PANEL_USE_CALLBACK_EDIT 1
#else
#define PANEL_LINE_EDITING      "plain line input"
#define PANEL_USE_CALLBACK_EDIT 0
#endif

#include "pinctrl/pidp_panel.h"
#include "kx10_defs.h"

extern uint64 SW;         /* Switch register */
extern t_addr AS;         /* Address switches */
extern uint64 MB;         /* Memory Bus register */
extern uint64 MI;         /* Memory indicator register */
extern uint8  MI_flag;    /* Memory indicator mode */
extern uint32 FLAGS;      /* Flags register */
extern uint8  RUN;        /* Run flag */
extern uint32 IR;         /* Instruction register */
extern uint32 AC;         /* Accumulator */
extern uint8  IX;         /* Index register */
extern uint8  IND;        /* Indirect flag */
extern t_addr AB;         /* Memory address register */
extern t_addr PC;         /* Program counter register */
#if KA | KI
extern int    nxm_stop;   /* Stop if non-existent memory access */
extern int    adr_cond;   /* Address stop condition */
#endif
extern uint8  IOB_PI;     /* Pending Interrupt requests */
extern uint8  PIR;        /* Current Interrupt requests */
extern uint8  PIH;        /* Currently held interrupts */
extern uint8  PIE;        /* Currently enabled interrupts */
extern int    pi_enable;  /* Interrupt system enabled */
extern uint8  prog_stop;  /* Program stop */
extern uint8  examine_sw; /* Examine memory */
extern uint8  deposit_sw; /* Deposit memory */
extern uint8  sing_inst_sw;/* Execute single instruction */
extern uint8  xct_sw;     /* Execute an instruction */
extern uint8  stop_sw;    /* Stop the simulator */
extern uint8  MI_disable; /* Disable MI display */
extern int    watch_stop; /* Memory Stop */
extern uint32 rdrin_dev;  /* Read in device. */
extern uint8  MI_disable; /* Disable MI */
int           repeat_sw;  /* Repeat switch state */
int           par_stop;   /* Parity stop */
int           pwr_off;    /* Power off system */
int           rep_rate;   /* Rate of repeat function */
int           rep_count;  /* Count down to repeat trigger */

/* led row 0 */
#define MB_MASK0             RMASK       /* 18-35 */
#define MB_V_0               0           /* right */

/* led row 1 */
#define MB_MASK1             LMASK       /* 0-17 */
#define MB_V_1               18          /* right */

/* led row 2 */
#define AB_MASK2             RMASK       /* 18-35 */
#define AB_V_2               0           /* right */

/* led row 3 */
#define IX_MASK3             017 
#define IX_V_3               0            /* left */
#define IND_LAMP             020
#define AC_MASK3             017
#define AC_V_3               5            /* left */
#define IR_MASK3             0777         /* 0-9 */
#define IR_V_3               9            /* left */

/* led row 4 */
#define PC_MASK4             RMASK        /* 18-35 */
#define PC_V_4               0            /* right */

/* led row 5 */
#define PI_IOB_MASK5         0177
#define PI_IOB_V_5           7            /* left */
#define PI_ENB_MASK5         0177
#define PI_ENB_V_5           0            /* left */
#define PROG_STOP_LAMP       0040000
#define USER_LAMP            0100000
#define MEM_STOP_LAMP        0200000
#define PWR_LAMP             0400000

/* led row 6 */
#define PI_REQ_MASK6         0177
#define PI_REQ_V_6           0            /* left */
#define PI_PRO_MASK6         0177
#define PI_PRO_V_6           7            /* left */
#define RUN_LAMP             0040000
#define PION_LAMP            0100000
#define PI_LAMP              0200000
#define MI_LAMP              0400000

/* switch row 0 */
#define SR_MASK0             RMASK
#define SR_V_0               0           /* Left */

/* switch row 1 */
#define SR_MASK1             LMASK
#define SR_V_1               18          /* Left */

/* Switch row 2 */
#define MA_SW_MASK3          RMASK
#define MA_SW_V_3            0           /* Left */

/* Switch row 3 */
#define EXAM_NEXT            000001        /* SW=0 */
#define EXAM_THIS            000002        /* SW=1 */
#define XCT_SW               000004        /* SW=2 Set xct_inst */
#define RESET_SW             000010        /* SW=3 Call reset */
#define STOP_SW              000020        /* SW=4 Set RUN = 0 */
#define CONT_SW              000040        /* SW=5 call sim_instr */
#define START_SW             000100        /* SW=6 Call reset then sim_instr */
#define READ_SW              000200        /* SW=7 Boot function */
#define DEP_NEXT             000400        /* SW=8 */
#define DEP_THIS             001000        /* SW=9 */

/* Switch row 4 */
#define ADR_BRK_SW           000001       /* Address Break */
#define ADR_STOP_SW          000002       /* Address stop */
#define WRITE_SW             000004       /* Write stop */
#define DATA_FETCH           000010       /* Data fetch stop */
#define INST_FETCH           000020       /* Instruct fetch stop */
#define REP_SW               000040       /* Repeat switch */
#define NXM_STOP             000100       /* set nxm_stop */
#define PAR_STOP             000200       /* Nop */
#define SING_CYCL            000400       /* Nop */
#define SING_INST            001000       /* set sing_inst */

int    xrows[3] = { 4, 17, 27 };
int    xIO = 22;                 /* GPIO 22: */
int    cols[18] = { 21,20,16,12,7,8,25,24,23,18,10,9,11,5,6,13,19,26 };

long intervl = 50000;

/* Panel wiring, handed to pidp_panel_open() by the mux thread. The numbers
 * are the legacy BCM GPIOs the ribbon is wired to; the backend translates
 * them (line offsets on gpiod, controller registers on gpiomem).
 *
 * PIDP10_GPIO_CHIP is passed straight through: the gpiod backend reads it as a
 * gpiochip name or /dev path, the gpiomem one as a pinctrl compatible/name.
 * An unset/empty value means "work it out", which is what lets one binary
 * serve a cabinet and a machine with no panel at all.
 */
static pidp_panel_config
panel_request(void)
{
    pidp_panel_config c = { NULL, NULL, NULL, 0, 0, 0, NULL, 0 };
    const char *chip = getenv("PIDP10_GPIO_CHIP");

    c.rows     = xrows;
    c.cols     = cols;
    c.latch    = xIO;
    c.hold_ns  = intervl;              /* row stays lit                */
    c.gap_ns   = intervl / 10;         /* dead time before the next row */
    c.chip     = (chip && *chip) ? chip : NULL;
    c.backend  = NULL;                 /* env PIDP10_PANEL decides      */
    return c;
}

struct {
      int   last_state;      /* last state */
      int   state;           /* Stable state */
      int   debounce;        /* Debounce timer */
      int   changed;         /* Switch changed state */
} switch_state[16];



void *blink(void *ptr); /* the real-time GPIO multiplexing process to start up */
pthread_t blink_thread;
int blink_thread_terminate = 0;

t_stat gpio_mux_thread_start()
{
    int res;
    res = pthread_create(&blink_thread, NULL, blink, &blink_thread_terminate);
    if (res) {
        return sim_messagef(SCPE_IERR, 
                     "Error creating gpio_mux thread, return code %d\n", res);
    }
    sim_messagef(SCPE_OK, "Created blink_thread\n");
    sleep(2); /* allow 2 sec for multiplex to start */
    return SCPE_OK;
}


/*
 * Debounce a momentary switch.
 */
static void debounce_sw(int state, int sw)
{
    if (switch_state[sw].state == state) {
        if (switch_state[sw].debounce != 0) {
            switch_state[sw].debounce--;
        } else {
            if (switch_state[sw].last_state != switch_state[sw].state) {
                 switch_state[sw].changed = 1;
            }
            switch_state[sw].last_state = switch_state[sw].state;
        }
    } else {
        switch_state[sw].debounce = 8;
        switch_state[sw].changed = 0;
        switch_state[sw].state = state;
    }
}

static t_addr
read_sw()
{
    int        col, row;
    t_uint64   sw;
    t_addr     new_as;

    /* Columns become inputs with a pull-up; the matrix is read row by row
     * below. On libgpiod this is the expensive part (a direction
     * reconfiguration), hence the caller rate-limits it. */
    pidp_panel_inputs(1);

    new_as = 0;
    for (row=0; row<5; row++) { 
        /* Select row address, settle, sample the 18 column lines.
         * Bit i set == column i high == that switch NOT pressed. */
        sw = pidp_panel_row_read(row);
        /* Map row to values */
        switch (row) {
        default:
        case 0:
                SW = (SW & SR_MASK1) | ((~sw << SR_V_0) & SR_MASK0);
                break;

        case 1:
                SW = (SW & SR_MASK0) | ((~sw << SR_V_1) & SR_MASK1);
                break;

        case 2:
                new_as = (t_addr)(~sw << MA_SW_V_3) & MA_SW_MASK3;
                break;

        case 3: /* Momentary switches */
                for (col = 0; col < 10; col++) {
                    int state = (sw & (1 << col)) == 0;
                    debounce_sw(state, col);
                }
                break;

        case 4:
#if KA | KI
                adr_cond = 0;
                adr_cond |= ((sw & INST_FETCH) == 0) ? ADR_IFETCH : 0;
                adr_cond |= ((sw & DATA_FETCH) == 0) ? ADR_DFETCH : 0;
                adr_cond |= ((sw & WRITE_SW) == 0) ? ADR_WRITE : 0;
                adr_cond |= ((sw & ADR_STOP_SW) == 0) ? ADR_STOP : 0;
                adr_cond |= ((sw & ADR_BRK_SW) == 0) ? ADR_BREAK : 0;
                nxm_stop = (sw & NXM_STOP) == 0;
#endif         
                sing_inst_sw = ((sw & SING_INST) == 0) ||
                               ((sw & SING_CYCL) == 0);
                /* PAR_STOP handle special features */
                par_stop = (sw & PAR_STOP) == 0;
                /* SING_CYCL no function yet */
                repeat_sw = (sw & REP_SW) == 0;
                break;
        }
    }
    return new_as;
}

void *blink(void *ptr)
{
    int        *terminate = (int *)ptr;
    int        col, row, ret;
    uint32     leds;
    t_addr     new_as;
    struct sched_param sp;

    sp.sched_priority = 98; // maybe 99, 32, 31?
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp))
        fprintf(stderr, "warning: failed to set RT priority\n");

    /* One call replaces the old gpiolib_init()/gpiolib_mmap()/fsel dance and
     * cannot abort the simulator: with no panel hardware the selector hands us
     * the simulator backend and the engine runs headless. */
    pidp_panel_config req = panel_request();

    ret = pidp_panel_open(&req);
    if (ret) {
        sim_messagef(SCPE_IERR, "Front panel unavailable (%s), running "
                     "without blinkenlights\n", strerror(-ret));
        return (void *)-1;
    }

    /* Read initial value of switches */
    new_as = read_sw();
    sim_messagef(SCPE_OK, "PiDP-10 FP on (%s), console line editing: %s\n",
                 pidp_panel_name(), PANEL_LINE_EDITING);


    /* start the actual multiplexing */

    while (*terminate == 0)
    {
        /* Columns drive the LEDs for the whole frame; the latch stays
         * released while the data changes. Cheap no-op if the columns are
         * already outputs. */
        pidp_panel_inputs(0);

        /* Display each row */
        for (row=0; row<7; row++) { /* 7 rows of LEDS get lit */
            switch (row) {
            default:
            case 0:
                    leds = (MI & MB_MASK0) >> MB_V_0;
                    break;
            case 1:
                    leds = (MI & MB_MASK1) >> MB_V_1;
                    break;
            case 2:
                    if (par_stop) {
                        leds = rdrin_dev & 0777;
                        leds |= rep_rate << 12;
                        leds |= MI_disable << 10;
                    } else {
                        leds = (AB & AB_MASK2) >> AB_V_2;
                    }
                    break;

            case 3:
                    leds = (IR & IR_MASK3) << IR_V_3;
                    leds |= (AC & AC_MASK3) << AC_V_3;
                    leds |= (IND) ? IND_LAMP : 0;
                    leds |= (IX & IX_MASK3) << IX_V_3;
                    break;

            case 4:
                    leds = (PC & PC_MASK4) >> PC_V_4;
                    break;

            case 5:
                    leds = PWR_LAMP;
                    leds |= (IOB_PI & PI_IOB_MASK5) << PI_IOB_V_5;
                    leds |= (PIE & PI_ENB_MASK5) << PI_ENB_V_5;
                    leds |= (FLAGS & USER) ? USER_LAMP : 0;
                    leds |= (prog_stop) ? PROG_STOP_LAMP: 0;
                    leds |= (watch_stop) ? MEM_STOP_LAMP: 0;
                    break;

            case 6:
                    leds = (RUN) ? RUN_LAMP : 0;
                    leds |= (pi_enable) ? PION_LAMP : 0;
                    leds |= (PIR & PI_REQ_MASK6) << PI_REQ_V_6;
                    leds |= (PIH & PI_PRO_MASK6) << PI_PRO_V_6;
                    leds |= (MI_flag) ? PI_LAMP : MI_LAMP;
                    break;
            }

            /* Row select, column data, latch strobe, hold and blanking.
             * One call, because the backends batch what they can: the old
             * code cost ~30 register writes per row here and a libgpiod
             * version must cost a couple of ioctls instead. */
            pidp_panel_row_write(row, leds);
       }

       /* Read in switches.
        *
        * The button scan needs the columns reconfigured as inputs, which on
        * the character device is far more expensive than lighting the LEDs.
        * pidp_panel_scan_due() is always true unless PIDP10_SWITCH_HZ asks
        * for a cap, so this is upstream behaviour by default. */
       if (pidp_panel_scan_due())
           new_as = read_sw();

       /* If running, check for switch changes */
       if (par_stop) {
           /* Process all momentary switches */
           for (col = 0; col < 10; col++) {
               if (switch_state[col].changed && switch_state[col].state) {
                  switch_state[col].changed = 0;
                  switch (col) {
                  case 1:      /* Examine this */
                          rep_rate = (new_as >> 14) & 0xf;
                          break;

                  case 4:      /* Stop function */
                          stop_sw = 1;
                          pwr_off = 1;
                          break;

                  case 5:      /* Continue */
                          MI_disable = !MI_disable;
                          if (MI_disable)
                              MI_flag = 0;
                          break;

#if KA | KI
                  case 7:      /* ReadIN */
                          rdrin_dev = 0774 & new_as;
                          break;
#endif

                  case 0:      /* Examine next */
                  case 2:      /* Execute function */
                  case 3:      /* Reset function */
                  case 6:      /* Start */
                  case 8:      /* Deposit next */
                  case 9:      /* Deposit this */
                  default:
                          break;
                  }
               }
           }
       } else {
           AS = new_as;
       }

       /* Check repeat count */
       if (rep_count > 0 && --rep_count == 0) {
           for (col = 0; col < 10; col++) {
               switch_state[col].changed = switch_state[col].state;
           }
       }

       /* Process switch changes if running */
       if (RUN) {
            for (col = 0; col < 10; col++) {
                if (switch_state[col].changed && switch_state[col].state) {
                   /* If repeat switch set, trigger timer */
                   if (repeat_sw) {
                       rep_count = (rep_rate + 1) * 16;
                   }
                   switch (col) {
                   case 1:    /* Examine this */
                              examine_sw = 1;
                              MI_flag = 0;
                              switch_state[col].changed = 0;
                              break;

                   case 0:    /* Examine next */
                   case 5:    /* Continue */
                   case 6:    /* Start */
                   case 7:    /* ReadIN */
                   case 8:    /* Deposit next */
                   default:
                              switch_state[col].changed = 0;
                              break;

                   case 2:    /* Execute function */
                              xct_sw = 1;
                              switch_state[col].changed = 0;
                              break;

                   case 3:    /* Reset function */
                              stop_sw = 1;
                              break;

                   case 4:    /* Stop function */
                              stop_sw = 1;
                              switch_state[col].changed = 0;
                              break;

                   case 9:    /* Deposit this */
                              deposit_sw = 1;
                              MI_flag = 0;
                              switch_state[col].changed = 0;
                              break;
                   }
                }
            }
       }

       /* done with reading the switches, 
        * so start the next cycle of lighting up LEDs
        */
    }

    /* received terminate signal, close down: latch released, columns
     * tri-stated, rows deselected - the state that leaves the LEDs dark and
     * the line drivers unstressed. */
    pidp_panel_park();

    return NULL;
}

volatile int    input_wait;
static char  *input_buffer = NULL;

/*
 * Handler for EditLine package when line is complete.
 */
static void
read_line_handler(char *line)
{
    if (line != NULL) {
       input_buffer = line;
       input_wait = 0;
#if PANEL_USE_CALLBACK_EDIT
       add_history(line);
#endif
    }
}

#if !PANEL_USE_CALLBACK_EDIT
/*
 * Stand-in for rl_callback_read_char() on a machine that has neither libedit
 * nor GNU readline: echo what arrives and hand the completed line to the same
 * handler. It gives line-at-a-time SCP input at the console and nothing else
 * (no editing, no history), which is enough to keep the panel build usable.
 */
static char   plain_buf[256];
static size_t plain_len;

static void
plain_read_char(int fd)
{
    char c;

    while (read(fd, &c, 1) == 1) {
        if ((c == '\r') || (c == '\n')) {
            char *line = (char *) malloc(plain_len + 1);

            if (line == NULL)
                return;
            memcpy(line, plain_buf, plain_len);
            line[plain_len] = '\0';
            plain_len = 0;
            printf("\r\n");
            fflush(stdout);
            read_line_handler(line);        /* takes ownership of line */
            return;
        }
        if ((c == '\b') || (c == 0177)) {
            if (plain_len > 0) {
                plain_len--;
                printf("\b \b");
            }
            continue;
        }
        if (plain_len < sizeof (plain_buf) - 1) {
            plain_buf[plain_len++] = c;
            (void) fputc(c, stdout);
            fflush(stdout);
        }
    }
}
#endif /* !PANEL_USE_CALLBACK_EDIT */

/*
 * Process input from stdin or switches.
 */
static char *
vm_read(char *cptr, int32 sz, FILE *file)
{
    struct timeval tv = {0,10000};  /* Wait for 10ms */
    fd_set         read_set;
    int            fd = fileno(file);  /* What to wait on */
    int            col;

    if (input_buffer != NULL)
        free(input_buffer);
#if PANEL_USE_CALLBACK_EDIT
    rl_callback_handler_install(sim_prompt, (rl_vcpfunc_t*) &read_line_handler);
#endif
    input_wait = 1;
    input_buffer = NULL;
    while (input_wait) {
       FD_ZERO(&read_set);
       FD_SET(fd, &read_set);
       tv.tv_sec = 0;
       tv.tv_usec = 10000;
       (void)select(fd+1, &read_set, NULL, NULL, &tv);
       if (FD_ISSET(fd, &read_set)) {
#if PANEL_USE_CALLBACK_EDIT
           rl_callback_read_char();
#else
           plain_read_char(fd);
#endif
       } else {
           if (pwr_off) {
               if ((input_buffer = (char *)malloc(20)) != 0) {
                   strcpy(input_buffer, "quit\r");
                   stop_sw = 1;
                   pwr_off = 0;
                   input_wait = 0;
               }
               break;
           }

           /* Process switches */
           for (col = 0; col < 10; col++) {
                if (switch_state[col].changed && switch_state[col].state) {
                    /* If repeat switch set, trigger timer */
                    if (repeat_sw) {
                        rep_count = (rep_rate + 1) * 16;
                    }
                    switch (col) {
                    case 0:      /* Examine next */
                            AB++;
                            MB = (AB < 020) ? FM[AB] : M[AB];
                            MI_flag = 0;
                            break;

                    case 1:      /* Examine this */
                            AB = AS;
                            MB = (AS < 020) ? FM[AS] : M[AS];
                            MI_flag = 0;
                            break;

                    case 2:      /* Execute function */
                            if ((input_buffer = (char *)malloc(20)) != 0) {
                                strcpy(input_buffer, "step\r");
                                xct_sw = 1;
                                input_wait = 0;
                            }
                            break;

                    case 3:      /* Reset function */
                            if ((input_buffer = (char *)malloc(20)) != 0) {
                                strcpy(input_buffer, "reset all\r");
                                input_wait = 0;
                            }
                            break;

                    case 4:      /* Stop function */
                            break;

                    case 5:      /* Continue */
                            if ((input_buffer = (char *)malloc(10)) != 0) {
                               strcpy(input_buffer,
                                        (sing_inst_sw) ? "step\r" : "cont\r");
                               input_wait = 0;
                            }
                            break;

                    case 6:      /* Start */
                            if ((input_buffer = (char *)malloc(20)) != 0) {
                                sprintf(input_buffer, "run %06o\r", AS);
                                input_wait = 0;
                            }
                            break;

#if KA | KI
                    case 7:      /* ReadIN */
                            if ((input_buffer = (char *)malloc(20)) != 0) {
                                DEVICE         *dptr;
                                int            i;

                                /* Scan all devices to find match */
                                for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
                                    DIB *dibp = (DIB *) dptr->ctxt;
                                    if (dibp && !(dptr->flags & DEV_DIS) &&
                                        (dibp->dev_num == (rdrin_dev & 0774))) {
                                        if (dptr->numunits > 1)
                                            sprintf(input_buffer, "boot %s0\r",
                                                      dptr->name);
                                        else
                                            sprintf(input_buffer, "boot %s\r",
                                                      dptr->name);
                                        input_wait = 0;
                                        break;
                                    }
                                }
                            }
                            /* If we did not find a boot device, free command */
                            if (input_wait) {
                                free(input_buffer);
                                input_buffer = NULL;
                                sim_messagef(SCPE_OK, "Device %03o not found\n",
                                                rdrin_dev);
                            }
                            break;
#endif

                    case 8:      /* Deposit next */
                           AB++;
                           if (AB < 020) {
                               FM[AB] = SW;
                               MB = FM[AB];
                           } else {
                               M[AB] = SW;
                               MB = M[AB];
                           }
                           MI_flag = 0;
                           break;

                    case 9:      /* Deposit this */
                           AB = AS;
                           if (AS < 020) {
                               FM[AS] = SW;
                               MB = FM[AS];
                           } else {
                               M[AS] = SW;
                               MB = M[AS];
                           }
                           MI_flag = 0;
                           break;
                    }
                    switch_state[col].changed = 0;
                }
            }
       }
    }
#if PANEL_USE_CALLBACK_EDIT
    rl_callback_handler_remove();
#endif
    return input_buffer;
}

static void
vm_post(t_bool from_scp)
{
}

/*
 * Start panel thread, and install console read functions.
 */
t_stat pi_panel_start(void)
{
    t_stat    r;

    /* start up multiplexing thread */
    r = gpio_mux_thread_start();
    sim_vm_read = &vm_read;
    sim_vm_post = &vm_post;
    return r;
}

/*
 * Stop display thread.
 */
void pi_panel_stop(void)
{
    if (blink_thread_terminate == 0) {
        blink_thread_terminate=1;
        sim_vm_read = NULL;

        sleep (2);      /* allow threads to close down */
        /* Done only after the mux thread has stopped touching the lines,
         * otherwise it would light rows again behind our back. */
        pidp_panel_close();
    }
}

#endif
