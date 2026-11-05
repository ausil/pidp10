# pidp10-test - front-panel diagnostic: drive the LED matrix from the register
# side and report the frame rate.
#
# It talks to the GPIO controller registers through pinctrl's gpiolib
# (/dev/gpiomem), i.e. it is a Raspberry Pi OS tool. On a distribution kernel
# use `pidp-gpio-probe --selftest`, which exercises the same multiplexing loop
# through pidp_panel and works on the gpiod backend too.
#
# The pinctrl sources are compiled out of ../utils/pinctrl. They used to be
# copied into this directory, and the copies had already drifted: this tree's
# gpiochip.h was missing the `used` attribute that stops -flto from deleting
# the gpiochip_* tables the linker finds through __start_gpiochips.

CC ?= gcc
CFLAGS ?= -pthread -Wall -Wextra -g
CPPFLAGS += -I..                       # so "pinctrl/gpiolib.h" finds src/utils

PINCTRL = ../utils/pinctrl
SRC = pidp10panel.c \
      $(PINCTRL)/gpiochip_bcm2712.c $(PINCTRL)/gpiochip_bcm2835.c \
      $(PINCTRL)/gpiochip_rp1.c $(PINCTRL)/gpiolib.c $(PINCTRL)/util.c

# Objects land here, not in ../utils/pinctrl: that directory is shared with
# the cmake build of pinctrl/pidp-gpio-probe, and .o files from two build
# systems in one source directory is how a tree grows stale objects.
OBJ = $(notdir $(SRC:.c=.o))
VPATH = $(PINCTRL)
EXECUTABLE = pidp10-test

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(EXECUTABLE)

.PHONY: all clean
