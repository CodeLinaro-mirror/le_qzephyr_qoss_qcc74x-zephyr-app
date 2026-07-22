# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

"""Runtime workaround for `west twister` on this workspace's Zephyr v4.4.0
(Qualcomm Gerrit mirror) fork.

Two independent, upstream-caused issues break `west twister` (but not
`west build`) on this fork, and this file works around both WITHOUT
modifying any file under zephyr/ or qcc74x_zephyr/:

1. Duplicate board definition: the Gerrit fork ships
   zephyr/boards/qcom/qcc744m_evk/board.yml (SoC: bl618m05q2i) which
   collides by name with the correct out-of-tree module definition at
   qcc74x_zephyr/boards/qcom/qcc744m_evk/board.yml (SoC: qcc744).
   `west build -b <board>` filters by board name before the conflict
   check runs, so it never trips this. `west twister` must enumerate
   ALL boards before applying `-p <board>`, so the conflict always
   fires first, regardless of which board you actually asked for --
   there's no way to route around this by choosing a different `-p`.

2. `--edtlib-Werror` escalation: `west twister`'s CMake invocation
   unconditionally passes -DEXTRA_GEN_EDT_ARGS=--edtlib-Werror unless
   --disable-warnings-as-errors is given, turning the known
   'riscv,isa' deprecated-property warning (from
   qcc74x_zephyr/dts/riscv/qcom/qcc74x/qcc74x.dtsi) into a fatal
   CMake error. `west build` does not pass this flag by default, so
   it never surfaces there.

This file is loaded automatically by Python's `site` module for any
interpreter whose PYTHONPATH includes this directory -- see
`source ./regression/env.sh` in this same folder. It has no effect on
any Python process that doesn't have this directory on PYTHONPATH, so
it's opt-in per-shell, not a global site-wide change.

Track upstream fix: qcom Gerrit fork should drop
zephyr/boards/qcom/qcc744m_evk/ entirely -- board definitions for
QCC74x belong in the qcc74x_zephyr out-of-tree module only.
"""

import os
import sys


def _resolve_zephyr_base():
    env_base = os.environ.get("ZEPHYR_BASE")
    if env_base:
        return os.path.abspath(env_base)
    # Fallback: this file lives at
    # <workspace>/qapp/qcc74x/qcc74x_testing/regression/pyshim/sitecustomize.py
    here = os.path.dirname(os.path.abspath(__file__))
    workspace_root = os.path.abspath(os.path.join(here, "..", "..", "..", "..", ".."))
    return os.path.join(workspace_root, "zephyr")


_ZEPHYR_BASE = _resolve_zephyr_base()
_STALE_BOARD_YML = os.path.normpath(
    os.path.join(_ZEPHYR_BASE, "boards", "qcom", "qcc744m_evk", "board.yml")
)


def _patch_list_boards_module(module):
    """Skip the Gerrit fork's stale qcc744m_evk/board.yml during scans."""
    if getattr(module, "_qcc74x_es_patched", False):
        return

    _orig_load_v2_boards = module.load_v2_boards

    def _patched_load_v2_boards(board_name, board_yml, systems):
        if os.path.normpath(str(board_yml)) == _STALE_BOARD_YML:
            return {}, []
        return _orig_load_v2_boards(board_name, board_yml, systems)

    module.load_v2_boards = _patched_load_v2_boards
    module._qcc74x_es_patched = True


def _patch_environment_module(module):
    """Make --disable-warnings-as-errors the default for twister runs.

    This only relaxes edtlib's deprecated-property check back to a
    warning (matching `west build`'s default behavior) -- it does not
    change compiler warning-as-error behavior for anything else. If a
    caller explicitly wants strict mode back, remove this shim from
    PYTHONPATH for that invocation (there's no CLI flag to re-enable
    strict mode once this default is applied, since twister's own
    parser only exposes --disable-warnings-as-errors as a store_true
    flag with no negation).
    """
    if getattr(module, "_qcc74x_es_patched", False):
        return

    _orig_parse_arguments = module.parse_arguments

    def _patched_parse_arguments(parser, args, options=None, on_init=True):
        result = _orig_parse_arguments(parser, args, options=options, on_init=on_init)
        result.disable_warnings_as_errors = True
        return result

    module.parse_arguments = _patched_parse_arguments
    module._qcc74x_es_patched = True


_PATCHERS = {
    "list_boards": _patch_list_boards_module,
    "twisterlib.environment": _patch_environment_module,
}


class _PostImportExecWrapper:
    """Loader wrapper that runs a patch function right after the real
    loader's exec_module() finishes populating the module.

    Delegates every other Loader method to the wrapped loader via
    __getattr__ so this only changes exec_module's behavior.
    """

    def __init__(self, real_loader, patch_fn):
        self._real_loader = real_loader
        self._patch_fn = patch_fn

    def __getattr__(self, name):
        return getattr(self._real_loader, name)

    def create_module(self, spec):
        return self._real_loader.create_module(spec)

    def exec_module(self, module):
        self._real_loader.exec_module(module)
        self._patch_fn(module)


class _QCC74xImportPatcher:
    """Meta-path hook: patch `list_boards` and `twisterlib.environment`
    the moment each finishes loading, without modifying any file under
    zephyr/ or qcc74x_zephyr/.

    twister's own scripts import these modules lazily, so patching at
    sitecustomize load time is too early (ZEPHYR_BASE's scripts dir
    isn't even on sys.path yet). Hooking the import machinery instead
    lets the patch apply right after each module actually loads,
    regardless of which entry point (`west twister`,
    `python3 .../twister`, `run_regression.py`) triggered the import.

    Implements find_spec (not the legacy find_module) -- Python 3.12
    dropped automatic find_module fallback support for meta_path
    finders, so a find_module-only finder is silently never invoked.

    This does NOT `import` the target module itself from inside
    find_spec -- doing so re-enters the import machinery for the same
    fullname while the original import is still in progress, which
    can hand back a second, discarded module object that never
    reaches the caller (verified empirically: id() differs between
    the object patched from inside a recursive import and the object
    the outer `import` statement ultimately binds). Instead, this
    finder locates the *real* spec by asking the rest of sys.meta_path
    (skipping itself) via importlib.util._find_spec, then wraps that
    spec's loader so the patch runs inside exec_module -- i.e. after
    the real module is fully populated and already the one that will
    be cached in sys.modules and returned to the caller.
    """

    def find_spec(self, fullname, path=None, target=None):
        patch_fn = _PATCHERS.get(fullname)
        if patch_fn is None:
            return None

        rest = [f for f in sys.meta_path if f is not self]
        real_spec = None
        for finder in rest:
            find = getattr(finder, "find_spec", None)
            if find is None:
                continue
            real_spec = find(fullname, path, target)
            if real_spec is not None:
                break

        if real_spec is None or real_spec.loader is None:
            return real_spec

        real_spec.loader = _PostImportExecWrapper(real_spec.loader, patch_fn)
        return real_spec


sys.meta_path.insert(0, _QCC74xImportPatcher())
