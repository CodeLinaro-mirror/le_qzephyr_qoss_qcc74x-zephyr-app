#!/bin/bash

#========================================================================
#Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#SPDX-License-Identifier: BSD-3-Clause-Clear
#integration entry
#========================================================================

SRC="../../prop/qcc74x-bin"
DST="../../modules/hal/qcom/qcc74x/zephyr/blobs/qcc74x"

# ─────────────────────────────────────────
#(SRC file list)
# ─────────────────────────────────────────
FILES=(
    bin/qcc74x_lp_fw.bin
    bin/boot2_qcc743_isp_release_v8.1.9.bin
    bin/flash_para.bin
    lib/default/libqcc743_wifi_macsw.a
    lib/lp/libqcc743_wifi_macsw.a
    lib/default/libqcc743_wifi_fhost.a
    lib/lp/libqcc743_wifi_fhost.a
    lib/libbtblecontroller.a
    lib/libqcc743_phyrf.a
    lib/libqcc743_lpphyrf.a
    lib/libqcc743_phyrf4mfg.a
    lib/libcsi_xt900p32f_dsp.a
    tool/QConn_Secure-ubuntu
    tool/QConn_Secure.exe
    tool/QConn_Flash-ubuntu
    tool/QConn_Flash.exe
    tool/QConn_Flash_Cmd-ubuntu
    tool/QConn_Flash_Cmd.exe
)
# ─────────────────────────────────────────
# check src file exist
# ─────────────────────────────────────────
if [[ ! -d "$SRC" ]]; then
    echo "[ERROR] Source directory not found: $SRC"
    exit 1
fi

# ─────────────────────────────────────────
# copying
# ─────────────────────────────────────────
failed=0

for f in "${FILES[@]}"; do
    src_file="$SRC/$f"
    dst_file="$DST/$f"
    dst_dir="$(dirname "$dst_file")"

    # source file does not exist,skip
    if [[ ! -f "$src_file" ]]; then
        echo "[WARN]  MISSING SRC: $src_file"
        (( failed++ ))
        continue
    fi

    # create dest dir
    mkdir -p "$dst_dir"

    cp "$src_file" "$dst_file" && echo "[INFO]   $f" || {
        echo "[ERROR] Failed to copy: $f"
        (( failed++ ))
    }
done

# ─────────────────────────────────────────
# chmod +x
# ─────────────────────────────────────────
EXEC_TOOLS=(
    tool/QConn_Secure-ubuntu
    tool/QConn_Flash-ubuntu
    tool/QConn_Flash_Cmd-ubuntu
)

echo "──────────────────────────────────────"
for t in "${EXEC_TOOLS[@]}"; do
    chmod +x "$DST/$t" && echo "[INFO]  🔧 chmod +x: $t"
done

# ─────────────────────────────────────────
# summary
# ─────────────────────────────────────────
echo "──────────────────────────────────────"
total=${#FILES[@]}
if [[ $failed -eq 0 ]]; then
    echo "[INFO]  All $total files copied successfully"
    exit 0
else
    echo "[ERROR] $failed / $total files failed"
fi
