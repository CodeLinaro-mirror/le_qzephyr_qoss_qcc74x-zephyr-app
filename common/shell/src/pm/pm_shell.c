/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/shell/shell.h>

#if defined(CONFIG_QCC74X_PM_SHELL)

SHELL_SUBCMD_SET_CREATE(pm_commands, (qcom, pm));
SHELL_SUBCMD_ADD((qcom), pm, &pm_commands, "power-management commands", NULL, 1, 0);

#endif /* CONFIG_QCC74X_PM_SHELL */
