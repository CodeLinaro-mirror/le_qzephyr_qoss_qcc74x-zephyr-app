# SPDX-License-Identifier: Apache-2.0
# Wrapper: builds upstream uart/async_api with qcc748m_evk support.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(uart_async_api_qcc74x)
target_sources(app PRIVATE ${ZEPHYR_BASE}/samples/drivers/uart/async_api/src/main.c)
