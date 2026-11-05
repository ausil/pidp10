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
# Panel tools: pidp-gpio-probe (is a cabinet attached?), scansw10 (read the
# maintenance switch register; the engine unit uses it to pick which system to
# boot) and pidp10-test (bring-up test for a newly cabled cabinet).
#
# All three reach the hardware through pidp_panel, so the answer they give is
# produced by the same code that would have to drive the panel - which is also
# why the engine unit asks pidp-gpio-probe instead of running gpioinfo. Each
# works on Fedora (gpiod), on Raspberry Pi OS (register mapping) and with no
# cabinet at all (sim).
#
# Unlike the standalone `pinctrl` CLI these link the gpiomem backend as well:
# it is never mapped unless the operator asks for it by name, and a tool that
# could not name it could not test a Raspberry Pi OS cabinet either.
# ---------------------------------------------------------------------------
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBGPIOD libgpiod>=2.0)
endif()

if(LIBGPIOD_FOUND)
    set(PANEL_GPIOD_SRC pidp_panel_gpiod.c)
    set(PANEL_HAVE_GPIOD 1)
else()
    # The selector's #if keeps every libgpiod reference out of the build, so
    # the tools still build here; they drive gpiomem, or say there is no panel.
    set(PANEL_GPIOD_SRC "")
    set(PANEL_HAVE_GPIOD 0)
    message(STATUS "libgpiod >= 2.0 not found: panel tools are built without "
                   "the character-device backend (PIDP10_PANEL=gpiod will fail)")
endif()

set(PANEL_SRC
    pidp_panel.c pidp_panel_sim.c pidp_panel_gpiomem.c ${PANEL_GPIOD_SRC}
    gpiolib.c util.c gpiochip_bcm2835.c gpiochip_bcm2712.c gpiochip_rp1.c)

function(pidp_panel_tool name)
    add_executable(${name} ${ARGN} ${PANEL_SRC})
    target_compile_definitions(${name} PRIVATE
        PIDP10_HAVE_GPIOD=${PANEL_HAVE_GPIOD} PIDP10_HAVE_GPIOMEM=1)
    target_include_directories(${name} PRIVATE ${LIBGPIOD_INCLUDE_DIRS})
    target_link_libraries(${name} pthread ${LIBGPIOD_LIBRARIES})
    set_target_properties(${name} PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
    target_compile_options(${name} PRIVATE -fno-lto)
    target_link_options(${name} PRIVATE -fno-lto)
    install(TARGETS ${name} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endfunction()

pidp_panel_tool(pidp-gpio-probe pidp_gpio_probe.c)
pidp_panel_tool(scansw10 scansw10.c)
pidp_panel_tool(pidp10-test pidp10_test.c)

