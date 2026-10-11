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

`jtagd` discovers hardware plugins in Quartus's platform library directory and
calls the exported `get_supported_hardware` entry point. Index 0 returns the
operation table when a supported FTDI device is present; other indexes return
null. A plugin filename in the hardware-driver namespace and the expected entry
point allow the server to load it without application-side linking.

The server discovers ports through `ScanPorts` and opens a plugin-owned opaque
`JTAGClientData` handle. Clock callbacks queue MPSSE commands; `Flush` executes
transfers and delivers TDO through the server's callbacks. The server closes the
handle when it finishes using the cable. `mpsse_scan` uses this same interface
inside its own process and accesses D2XX directly.

Each client handle and the in-process divider cache require serialized access.
The implementation does not expose a general-purpose thread-safe API.

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

Requests above 30 MHz, below 458 Hz or equal to zero are rejected. Other requests
choose the fastest realizable frequency that does not exceed the request. A
20 MHz request selects 15 MHz; there is no intermediate setting between 15 and
30 MHz. Parameter readback describes the selected divider, not an electrical
frequency measurement or a guarantee of target compatibility.

Use the cable number shown by `jtagconfig`:

```bash
jtagconfig --setparam 1 JtagClock 30000000
jtagconfig --getparam 1 JtagClock
# Return to the default:
jtagconfig --setparam 1 JtagClock 10000000
```

Queued clocks finish at the old rate before the new divider is programmed. The
selected divider survives close/reopen in the same JTAG server process. The cache
uses the FTDI serial number, or the enumeration index when the serial is empty.
A serial-less adapter cannot be reliably distinguished from a replacement at the
same index. No settings file or EEPROM is written; restarting the server restores
10 MHz. Quartus's GUI may not offer a frequency control for a third-party cable.

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
| `SetParam` / `GetParam` | Set/read `JtagClock`; report `JtagClockAutoAdjust` disabled |
| `Delay` | Flush, then wait in microseconds without generating TCK; Windows rounds the wait upward to milliseconds |
| `GetParamBlock` | Read `SerialNumber`; input length is capacity, output length is bytes copied, with no NUL terminator |

Unknown parameter names and invalid clock requests return `0x2D`. Disabling
`JtagClockAutoAdjust` succeeds; enabling it returns `0x7E` because there is no
target feedback for automatic adjustment.

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
