cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wdt_l2)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

zephyr_linker_sources(SECTIONS linker/wdt_noinit.ld)
zephyr_linker_sources(ROM_SECTIONS linker/heap_limit.ld)
