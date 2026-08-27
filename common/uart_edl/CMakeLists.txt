# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library()
zephyr_library_sources(src/uart_edl_trigger.c)

# Prevent match_pos from landing in .sbss (GP-relative small-data section).
# On RISC-V the GP window is only ±2 KB; even one byte in .sbss can push
# other objects out of range and cause
# "relocation truncated to fit: R_RISCV_GPREL_I" link errors.
#
# zephyr_library_compile_options() calls target_compile_options(PRIVATE),
# but the zephyr_interface INTERFACE_COMPILE_OPTIONS (which include the
# board-default -msmall-data-limit=8) are appended AFTER the library's own
# options, so they override it.  set_source_files_properties() is applied
# last in the compiler command line and therefore wins.
set_source_files_properties(
    src/uart_edl_trigger.c
    PROPERTIES
    COMPILE_OPTIONS "-msmall-data-limit=0"
)