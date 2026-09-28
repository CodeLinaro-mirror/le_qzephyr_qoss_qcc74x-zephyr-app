# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

menu "QCC74x Low Power Shell"

depends on SOC_SERIES_QCC74X && SHELL

config QCC74X_PM_SHELL
	bool
	default y if QCC74X_PM_PDS_VALIDATION || QCC74X_HBN_SHELL

config QCC74X_PM_PDS_VALIDATION
	bool "Enable QCC74x low-power PDS15 validation commands"
	depends on PM && QCC74X_PM_LPFW && QCC74X_PM_PDS15_POLICY
	depends on QCC74X_PM_DIAGNOSTICS
	default n
	help
	  Enable the qcom pm pds shell command group used to observe QCC74x
	  PDS15 low-power bring-up, run LPFW initialization, and inspect the
	  wake path from an application using the QCOM shell.

config QCC74X_PM_PDS_DEFAULT_SLEEP_MS
	int "Default low-power validation sleep duration in milliseconds"
	depends on QCC74X_PM_PDS_VALIDATION
	range 1 60000
	default 200

endmenu
