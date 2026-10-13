# Quartus integration

The project implements a third-party hardware plugin for Quartus's JTAG server.
The callback ABI is based on the upstream plugin and inspection of Quartus
behavior; it is not a published Intel hardware-driver SDK. Treat changes to the
operation-table layout as compatibility changes.

## Loading and ownership

```text
Quartus Programmer / jtagconfig
    -> JTAG server (jtagd)
    -> get_supported_hardware / JtagClientOperations
    -> MPSSE command buffering
    -> FTDI D2XX / USB
    -> FT232H / target JTAG TAP
```

`jtagd` loads the plugin from Quartus's platform library directory through
`get_supported_hardware`. Index 0 returns the operation table when a supported
adapter is present; other indexes return null.

`ScanPorts` enumerates adapters; `OpenHardware` creates a plugin-owned handle.
Clock callbacks queue MPSSE commands, and `Flush` transfers them and returns TDO
through the server callbacks. `CloseHardware` releases the handle. `mpsse_scan`
uses the same interface directly, without Quartus. Handles and the settings cache
require serialized access.

## Deployment

Configure [USB access](../README.md#usb-access) using the installation instructions
in the README before using the adapter with Quartus.

Finish clients using the adapter and stop the relevant JTAG server before
replacing a loaded plugin. Back up the existing plugin if one is present.

On Linux:

```bash
cp export/libjtag_hw_mpsse.so /path/to/quartus/linux64/
```

On Windows, from PowerShell:

```powershell
Copy-Item .\export\jtag_hw_mpsse.dll "C:\path\to\quartus\bin64\"
```

Restart the relevant Quartus tools and JTAG server, then enumerate through the
installation's `bin/jtagconfig` or select the cable in **Programmer > Hardware
Setup**. A file named `libjtag_hw_mpsse.so` is a build of this project, not a
Quartus-supplied MPSSE driver.

## Clock parameters

The FT232H uses a 60 MHz MPSSE master clock with divide-by-five disabled:

```text
TCK = 60,000,000 / (2 * (divider + 1)) Hz
```

The divider is 16 bits. The default is 2, giving 10 MHz.

| Divider | TCK |
| --- | --- |
| 0 | 30 MHz |
| 1 | 15 MHz |
| 2 | 10 MHz |
| 3 | 7.5 MHz |
| 4 | 6 MHz |

`JtagClock` accepts 458–30,000,000 Hz and rounds down to a realizable rate:
20 MHz selects 15 MHz; 12 MHz selects 10 MHz. Readback reports the divider's rate,
not a physical frequency measurement. See the [README](../README.md#quartus-integration)
for commands.

A rate change flushes queued clocks first. Settings survive close/reopen within
the same server process, keyed by FTDI serial number or enumeration index when
no serial is available. Serial-less replacements at the same index share that
cache entry. Restarting the server restores 10 MHz with adjustment disabled;
no settings file or EEPROM is written.

### Automatic adjustment

`JtagClockAutoAdjust` accepts `0` (off, default) or `1` (on). Use Hardware Setup's
**Auto-adjust frequency at chain scanning** checkbox or the
[README commands](../README.md#quartus-integration).

The ABI has no scan callback, so calibration runs after five consecutive
TMS-high clocks, at TAP reset. Enabling the switch generates no clocks. All TAP
resets can trigger calibration, including those outside chain scanning.

The plugin builds a stable reference at 1 MHz, falling back to 100 kHz or 10 kHz.
It then tries 30, 15, 10, 7.5, 6, 3, 1, 0.5 and 0.1 MHz from highest to lowest,
skipping unsupported rates and those at or below the reference. Each candidate
must match five consecutive reads; otherwise it falls back to the next rate.
If none passes, it keeps the reference rate. If no reference is stable, it
restores the previous divider and lets the normal scan diagnose the chain.

Probes support up to 128 reset-selected IDCODE/BYPASS devices, reject stuck
high/low TDO, and leave the TAP in reset. Probe TDO is never delivered to Quartus.
USB errors stop the operation and remain latched until reopen. Calibration adds
scan latency and checks chain communication, not programming stability or RTCK
adaptive clocking. Commands follow [FTDI AN_108](https://ftdichip.com/Support/Documents/AppNotes/AN_108_Command_Processor_for_MPSSE_and_MCU_Host_Bus_Emulation_Modes.pdf).

Disable adjustment before choosing a manual rate; otherwise the next reset can
change it.

## Callback reference

`include/quartus_abi.h` defines the x86_64 operation tables. The client table is
208 bytes, with `Delay` at offset `0x80` and `InitDriver` at `0xB8`. Compile-time
checks protect these offsets. Scalar parameter values and block lengths are
32 bits on both platforms; the client allocation is opaque to callers.

| Callback | Behavior |
| --- | --- |
| `ScanPorts` | Enumerate supported adapters; bound the output name and preserve the real D2XX device index |
| `OpenHardware` | Legacy initialization; nonzero means success |
| `InitDriver` | Same initialization, with zero for success and `0x56` for failure |
| `CloseHardware` | Release the client handle and its FTDI handle |
| `ClockRaw` / `ClockMultiple` | Queue constant-TDI clocks or a TDI bitstream, with constant TMS |
| `Flush` | Execute queued transfers and deliver TDO through `JTAGServerOperations` |
| `SetParam` / `GetParam` | Set/read `JtagClock` and the `JtagClockAutoAdjust` switch |
| `Delay` | Flush, then wait in microseconds without generating TCK; Windows rounds the wait upward to milliseconds |
| `GetParamBlock` | Read `SerialNumber`; input length is capacity, output length is bytes copied, with no NUL terminator |

Unknown parameter names, invalid clock requests and automatic-adjustment values
other than `0` or `1` return `0x2D`.

The recirculation callbacks remain null so the server can use its software
fallback. `DirectControl` and `EnableActiveSerial` remain null because the
four-wire wiring does not provide nCE/nCS/ASDO. `SetParamBlock` and `RemoveDriver`
remain null because this adapter has no writable block parameters or persistent
driver configuration.

## Failure handling

Initialization purges stale data and checks both MPSSE synchronization replies
before accepting JTAG traffic. A failure stops initialization and releases the
handle. Transfers handle partial progress and reject driver errors, zero progress
and missing replies. After a transport failure, queued TDO is discarded and the
error remains latched until the adapter is closed and reopened.

| Symptom | Check |
| --- | --- |
| No cable appears | USB enumeration, supported VID/PID, plugin location and loaded server instance |
| Cable cannot be opened | USB permissions, kernel driver ownership and another process using the adapter |
| Chain scan fails | TCK setting, signal levels, common ground, wiring and target power |
| Clock readback differs from the request | Divider rounding and whether the JTAG server has restarted |
| A transport error remains after reconnecting | Close and reopen the adapter to clear the latched error |
