/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2024 tow3rs
 * Copyright (C) 2026 Sergey Kharenko
 */

#include "mpsse_internal.h"

#include <stddef.h>

/* Changing these layouts breaks the Quartus x86_64 plugin ABI. */
_Static_assert(sizeof(JTAGServerOperations) == 48, "Quartus server table size");
_Static_assert(sizeof(JtagClientOperations) == 208, "Quartus client table size");
_Static_assert(offsetof(JtagClientOperations, Delay) == 0x80, "Quartus Delay offset");
_Static_assert(offsetof(JtagClientOperations, InitDriver) == 0xB8, "Quartus InitDriver offset");

static JtagClientOperations hw_device = {
    .Size = sizeof(JtagClientOperations),
    .Name = "JTAG-MPSSE-Blaster",
    .Features = 0x800,
    .Flags = 0,
    .Reserved1 = NULL,
    .ScanPorts = mpsse_scan_ports,
    .Reserved2 = NULL,
    .OpenHardware = mpsse_open_hardware,
    .CloseHardware = mpsse_close_hardware,
    .SetParam = mpsse_set_param,
    .GetParam = mpsse_get_param,
    .DirectControl = NULL, // USB-Blaster control includes unwired nCE/nCS/ASDO pins.
    .ClockRaw = mpsse_clock_bits,
    .ClockMultiple = mpsse_clock_bytes,
    .Delay = mpsse_delay,
    .Flush = mpsse_flush,
    // jtagd performs recirculation in software when these callbacks are NULL.
    .EnableRecirculate = NULL,
    .DisableRecirculate = NULL,
    .Recirculate = NULL,
    .SetParamBlock = NULL, // No writable block parameters (including EEPROM).
    .GetParamBlock = mpsse_get_param_block,
    .InitDriver = mpsse_init_driver,
    // The four-wire adapter has no Active Serial/nCE/nCS/ASDO wiring.
    .EnableActiveSerial = NULL,
    .RemoveDriver = NULL, // No persistent driver configuration to remove.
};

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
void *get_supported_hardware(int index)
{
    return index == 0 && mpsse_has_supported_device() ? &hw_device : NULL;
}
