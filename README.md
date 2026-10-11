# mpsse-blaster

A C11 JTAG adapter implementation for FTDI MPSSE devices. The project builds a
Quartus hardware plugin and `mpsse_scan`, a standalone IDCODE scanner.

The supported adapter is the FT232H (`0403:6014`). Build targets are Linux and
Windows on x86_64. Both binaries link the FTDI D2XX library statically.

## Project layout

```text
include/        Quartus ABI definitions and internal module declarations
src/            Device management, USB transport, clocks and JTAG buffering
tool/           Standalone mpsse_scan application
docs/           Quartus integration and callback reference
```

`include/quartus_abi.h` defines the operation tables and an opaque client handle.
`include/mpsse_internal.h` contains implementation details. The plugin exports
`get_supported_hardware`; the scanner consumes the same operation table without
requiring Quartus.

See [CONTRIBUTING.md](CONTRIBUTING.md) for module responsibilities and code style.

## Build requirements

- CMake 3.21 or later.
- An x86_64 C11 compiler: GCC/Clang on Linux or Visual Studio 2022 on Windows.
- The platform's x86_64 [FTDI D2XX SDK](https://ftdichip.com/drivers/d2xx-drivers/)
  with headers and a static library.

Place the SDK headers and static library under `lib/`:

```text
lib/
├── linux-x86_64/
│   ├── ftd2xx.h
│   ├── WinTypes.h
│   └── libftd2xx.a
└── windows-x86_64/
    ├── ftd2xx.h
    └── Static/amd64/ftd2xx.lib
```

Only the directory for your platform is required. The Linux library must contain
position-independent code suitable for a shared library. On Windows, use the
static library rather than the similarly named DLL import library; a static
`ftd2xx.lib` directly under `windows-x86_64/` is also accepted.

The SDK is supplied by the developer and excluded from version control. Neither
binary requires a separate `libftd2xx.so` or `ftd2xx.dll` at runtime. Windows
hardware access requires the FTDI device driver.

## Building

Run commands from the repository root.

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Windows

From PowerShell with Visual Studio's C/C++ build tools installed:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

### Targets and options

| Target | Linux output | Windows output |
| --- | --- | --- |
| `jtag_hw_mpsse` | `export/libjtag_hw_mpsse.so` | `export/jtag_hw_mpsse.dll` |
| `mpsse_scan` | `export/mpsse_scan` | `export/mpsse_scan.exe` |

| CMake option | Default | Purpose |
| --- | --- | --- |
| `BUILD_PLUGIN` | `ON` | Build the Quartus plugin |
| `BUILD_SCAN_TOOL` | `ON` | Build `mpsse_scan` |
| `FTDI_D2XX_ROOT` | `lib/<platform>-x86_64` | Locate the D2XX SDK |
| `MPSSE_OUTPUT_DIRECTORY` | `export/` | Select the production binary output directory |

For example, build only the scanner with an SDK outside the repository:

```bash
cmake -S . -B build -DBUILD_PLUGIN=OFF -DFTDI_D2XX_ROOT=/path/to/sdk
cmake --build build --target mpsse_scan --parallel
```

If the headers and library are stored separately, set `FTDI_D2XX_INCLUDE_DIR`
and `FTDI_D2XX_LIBRARY` explicitly. Debug and Release builds share the default
output directory; use separate `MPSSE_OUTPUT_DIRECTORY` values to keep both.

## Installation

### USB access

Both `mpsse_scan` and the Quartus plugin need access to the FT232H.
On Linux, install the provided access rule:

```bash
sudo cp 99-mpsse-blaster.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Reconnect the adapter. The rule matches VID/PID `0403:6014` and the product string
`Single RS232-HS`. This rule sets USB permissions; D2XX also needs the interface
released from the Linux `ftdi_sio` serial driver.

Disable `ftdi_sio` automatic loading and unload the currently loaded module:

```bash
printf '%s\n' 'blacklist ftdi_sio' | sudo tee /etc/modprobe.d/mpsse-blaster.conf
sudo modprobe -r ftdi_sio
```

This disables FTDI serial-port support system-wide. Close applications using
FTDI serial ports before unloading the module. If it reports that the module is
in use, release those ports and retry; do not force removal.

Reconnect the adapter and run `lsusb -t` to confirm that its interface is no
longer bound to `ftdi_sio`. The blacklist prevents normal automatic loading after
reconnection or reboot. If `ftdi_sio` is loaded from the initramfs during boot,
rebuild the initramfs after adding the blacklist (`sudo dracut -f` on Fedora,
or `sudo update-initramfs -u` on Debian/Ubuntu).

On Windows, install the [FTDI device driver](https://ftdichip.com/drivers/d2xx-drivers/).

### Quartus plugin

To use the adapter with Quartus, follow the
[plugin deployment instructions](docs/quartus-integration.md#deployment).
The standalone scanner can run after USB access is configured.

### Installation prefix

An optional install prefix places the plugin under `lib` or `lib64`, the scanner
under `bin`, and the Linux udev rule under `share/mpsse-blaster`:

```bash
cmake --install build --config Release --prefix /path/to/install
```

Building or installing to a separate prefix does not deploy the Quartus plugin.

## Using mpsse_scan

Wire the FT232H to the target with compatible signal levels and a common ground:

| FT232H pin | Signal |
| --- | --- |
| ADBUS0 | TCK |
| ADBUS1 | TDI |
| ADBUS2 | TDO |
| ADBUS3 | TMS |
| GND | GND |

After configuring [USB access](#usb-access), run on Linux:

```bash
./export/mpsse_scan
```

On Windows, run `.\export\mpsse_scan.exe`.

The scanner resets the TAP and reads the first device's default 32-bit IDCODE.
Press Enter to repeat, or Ctrl+C to exit. EOF exits after the current scan, so
`./export/mpsse_scan </dev/null` performs one scan on Linux. Devices whose reset
instruction selects BYPASS instead of IDCODE need a different scan sequence.

## Quartus integration

Deploy the plugin to Quartus's platform library directory, then select
`JTAG-MPSSE-Blaster` in Programmer or enumerate it with `jtagconfig`.
The [integration guide](docs/quartus-integration.md) covers deployment, plugin
loading, callback behavior and troubleshooting. This is a third-party plugin;
the Quartus hardware-driver ABI is not a published SDK contract.

The default TCK is **10 MHz**. `JtagClock` accepts frequencies in Hz, including
**30 MHz**, and reports the divider's realized rate. Hardware operation at a
selected rate depends on the target and wiring. To select 15 MHz, using the cable
number returned by `jtagconfig`:

```bash
jtagconfig --setparam 1 JtagClock 15000000
jtagconfig --getparam 1 JtagClock
```

The high-speed divider produces 30, 15, 10, 7.5, 6 MHz and further lower rates.
Requests round down to the fastest realizable rate at or below the request:
20 MHz selects 15 MHz, and 12 MHz selects 10 MHz. There is no divider setting
between 15 and 30 MHz. Restarting the JTAG server restores the 10 MHz default.

## License and origin

Project sources use **GPL-3.0-only**; see [LICENSE](LICENSE). FTDI D2XX has its own
license and is not included in this repository.

Derived from [tow3rs/jtag-mpsse-blaster](https://github.com/tow3rs/jtag-mpsse-blaster),
originally developed by [tow3rs](https://github.com/tow3rs). Maintained by
Sergey Kharenko.
