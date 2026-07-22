# HW Management

Scripts for managing the QCC7xx hardware test bench. Used by the on-demand Bitbucket pipeline (`on-demand-hw-test`) and can be run locally.

## Files

| Script               | Purpose                                                                                                                                                                                |
|----------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `hw_health_check.py` | Verifies the test bench is alive before a test run: checks west workspace, resets USB devices via PyUSB, waits for serial ports to be stably accessible, then runs a board smoke test. |
| `repo_sync.py`       | Syncs west workspace repos to specified branches/commits. Without arguments, prints current repo status.                                                                               |

## Setup

### Python dependency

`hw_health_check.py` uses [PyUSB](https://github.com/pyusb/pyusb) for USB device resets:

```bash
pip install pyusb
```

PyUSB requires `libusb-1.0` on the system, which is typically pre-installed:

```bash
sudo apt install libusb-1-0-0   # Debian / Ubuntu
```

### udev rule (USB reset permissions)

PyUSB needs write access to `/dev/bus/usb` to reset devices. Without this rule
the reset step will fail with *Access denied (errno 13)*.

#### Step 1 – List the board device files

The health check log prints which `/dev/` symlinks it found, e.g.:

```
/dev/qcc744m_evk_2_1 -> /dev/ttyACM3 (resolved symlink)
```

You can also list all board symlinks at any time:

```bash
ls -la /dev/qcc*
```

This shows the friendly symlink names and which real kernel devices they point
to (`ttyACM0`, `ttyUSB0`, etc.).

#### Step 2 – Find the USB Vendor ID for each board

Pick one of the real device names from step 1 (e.g. `ttyACM3`) and run:

```bash
udevadm info /dev/ttyACM3 | grep ID_VENDOR_ID
```

Expected output for a QCC board:

```
E: ID_VENDOR_ID=1a86
```

If you have multiple boards connected you can check all at once:

```bash
for dev in $(ls /dev/qcc* | xargs -I{} readlink -f {}); do
    echo -n "$dev: "; udevadm info "$dev" | grep ID_VENDOR_ID
done
```

Alternatively, `lsusb` lists all connected USB devices with their vendor IDs in
a human-readable table:

```bash
lsusb
# Look for lines like: Bus 001 Device 004: ID 1a86:55d4 ...
```

#### Step 3 – Create the udev rule

Once you have confirmed the vendor ID (should be `1a86` for QCC boards with the
CH347 USB bridge), create the rule file:

```bash
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", MODE="0664", GROUP="plugdev"' | sudo tee /etc/udev/rules.d/99-qcc-boards.rules
```

This file goes to `/etc/udev/rules.d/` - that is the standard location for
local (non-package-managed) udev rules. The `99-` prefix ensures it is loaded
after all distribution-provided rules.

#### Step 4 – Add your user to the `plugdev` group

```bash
sudo usermod -aG plugdev $USER
```

#### Step 5 – Reload udev and re-trigger rules

```bash
sudo udevadm control --reload && sudo udevadm trigger
```

`udevadm trigger` re-applies all rules to already-connected devices so you do
not need to physically replug the boards.

#### Step 6 – Verify

Log out and back in (group membership change requires a new login session), then
check that your user is now in `plugdev`:

```bash
groups | grep plugdev
```

Re-run the health check — the *Access denied* warning should be gone:

```bash
python hw_management/hw_health_check.py --skip-smoke
```

### ModemManager

If ModemManager is running it will probe newly-enumerated serial ports for
several seconds, causing `EBUSY` when a flash runner tries to open the port.

The script handles this automatically: the serial check uses a stability window
and will not declare a port ready until it has been continuously accessible for
longer than ModemManager's probe cycle (~7 s). No configuration change is
required, but the check will take up to ~15 s per board when ModemManager is
active.

## Usage

Run help for usage examples:

```bash
python hw_health_check.py --help

```
