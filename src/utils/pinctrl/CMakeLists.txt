cmake_minimum_required(VERSION 3.10...3.27)
project(pinctrl C)
include(GNUInstallDirs)

# Warnings are compile options, not a reassignment of CMAKE_C_FLAGS. The
# distribution's packaging macros deliver their hardening flags (-O2 -fPIE
# -D_FORTIFY_SOURCE ...) through CMAKE_C_FLAGS; overwriting it produced
# non-PIE objects linked into a PIE executable, which fails to link on aarch64
# ("relocation R_AARCH64_ADR_PREL_PG_HI21 against symbol stderr") while looking
# perfectly healthy on x86-64.
add_compile_options(-Wall -Wextra -Werror -pedantic)

#add executables
add_executable(pinctrl pinctrl.c gpiolib.c util.c gpiochip_bcm2835.c gpiochip_bcm2712.c gpiochip_rp1.c)
# The gpiochip_* tables live in a custom linker section and are only reachable
# through __start_gpiochips; LTO does not know that. `used` in gpiochip.h is the
# real fix, this is the belt so a distribution's -flto cannot eat the table.
set_target_properties(pinctrl PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
target_compile_options(pinctrl PRIVATE -fno-lto)
target_link_options(pinctrl PRIVATE -fno-lto)
install(TARGETS pinctrl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES pinctrl-completion.bash RENAME pinctrl DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/bash-completion/completions")

# ---------------------------------------------------------------------------
# pidp-gpio-probe - is there a front panel on this machine?
#
# Used by the systemd engine unit (install/fedora/libexec/pidp10-engine) so
# that "do we have blinkenlights" is answered by the same code that would have
# to drive them, instead of by a `gpioinfo | grep gpio` that is true on every
# machine which has GPIO at all.
#
# It only needs the gpiod backend plus the shared selector; the register
# mapping backend is deliberately left out (probing it would mean mapping
# device memory just to ask a question).
# ---------------------------------------------------------------------------
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBGPIOD libgpiod>=2.0)
endif()

if(LIBGPIOD_FOUND)
    add_executable(pidp-gpio-probe
        pidp_gpio_probe.c pidp_panel.c pidp_panel_sim.c pidp_panel_gpiod.c)
    target_compile_definitions(pidp-gpio-probe PRIVATE
        PIDP10_HAVE_GPIOD=1 PIDP10_HAVE_GPIOMEM=0)
    target_include_directories(pidp-gpio-probe PRIVATE ${LIBGPIOD_INCLUDE_DIRS})
    target_link_libraries(pidp-gpio-probe ${LIBGPIOD_LIBRARIES})
    install(TARGETS pidp-gpio-probe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
    message(STATUS "libgpiod >= 2.0 not found: building pinctrl only "
                   "(no pidp-gpio-probe, front panel will fall back to sim)")
endif()
