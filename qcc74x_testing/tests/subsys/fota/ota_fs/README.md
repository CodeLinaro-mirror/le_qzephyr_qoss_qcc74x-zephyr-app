# QCC748 LittleFS local OTA ztest

This hardware ztest stages `/lfs/zephyr.bin.ota` from a preloaded 2 MiB
LittleFS partition into the inactive firmware slot, verifies its SHA-256 and
source-fixture retention, then activates the verified image. Reset manually
to boot the newly active slot.

## Fixture provisioning

Create a complete LittleFS partition image. The input OTA artifact remains
unchanged and appears on the device as `/lfs/zephyr.bin.ota`.

```sh
python3 qcc74x-zephyr/scripts/lfs/make_littlefs_image.py \
  --file /path/to/zephyr.bin.ota --size 2M --output littlefs.bin
```

The script requires the host `mklittlefs` tool. Its geometry matches
`prj.conf`: 2 MiB partition, 4 KiB erase block, 16-byte read/program size.

Build the fixture from the current ztest `.ota` artifact after every firmware
build. Flash the ztest firmware normally with `west flash`, then use QConn raw
write to program the filesystem image at `0x00400000`:

```sh
QConn_Flash_Cmd-ubuntu --interface uart --port /dev/ttyACM0 \
  --chipname qcc748 --write --start 0x00400000 --file littlefs.bin
```

Use the appropriate UART port and ISP-mode procedure for the test setup.
Do not use this offset on hardware whose partition layout has not been
verified for this test overlay.

The fixture image, not the firmware written by `west flash`, becomes the
payload booted from the alternate slot. Before resetting, check that the DUT's
`OTA file: found <N> bytes` log matches the current input `.ota` size; a stale
fixture can make a successful first run boot an old test image on the second
run.
