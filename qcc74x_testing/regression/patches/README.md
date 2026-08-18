# Zephyr Patches for QCC74x Twister Testing

Patches in this directory must be applied to the `zephyr/` tree before
running `west twister` with QCC74x hardware.

## Apply a patch

Run from the **workspace root**:

**Windows (CMD)**
```bat
git -C zephyr apply --ignore-whitespace ..\qapp\qcc74x\qcc74x_testing\regression\patches\twister_rts_dtr_fix.patch
```

**Linux / macOS**
```sh
git -C zephyr apply --ignore-whitespace ../qapp/qcc74x/qcc74x_testing/regression/patches/twister_rts_dtr_fix.patch
```


## Revert a patch

Run from the **workspace root**:

**Windows (CMD) / Linux / macOS**
```
git -C zephyr checkout scripts/pylib/twister/twisterlib/handlers.py
```

---

## Patch list

| File | Description |
|------|-------------|
| `twister_rts_dtr_fix.patch` | QCC74x EVK (`qcc743m_evk` / `qcc744m_evk` / `qcc748m_evk`): perform RTS/DTR reset pulse and deferred serial open after flashing, so the CH347 boot log is not missed. |