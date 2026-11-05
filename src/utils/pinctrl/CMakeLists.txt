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
