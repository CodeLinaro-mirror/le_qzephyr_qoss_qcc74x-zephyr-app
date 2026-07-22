# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/usr/bin/env bash
# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0
#
# Source this AFTER activating the workspace .venv, from anywhere:
#
#   source qapp/qcc74x/qcc74x_testing/regression/env.sh
#
# It prepends this directory's pyshim/ folder to PYTHONPATH, which makes
# a bare `west twister ...` (and `python3 zephyr/scripts/twister ...`)
# transparently work around two upstream Zephyr v4.4.0 (Gerrit mirror)
# fork issues -- see pyshim/sitecustomize.py for the full explanation.
#
# No file under zephyr/ or qcc74x_zephyr/ is modified by this. The
# workaround is scoped to shells that source this file -- unrelated
# Python processes are unaffected.

_QCC74X_TESTING_ENV_SH="${BASH_SOURCE[0]:-$0}"
_QCC74X_TESTING_REGRESSION_DIR="$(cd "$(dirname "${_QCC74X_TESTING_ENV_SH}")" && pwd)"
_QCC74X_TESTING_PYSHIM_DIR="${_QCC74X_TESTING_REGRESSION_DIR}/pyshim"

case ":${PYTHONPATH:-}:" in
    *":${_QCC74X_TESTING_PYSHIM_DIR}:"*)
        ;;
    *)
        export PYTHONPATH="${_QCC74X_TESTING_PYSHIM_DIR}${PYTHONPATH:+:${PYTHONPATH}}"
        ;;
esac

echo "qcc74x_testing: west twister workaround active (PYTHONPATH includes ${_QCC74X_TESTING_PYSHIM_DIR})"

unset _QCC74X_TESTING_ENV_SH _QCC74X_TESTING_REGRESSION_DIR _QCC74X_TESTING_PYSHIM_DIR
