
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear

if(NOT CONFIG_SOC_FAMILY_QCOM_QCC7XX)
	return()
endif()

add_subdirectory_ifdef(CONFIG_SHELL shell)
