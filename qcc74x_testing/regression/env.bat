@echo off
REM Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
REM SPDX-License-Identifier: BSD-3-Clause
REM
REM Windows CMD equivalent of env.sh. Run this AFTER activating the
REM workspace .venv, from any CMD shell:
REM
REM   call qapp\qcc74x\qcc74x_testing\regression\env.bat
REM
REM It prepends this directory's pyshim\ folder to PYTHONPATH, which makes
REM a bare `west twister ...` (and `python zephyr\scripts\twister ...`)
REM transparently work around two upstream Zephyr v4.4.0 (Gerrit mirror)
REM fork issues -- see pyshim\sitecustomize.py for the full explanation.
REM
REM No file under zephyr\ or qcc74x_zephyr\ is modified by this. The
REM workaround is scoped to shells that call this file -- unrelated
REM Python processes are unaffected.
REM
REM Must be invoked with `call` so the PYTHONPATH change survives in the
REM current shell.

set "PYTHONPATH=%~dp0pyshim;%PYTHONPATH%"
echo qcc74x_testing: west twister workaround active (PYTHONPATH includes %~dp0pyshim)
