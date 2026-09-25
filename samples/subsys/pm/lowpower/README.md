# QCC74x PM low-power demo

This minimal QCC748M application demonstrates one PDS suspend-to-RAM cycle,
followed by HBN hardware system-off through the QCC74x HAL HBN primitives. It
does not include Wi-Fi, Bluetooth, the network stack, shell commands, or Wi-Fi
diagnostics.

The SoC's default PDS auto-init initializes LPFW and RTC support after boot.
The application retries automatic-policy enablement only while that delayed
initialization is in progress. The application does not call the direct
PDS-entry API; the idle PM manager selects PDS.

## PDS then HBN demonstration

QCC74x declares the following relevant exit latencies in DTS:

- Shallow suspend-to-idle: 50 us
- PDS: 8000 us

After automatic-policy enablement, the demo first adds a generic Zephyr PM
policy latency request of 1000 us and sleeps for ten seconds. The 1 ms limit
is below PDS's 8 ms exit latency, so this phase demonstrates that the policy
blocks PDS. It then updates that request to 10000 us and sleeps for another
ten seconds. The 10 ms limit exceeds PDS's DTS exit latency, so the normal
QCC74x policy can select PDS and use that timeout as an RTC wake source.

The sample uses immediate logging so each lifecycle message is written to UART
in the caller's context rather than waiting for a deferred log-processing
thread. It holds PDS policy-locked while preparing the PDS interval, and uses
a 10 ms busy-wait after the PDS-entry and HBN-entry messages. This allows the
UART hardware to finish shifting the final characters before PDS or the
non-returning HBN transition can start.

After the one PDS wake, the application uses the HAL HBN0 entry sequence. It
initializes the Boot2 HBN parameters and configures the EVK's `sw0` GPIO2 as
an ACOMP rising-edge wake source. It does not generate that edge, so it remains
in HBN soft-off unless an external GPIO2 transition occurs. An HBN wake
cold-boots the system.

The demo deliberately does not enable `CONFIG_POWEROFF`: its QCC74x system-off
backend selects a SoC early-init hook that is not compatible with this PDS
resume path. PDS already links the HAL HBN implementation used by the sample.
The sample CMake file explicitly links the HAL system helper needed by both
LPFW and HBN. `src/main.c` also provides LPFW's otherwise unconditional Wi-Fi
RX-buffer export as `NULL`. These are required LPFW link dependencies; they do
not enable Wi-Fi, Bluetooth, or networking.

LPFW verification uses the QCC74x SEC SHA engine and accepts the image only
when its digest exactly matches the embedded expected digest. The sample
enables the board TRNG entropy driver so it configures and enables the shared
SEC0 clock domain at PRE_KERNEL_1, before PDS's delayed LPFW auto-init. This
is an SEC clock prerequisite, not a software SHA fallback; the HAL LPFW load
and verification routines remain unchanged from the SDK.

## Build

```sh
west build -p always -b qcc748m_evk \
  --build-dir build_pm_lowpower_demo \
  qapp/qcc74x/samples/subsys/pm/lowpower \
  -- -DZEPHYR_TOOLCHAIN_VARIANT=zephyr
```

## Run expectations

Flash the resulting image using the normal board workflow, then observe UART.
The first ten-second interval follows the 1 ms request and keeps PDS blocked.
The second interval follows the 10 ms request, during which the board can
enter PDS (target current below 1 mA) and resume on the RTC timeout. It then
enters HBN soft-off. This sample has not been designed to preserve radio
connectivity because radio and network features are intentionally excluded.
