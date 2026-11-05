# scansw10 - front-panel diagnostic: dump the 5x18 switch matrix.
#
# It talks to the GPIO controller registers through pinctrl's gpiolib
# (/dev/gpiomem), i.e. it is a Raspberry Pi OS tool; the supported panel path
# is src/utils/pinctrl/pidp_panel.c, whose backends cover Fedora and a machine
# with no cabinet at all. Kept because this is the one that measures the
# multiplexing loop from the register side.
#
# The pinctrl sources are compiled out of ../utils/pinctrl. They used to be
# copied into this directory, and the copies had already drifted: this tree's
# gpiochip.h was missing the `used` attribute that stops -flto from deleting
# the gpiochip_* tables the linker finds through __start_gpiochips.

CC ?= gcc
CFLAGS ?= -pthread -Wall -Wextra -g
CPPFLAGS += -I..                       # so "pinctrl/gpiolib.h" finds src/utils

PINCTRL = ../utils/pinctrl
SRC = scansw10.c \
      $(PINCTRL)/gpiochip_bcm2712.c $(PINCTRL)/gpiochip_bcm2835.c \
      $(PINCTRL)/gpiochip_rp1.c $(PINCTRL)/gpiolib.c $(PINCTRL)/util.c

# Objects land here, not in ../utils/pinctrl: that directory is shared with
# the cmake build of pinctrl/pidp-gpio-probe, and .o files from two build
# systems in one source directory is how a tree grows stale objects.
OBJ = $(notdir $(SRC:.c=.o))
VPATH = $(PINCTRL)
EXECUTABLE = scansw10

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(EXECUTABLE)

.PHONY: all clean
