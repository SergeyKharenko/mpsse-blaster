# mpsse-blaster

A JTAG adapter for Intel Quartus using FTDI MPSSE devices, with a Quartus plugin and a standalone JTAG scan tool.

Supports Linux and Windows on x86_64. The default device is the FT232H with VID/PID `0403:6014`.

## Preparing the FTDI SDK

Download the **x86_64 D2XX SDK for your operating system**, including headers and a static library, from the [FTDI D2XX driver page](https://ftdichip.com/drivers/d2xx-drivers/).

Extract the SDK into `lib/` and arrange the required files as shown below. Other files from the SDK may remain in these directories.

### Linux

```text
lib/
└── linux-x86_64/
    ├── ftd2xx.h
    ├── WinTypes.h
    └── libftd2xx.a
```

The static library must contain position-independent code (PIC) suitable for linking into a shared library.

### Windows

```text
lib/
└── windows-x86_64/
    ├── ftd2xx.h
    └── Static/
        └── amd64/
            └── ftd2xx.lib
```

CMake also accepts a static `ftd2xx.lib` directly under `lib/windows-x86_64/`. Use the SDK's **static library**, rather than the DLL import library, which may have the same filename. Install the FTDI device driver on Windows.

The SDK is not included in this repository. All contents of `lib/` are ignored by Git except `lib/.gitkeep`, which preserves the directory.

Both the plugin and the scan tool link D2XX statically. They do not require a separate `libftd2xx.so` or `ftd2xx.dll` at runtime, or a custom FTDI shared library search path.

## Building

Requirements:

- CMake 3.21 or later.
- A C11 compiler targeting x86_64: GCC or Clang on Linux, or Visual Studio 2022 on Windows.
- The FTDI SDK prepared as described above.

Run all commands from the repository root.

### Linux

Install the build tools for your distribution:

```bash
# Fedora
sudo dnf install gcc make cmake

# Debian / Ubuntu
sudo apt install build-essential cmake
```

Build a release version:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Windows

Install CMake and Visual Studio 2022 with the **Desktop development with C++** workload. Run the following commands in PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

### Build outputs and options

Intermediate build files are stored in `build/`. All binaries are written to `export/`:

| Component | Linux | Windows |
| --- | --- | --- |
| Quartus plugin | `export/libjtag_hw_mpsse.so` | `export/jtag_hw_mpsse.dll` |
| JTAG scan tool | `export/jtag_scan_tool` | `export/jtag_scan_tool.exe` |

Debug and Release configurations share these output paths; building another configuration overwrites the existing binaries. For a debug build, use `-DCMAKE_BUILD_TYPE=Debug` on Linux or `--config Debug` on Windows.

The scan tool is built by default. Add `-DBUILD_SCAN_TOOL=OFF` to the configure command to build only the plugin.

To use an SDK outside the default location, add `-DFTDI_D2XX_ROOT=/path/to/sdk`. If the headers and library are stored separately, or the library has a different filename, specify their paths explicitly:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DFTDI_D2XX_INCLUDE_DIR=/path/to/headers \
  -DFTDI_D2XX_LIBRARY=/path/to/libftd2xx.a
cmake --build build --parallel
```

On Windows, specify the path to the corresponding x64 static `.lib` file.

## Installation

### Linux

Install the USB access rule:

```bash
sudo cp 99-mpsse-blaster.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Reconnect the device to apply the rule. The provided rule matches VID/PID `0403:6014` and the product string `Single RS232-HS`. It grants access permissions but does not unbind the `ftdi_sio` kernel driver. If that driver has claimed the device, unbind the relevant interface before using D2XX.

Copy the plugin into your Quartus installation's `linux64/` directory, replacing the example path:

```bash
cp export/libjtag_hw_mpsse.so /path/to/quartus/linux64/
```

Use `sudo` if the destination requires elevated permissions. Restart the Quartus tools and `jtagd`, then select the interface in **Programmer > Hardware Setup**, or check it with Quartus's `bin/jtagconfig`.

### Windows

Install the FTDI device driver, then copy the plugin into your Quartus installation's `bin64/` directory, replacing the example path:

```powershell
Copy-Item .\export\jtag_hw_mpsse.dll "C:\path\to\quartus\bin64\"
```

Restart the Quartus tools and the JTAG service, then check the interface using Programmer or `jtagconfig`.

### Optional installation prefix

To install the build outputs into a separate directory:

```bash
cmake --install build --config Release --prefix /path/to/install
```

The plugin is installed under `lib` or `lib64`, the scan tool under `bin`, and the Linux udev rule under `share/mpsse-blaster`. Copy the plugin and rule to their deployment locations using the steps above.

## Using the scan tool

The scan tool's source is in `tool/JTAG-Scan-Tool.c`, and its executable is in `export/`. It uses D2XX directly and does not require Quartus or an installed Quartus plugin.

Connect the FT232H to the target board with a common ground and compatible signal voltage levels:

| FT232H pin | JTAG signal |
| --- | --- |
| ADBUS0 | TCK |
| ADBUS1 | TDI |
| ADBUS2 | TDO |
| ADBUS3 | TMS |
| GND | GND |

On Linux, configure USB access as described above, then run:

```bash
./export/jtag_scan_tool
```

On Windows:

```powershell
.\export\jtag_scan_tool.exe
```

The tool takes no command-line arguments. It scans supported interfaces and prints the raw TDO data and the IDCODE of the first device in each JTAG chain. Example output:

```text
Device:         [JTAG-MPSSE-Blaster]

  Port:                 [00 Single RS232-HS (0403:6014)]
  Raw TDO bytes:        FF FF DD 60 E0 02
  JTAG IDCode:          02E060DD
```

After a supported interface is found, press Enter to scan again or `Ctrl+C` to exit.

- `No devices`: Check the USB connection, VID/PID, and driver. Press Enter to exit, then rerun the tool after resolving the issue.
- `Unable to open port`: Check access permissions and whether another application or a kernel driver has claimed the device.

The FT232H TCK frequency is fixed at 15 MHz; configuring a custom frequency is not currently supported. Linux builds, installation, and plugin loading have been verified. Windows builds and JTAG operation on physical hardware have not yet been verified for this version.

## License

The project source is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE).

FTDI D2XX is subject to its own license and is not covered by this project's GPL license. Confirm license compatibility before distributing binaries that statically link D2XX.

## Acknowledgments

This project is derived from [tow3rs/jtag-mpsse-blaster](https://github.com/tow3rs/jtag-mpsse-blaster), originally developed by [tow3rs](https://github.com/tow3rs).

This version is maintained by Sergey Kharenko and introduces CMake builds, static D2XX linking, and a revised project layout.
