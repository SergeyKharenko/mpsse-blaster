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
| `src/mpsse_autotune.c` | Bounded chain probes and automatic clock selection at TAP reset |
| `src/mpsse_jtag.c` | JTAG command buffering, TDO delivery and delay barriers |
| `include/mpsse_internal.h` | Private types and cross-module declarations |
| `tool/mpsse_scan.c` | Standalone consumer of the operation table |

Keep D2XX types and implementation details out of the public ABI header. The
Quartus table uses its existing member names and layout; private functions and
variables use `snake_case`, types use `PascalCase`, and constants use `UPPER_CASE`.
Private helpers should be `static`. Only `get_supported_hardware` is exported by
the plugin. Do not add dependencies between modules through included `.c` files.

Use `.clang-format` and `.editorconfig`. Format only project sources, not FTDI
SDK headers. Handle D2XX errors immediately, bound buffers, release handles on
initialization failure, and keep transport errors latched until reopen. Preserve
queued-operation ordering and serialized access to handles and settings.

## Validation

Build both targets using the [README instructions](README.md#building), then
check formatting:

```bash
clang-format --dry-run --Werror src/*.c include/*.h tool/*.c
```

Describe the platforms built and verification performed. Use an available
adapter and an isolated JTAG server for hardware checks; do not interrupt other
sessions. Disable automatic adjustment and restore 10 MHz afterward. Building
does not deploy the plugin, and chain-scan success does not prove programming
stability.
