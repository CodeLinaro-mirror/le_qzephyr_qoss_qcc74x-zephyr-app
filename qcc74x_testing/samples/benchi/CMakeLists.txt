# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(benchi)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    zephyr_compile_options(
        -Wno-implicit-function-declaration
        -Wno-stringop-truncation
    )
endif()

############################################
# extra modules
############################################

set(ZEPHYR_EXTRA_MODULES $ENV{ZEPHYR_BASE}/../iot_aleph/modules/googletest)


############################################
# cmake helper functions
############################################

macro(zephyr_object_library_named name)
    # This is a macro because we need add_library() to be executed
    # within the scope of the caller.
    set(ZEPHYR_CURRENT_LIBRARY ${name})
    add_library(${name} OBJECT "")
    zephyr_append_cmake_library(${name})
    target_link_libraries(${name} PUBLIC zephyr_interface)
endmacro()


############################################
# general include folder
############################################

zephyr_interface_library_named(${PROJECT_NAME}.include)
target_include_directories(${PROJECT_NAME}.include INTERFACE
        include
)
target_link_libraries(zephyr_interface INTERFACE ${PROJECT_NAME}.include)

############################################
# jsmn - minimal json
############################################

zephyr_interface_library_named(${PROJECT_NAME}.jsmn)
target_include_directories(${PROJECT_NAME}.jsmn INTERFACE
        include/jsmn
)

############################################
# threads
############################################
zephyr_library_named(${PROJECT_NAME}.threads)
target_sources(${PROJECT_NAME}.threads PRIVATE
        src/threads/bshg_threads.c
        src/threads/thread.app.c
        src/threads/thread.domain.c
        src/threads/thread.ll.c
)
target_include_directories(${PROJECT_NAME}.threads PUBLIC
        src/threads/
)
target_link_libraries(${PROJECT_NAME}.threads PUBLIC
    ${PROJECT_NAME}.messages
    ${PROJECT_NAME}.zbus
    ${PROJECT_NAME}.spi
    ${PROJECT_NAME}.jsmn
    ${PROJECT_NAME}.stress
    ${PROJECT_NAME}.tracing
)

############################################
# download
############################################
zephyr_library_named(${PROJECT_NAME}.download)
target_sources(${PROJECT_NAME}.download PRIVATE
        src/download/bshg_download.c
)
target_include_directories(${PROJECT_NAME}.download PUBLIC
        src/download
)
target_link_libraries(${PROJECT_NAME}.download PUBLIC
        ${PROJECT_NAME}.jsmn
        ${PROJECT_NAME}.flash
)

############################################
# zbus
############################################
zephyr_library_named(${PROJECT_NAME}.zbus.helper)
target_sources(${PROJECT_NAME}.zbus.helper PRIVATE
        src/zbus/helper/bshg_zbus_helper.c
)
target_include_directories(${PROJECT_NAME}.zbus.helper PUBLIC
        src/zbus/helper/
)

zephyr_library_named(${PROJECT_NAME}.zbus.trigger)
target_sources(${PROJECT_NAME}.zbus.trigger PRIVATE
        src/zbus/bshg_zbus_messages.c
)
target_include_directories(${PROJECT_NAME}.zbus.trigger PUBLIC
        src/zbus/trigger/
)
target_link_libraries(${PROJECT_NAME}.zbus.trigger PUBLIC
        ${PROJECT_NAME}.zbus.helper
        ${PROJECT_NAME}.tracing
)

add_library(${PROJECT_NAME}.zbus INTERFACE)
target_link_libraries(${PROJECT_NAME}.zbus INTERFACE
        ${PROJECT_NAME}.zbus.helper
        ${PROJECT_NAME}.zbus.trigger
)

############################################
# webserver
############################################
zephyr_library_named(${PROJECT_NAME}.webserver)
foreach(web_resource     index.html main.js)
    generate_inc_file_for_target(${PROJECT_NAME}.webserver
            src/webserver/static_web_resources/${web_resource}
            ${CMAKE_CURRENT_BINARY_DIR}/include/generated/${web_resource}.gz.inc
            --gzip
    )
endforeach()

foreach(inc_file server_cert.der server_privkey.der)
    generate_inc_file_for_target(
            ${PROJECT_NAME}.webserver
            src/webserver/certs/${inc_file}
            ${CMAKE_CURRENT_BINARY_DIR}/include/generated/${inc_file}.inc
    )
endforeach()

target_sources(${PROJECT_NAME}.webserver PRIVATE
    src/webserver/webserver.c
    src/webserver/ws.c
)
target_include_directories(${PROJECT_NAME}.webserver PUBLIC
    certs
    ${CMAKE_CURRENT_BINARY_DIR}/include/generated/
)
target_link_libraries(${PROJECT_NAME}.webserver PRIVATE
    zephyr_interface
    zephyr
    ${PROJECT_NAME}.zbus.helper
    ${PROJECT_NAME}.tracing
)

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(
    NAME http_resource_desc_test_https_service
    KVMA RAM_REGION GROUP RODATA_REGION
    SUBALIGN ${CONFIG_LINKER_ITERABLE_SUBALIGN})
zephyr_linker_section(
    NAME http_resource_desc_test_http_service
    KVMA RAM_REGION GROUP RODATA_REGION
    SUBALIGN ${CONFIG_LINKER_ITERABLE_SUBALIGN})

############################################
# messages
############################################
zephyr_library_named(${PROJECT_NAME}.messages)
target_sources(${PROJECT_NAME}.messages PRIVATE
        src/messages/ll/download.c
        src/messages/ll/file.c
        src/messages/ll/led.c
        src/messages/ll/spi.c
        src/messages/ll/tracing.c
        src/messages/calculate_hashsum.c
        src/messages/ping.c
        src/messages/wait.c
        src/messages/zbus.c
)
target_link_libraries(${PROJECT_NAME}.messages PUBLIC
        ${PROJECT_NAME}.jsmn
        ${PROJECT_NAME}.zbus
        ${PROJECT_NAME}.spi
        ${PROJECT_NAME}.flash
        ${PROJECT_NAME}.tracing
        ${PROJECT_NAME}.download
        mbedTLS
)

############################################
# messages
############################################
zephyr_library_named(${PROJECT_NAME}.flash)
target_sources(${PROJECT_NAME}.flash PRIVATE
        src/flash/filesystem.c
        src/flash/rawflash.c
)
target_link_libraries(${PROJECT_NAME}.flash PUBLIC
        ${PROJECT_NAME}.jsmn
        ${PROJECT_NAME}.tracing
        ${PROJECT_NAME}.download
        LITTLEFS
)

############################################
# spi
############################################
zephyr_library_named(${PROJECT_NAME}.spi)
target_sources(${PROJECT_NAME}.spi PRIVATE
        src/spi/bshg_spi.c
)
target_include_directories(${PROJECT_NAME}.spi PUBLIC
        src/spi/
)
zephyr_library_add_dependencies(offsets_h)
############################################
# stress
############################################
zephyr_library_named(${PROJECT_NAME}.stress)
target_sources(${PROJECT_NAME}.stress PRIVATE
        src/stress/stress.c
)
target_include_directories(${PROJECT_NAME}.stress PUBLIC
        src/stress/
)
target_link_libraries(${PROJECT_NAME}.stress PUBLIC
        ${PROJECT_NAME}.tracing
)

############################################
# tracing
############################################
zephyr_library_named(${PROJECT_NAME}.tracing)
target_sources(${PROJECT_NAME}.tracing PRIVATE
        src/tracing/BSHG_tracing.c
)
target_include_directories(${PROJECT_NAME}.tracing PUBLIC
        src/tracing/
)

############################################
# test
############################################
if(CONFIG_ZTEST)
    zephyr_object_library_named(${PROJECT_NAME}.test)

    file(GLOB_RECURSE test_sources test/*.cpp)
    target_sources(${PROJECT_NAME}.test PRIVATE
        test/threads/thread_app.json.c
        test/threads/thread_domain.json.c
        test/threads/thread_ll.json.c
    )
    target_link_libraries(${PROJECT_NAME}.test PRIVATE
            ${PROJECT_NAME}.spi
            ${PROJECT_NAME}.threads
            ${PROJECT_NAME}.zbus
            ${PROJECT_NAME}.jsmn
    )
    target_link_libraries(app PRIVATE
            ${PROJECT_NAME}.test
    )
endif()
############################################
# app (main)
############################################
target_sources(app PRIVATE
    src/main.c
    src/shell/shell.c # TODO: why does it only work when directly added to app?
)

target_link_libraries(app PRIVATE
        ${PROJECT_NAME}.threads
        ${PROJECT_NAME}.webserver
        ${PROJECT_NAME}.zbus
        ${PROJECT_NAME}.stress
        ${PROJECT_NAME}.tracing
)

foreach(_lib
        ${PROJECT_NAME}.threads
        ${PROJECT_NAME}.download
        ${PROJECT_NAME}.zbus.helper
        ${PROJECT_NAME}.zbus.trigger
        ${PROJECT_NAME}.webserver
        ${PROJECT_NAME}.messages
        ${PROJECT_NAME}.flash
        ${PROJECT_NAME}.spi
        ${PROJECT_NAME}.stress
        ${PROJECT_NAME}.tracing)
    if(TARGET ${_lib})
        add_dependencies(${_lib} heap_constants offsets_h)
    endif()
endforeach()
