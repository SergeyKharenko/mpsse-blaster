/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2024 tow3rs
 * Copyright (C) 2026 Sergey Kharenko
 */

#include "mpsse_internal.h"

#include <string.h>

uint32_t mpsse_clock_base_hz(const JTAGClientData *mpsse_blaster)
{
    // FT232H uses a 60 MHz MPSSE clock after divide-by-5 is disabled.
    // Other MPSSE devices keep the 12 MHz clock divided by two.
    return mpsse_blaster->is_high_speed ? 60000000U : 12000000U;
}

static FT_STATUS calculate_clock_divider(const JTAGClientData *mpsse_blaster,
                                         uint32_t requested_clock, unsigned short *clock_divisor,
                                         DWORD *actual_clock)
{
    if (!mpsse_blaster || !clock_divisor || !actual_clock || requested_clock == 0)
    {
        return FT_INVALID_PARAMETER;
    }

    uint64_t base_clock = mpsse_clock_base_hz(mpsse_blaster);
    uint64_t numerator = base_clock / 2;
    uint64_t divisor = (numerator + requested_clock - 1) / requested_clock - 1;

    if (requested_clock > numerator || divisor > 0xFFFF)
    {
        return FT_INVALID_PARAMETER;
    }

    *clock_divisor = (unsigned short)divisor;
    *actual_clock = (DWORD)(base_clock / (2 * (divisor + 1)));
    return FT_OK;
}

static FT_STATUS program_clock_divider(JTAGClientData *mpsse_blaster, unsigned short clock_divisor)
{
    unsigned char buffer[3] = {
        FT_OPCODE_SET_CLOCK_DIVIDER,
        (unsigned char)(clock_divisor & 0xFF),
        (unsigned char)((clock_divisor >> 8) & 0xFF),
    };
    FT_STATUS status =
        mpsse_transfer(mpsse_blaster->device_handle, buffer, sizeof(buffer), NULL, 0);
    if (status == FT_OK)
    {
        mpsse_blaster->clock_divisor = clock_divisor;
        mpsse_blaster->jtag_clock = mpsse_clock_base_hz(mpsse_blaster) / (2 * (clock_divisor + 1));
    }
    return status;
}

uint32_t mpsse_get_param(JTAGClientData *mpsse_blaster, char *param_name, uint32_t *param_value)
{
    if (!mpsse_blaster || !param_name)
    {
        return JTAG_UNSUPPORTED_PARAMETER;
    }

    if (!strcmp(param_name, JTAG_CLOCK_PARAM))
    {
        if (param_value)
        {
            *param_value = mpsse_blaster->jtag_clock;
        }
        return 0;
    }

    if (!strcmp(param_name, JTAG_CLOCK_AUTO_ADJUST_PARAM))
    {
        if (param_value)
        {
            *param_value = 0; // No target feedback for automatic clock adjustment.
        }
        return FT_OK;
    }

    return JTAG_UNSUPPORTED_PARAMETER;
}

uint32_t mpsse_set_param(JTAGClientData *mpsse_blaster, char *param_name, uint32_t param_value)
{
    if (!mpsse_blaster || !param_name)
    {
        return JTAG_UNSUPPORTED_PARAMETER;
    }

    if (!strcmp(param_name, JTAG_CLOCK_AUTO_ADJUST_PARAM))
    {
        if (param_value > 1)
        {
            return JTAG_UNSUPPORTED_PARAMETER;
        }
        return param_value ? JTAG_UNIMPLEMENTED : FT_OK;
    }

    if (strcmp(param_name, JTAG_CLOCK_PARAM) != 0)
    {
        return JTAG_UNSUPPORTED_PARAMETER;
    }

    unsigned short clock_divisor;
    DWORD actual_clock;
    FT_STATUS status =
        calculate_clock_divider(mpsse_blaster, param_value, &clock_divisor, &actual_clock);
    if (status != FT_OK)
    {
        // Quartus parameter errors use AJI status 0x2D, not D2XX status 6.
        return JTAG_UNSUPPORTED_PARAMETER;
    }

    // Finish queued clocks using the old divider before changing frequency.
    if (!mpsse_flush(mpsse_blaster, TRUE, 0))
    {
        return FT_IO_ERROR;
    }

    status = program_clock_divider(mpsse_blaster, clock_divisor);
    mpsse_blaster->transport_status = status;
    if (status == FT_OK)
    {
        // Keep this assignment explicit: mpsse_get_param must report the realizable value,
        // rather than the requested value when the divider cannot represent it exactly.
        mpsse_blaster->jtag_clock = actual_clock;
        if (mpsse_blaster->clock_setting)
        {
            mpsse_blaster->clock_setting->divisor = clock_divisor;
        }
    }
    return status;
}

uint32_t mpsse_get_param_block(JTAGClientData *mpsse_blaster, char *param_name,
                               unsigned char *buffer, uint32_t *size)
{
    if (!mpsse_blaster || !mpsse_blaster->device_handle || !param_name || !size ||
        (*size && !buffer))
    {
        return JTAG_UNSUPPORTED_PARAMETER;
    }
    if (strcmp(param_name, "SerialNumber") != 0)
    {
        return JTAG_UNSUPPORTED_PARAMETER;
    }
    char serial_number[16] = {0};
    char description[64] = {0};
    FT_DEVICE device_type;
    DWORD device_id;
    FT_STATUS status = FT_GetDeviceInfo(mpsse_blaster->device_handle, &device_type, &device_id,
                                        serial_number, description, NULL);
    if (status != FT_OK)
    {
        return status;
    }
    uint32_t length = 0;
    while (length < sizeof(serial_number) && serial_number[length])
    {
        length++;
    }
    // Match the Quartus USB-Blaster block parameter convention: the input
    // size is capacity, the output size is bytes copied, without a terminator.
    if (length > *size)
    {
        length = *size;
    }
    if (length)
    {
        memcpy(buffer, serial_number, length);
    }
    *size = length;
    return FT_OK;
}
