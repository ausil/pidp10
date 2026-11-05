
# pinctrl

pinctrl is a more powerful replacement for raspi-gpio, a tool for displaying
and modifying the GPIO and pin muxing state of a system. It accesses the
hardware directly, bypassing the kernel drivers, and as such requires root
privilege (run with "sudo").

The improvements over raspi-gpio include:

* GPIO controllers are detected at runtime from Device Tree.
* GPIOs can be referred to by name or number.
* Pin mode (-p) switches the UI to be in terms of 40-way header pin numbers.
* The "poll" command causes it to constantly monitor the specified pins,
  displaying any level changes it sees. For slow signals (up to a few hundred
  kHz) it can act as a basic logic analyser.
* The "get" and "set" keywords are optional in most cases.
* Splitting into a general gpiolib library and a separate client application
  allows new applications to be added easily.

**Build Instructions**

Install the prerequisites with "sudo apt install cmake" - you need at least version 3.10 of cmake. Run the following commands, either here or in the top-level directory to build and install everything:

 - *cmake .*
 - *make*
 - *sudo make install*

**Usage**

* `sudo pinctrl`              (Display the state of all recognised GPIOs)
* `sudo pinctrl -p`           (Show the state of the 40-way header pins)
* `sudo pinctrl 4,6 op dl`    (Make GPIOs 4 and 6 outputs, driving low)
* `sudo pinctrl poll BT_CTS,BT_RTS`    (Monitor the levels of the Bluetooth flow control signals)
* `pinctrl funcs 9-11`        (List the available alternate functions on GPIOs 9, 10 and 11)
* `pinctrl help`              (Show the full usage guide)

## The front panel (`pidp_panel`)

`ka10_pipanel.c` used to drive the PiDP-10 blinkenlights by calling
`gpiolib_*` here, i.e. by `mmap()`ing the GPIO controller registers out of
`/dev/gpiomem`. That works only on Raspberry Pi OS: a distribution kernel
builds no gpiomem driver, `CONFIG_STRICT_DEVMEM` refuses to map device memory
into userspace at all, and on a Pi 5 the panel lines are on the RP1 I/O chip,
whose registers are not even reachable through the SoC-side device tree this
code parses.

So panel access now goes through one API, `pidp_panel.h`, with a backend per
way of reaching the hardware:

| backend   | how it drives the panel                        | works on                |
|-----------|------------------------------------------------|-------------------------|
| `gpiod`   | `/dev/gpiochipN` via libgpiod                    | Fedora on Pi 4/5, anything with a Pi controller |
| `gpiomem` | the register mapping above (`gpiolib`)           | Raspberry Pi OS         |
| `sim`     | nothing; the LED image goes to the log           | any machine, dev laptops |

The choice is made at run time by `PIDP10_PANEL` (`auto` by default: probe for
hardware, fall back to `sim`), so one binary serves a cabinet and a machine
with no cabinet at all. Nothing outside these files may call `gpiolib_*` or
`gpiod_*` for the panel.

Build it as part of the simulator, not here:

    cd src/pdp10 && make PIDP10=1 pdp10-ka

`make` links libgpiod only if `gpiod.h` and the library are present, and tells
you which front-panel backends and which line-editing library it found.

### pidp-gpio-probe

Answers "is there a front panel on this machine?" for the shell scripts, so
they do not guess. Build it with cmake (`cmake . && make` builds `pinctrl`
plus `pidp-gpio-probe` when libgpiod >= 2.0 is installed).

* `pidp-gpio-probe --check`    exit 0 iff the panel would use real hardware
* `pidp-gpio-probe --backend`  print the backend that would be picked
* `pidp-gpio-probe --list`     every gpiochip, its label, and whether the 22
                               panel lines are free
* `pidp-gpio-probe --pins`     the panel pin table
* `pidp-gpio-probe --selftest` drive the multiplexing loop on the simulated
                               panel and report the real frames/s

`--selftest` is the only way to check the panel code off a cabinet, and it
reports honest numbers: the theoretical ~2.3k frames/s needs exact
`nanosleep()`, i.e. high-resolution timers and the `SCHED_FIFO` priority the
mux thread asks for (root). Unprivileged, expect a few hundred.

**Why auto-detection refuses chips.** `/dev/gpiochipN` exists on far more
machines than PiDP-10 cabinets, and a line 22 on one of them is somebody
else's hardware - a Qualcomm laptop has eleven chips, most of them PMIC power
rails. A chip is therefore only claimed when its name or label identifies a
Broadcom controller (`brcm`, `bcm2835`, `rp1`, ...) and all 22 panel lines are
free. Pointing the panel at anything else takes *two* settings,
`PIDP10_GPIO_CHIP` plus `PIDP10_PANEL_FORCE=1`, because a typo in a config
file must not be enough to drive a power rail. `--check` and `--list` only
open and inspect chips; they never request a line, so they are safe to run
while an engine owns the panel.
