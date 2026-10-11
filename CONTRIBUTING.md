# Contributing

The project uses C11, CMake and GPL-3.0-only. Keep changes focused and include a
description of the problem, the resulting behavior and the validation performed.
Preserve existing copyright notices and add an SPDX identifier to new source files.

## Code organization

| Path | Responsibility |
| --- | --- |
| `include/quartus_abi.h` | SDK-independent Quartus operation tables and opaque client handle |
| `src/quartus_plugin.c` | Operation table, ABI layout checks and exported entry point |
| `src/mpsse_device.c` | FTDI discovery, initialization, cleanup and divider retention |
| `src/mpsse_transport.c` | USB transfers and MPSSE synchronization |
| `src/mpsse_clock.c` | Clock dividers and scalar/block parameters |
| `src/mpsse_jtag.c` | JTAG command buffering, TDO delivery and delay barriers |
| `include/mpsse_internal.h` | Private types and cross-module declarations |
| `tool/mpsse_scan.c` | Standalone consumer of the operation table |

Keep D2XX types and implementation details out of the public ABI header. The
Quartus table uses its existing member names and layout; private functions and
variables use `snake_case`, types use `PascalCase`, and constants use `UPPER_CASE`.
Private helpers should be `static`. Only `get_supported_hardware` is exported by
the plugin. Do not add dependencies between modules through included `.c` files.

Use the repository's `.clang-format` and `.editorconfig`; formatting checks use
clang-format 18.1.8. Format only project sources, not user-provided FTDI headers:

```bash
clang-format -i src/*.c include/*.h tool/*.c
```

Handle each D2XX error immediately. Status codes are values, not bit flags. Close
owned handles on initialization failures, bound caller-provided buffers and keep
transport errors visible until the adapter is reopened. Preserve queued-operation
ordering when setting the clock or delaying. Client operations and the in-process
device/divider cache require serialized access; this code does not promise a
general-purpose thread-safe public API.

## Build checks

Build the plugin and scanner against the platform's D2XX SDK before submitting
changes. From the repository root on Linux:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

For Windows build commands and SDK configuration, see [README.md](README.md).
Run formatting checks over project sources:

```bash
clang-format --dry-run --Werror src/*.c include/*.h tool/*.c
```

Describe the platforms built and any hardware verification performed. Use an
available adapter and an isolated JTAG server when other Quartus processes are
running. Deployment is separate from building. After checking a different TCK
setting, restore the default 10 MHz. A successful chain scan establishes chain
communication; programming stability and electrical frequency measurements
require their own verification.
