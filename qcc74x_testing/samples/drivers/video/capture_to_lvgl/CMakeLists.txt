# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
if("${BOARD}" STREQUAL "qcc748m_evk" OR "${BOARD}" STREQUAL "qcc748m_evk/qcc748")
  set(APP_SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker/qcc748_psram.ld)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(video_capture)

# Override upstream ov2640.c with our local fixed version (corrected reset timing).
# --allow-multiple-definition: when both libapp.a and libdrivers__video.a define the
# same symbols, the linker uses the first occurrence. Since libapp.a is linked before
# libdrivers__video.a, our local ov2640.c symbols take priority.
# CONFIG_VIDEO_OV2640 remains =y so DTS ordinals are computed correctly.
target_link_options(app PRIVATE -Wl,--allow-multiple-definition)
zephyr_ld_options(-Wl,--allow-multiple-definition)

target_sources(app PRIVATE
    src/main.c
    src/ov2640.c
)

# Allow local ov2640.c to include internal zephyr video driver headers
zephyr_include_directories(${ZEPHYR_BASE}/drivers/video)
