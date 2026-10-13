/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Sergey Kharenko
 */

#include "mpsse_internal.h"

#include <string.h>

// Bound chain length and latency, allowing 32 terminating ones after 128 IDCODEs.
#define AUTOTUNE_MAX_DEVICES 128U
#define AUTOTUNE_SCAN_BYTES  (AUTOTUNE_MAX_DEVICES * 4U + 4U)
#define AUTOTUNE_REPETITIONS 5U

static const unsigned char reset_commands[] = {
    FT_OPCODE_CLOCK_TMS_NO_READ,
    4,
    0x1F,
    FT_OPCODE_SEND_IMMEDIATE,
};

static FT_STATUS read_reset_chain(JTAGClientData *client, unsigned char *signature)
{
    static const unsigned char scan_commands[] = {
        FT_OPCODE_CLOCK_TMS_NO_READ,
        4,
        0x1F, // Reset
        FT_OPCODE_CLOCK_TMS_NO_READ,
        3,
        0x02, // TMS 0,1,0,0: enter Shift-DR
        FT_OPCODE_CLOCK_DATA_BYTES,
        (unsigned char)((AUTOTUNE_SCAN_BYTES - 1U) & 0xFF),
        (unsigned char)((AUTOTUNE_SCAN_BYTES - 1U) >> 8),
    };
    unsigned char commands[sizeof(scan_commands) + AUTOTUNE_SCAN_BYTES + sizeof(reset_commands)];
    memcpy(commands, scan_commands, sizeof(scan_commands));
    memset(commands + sizeof(scan_commands), 0xFF, AUTOTUNE_SCAN_BYTES);
    memcpy(commands + sizeof(scan_commands) + AUTOTUNE_SCAN_BYTES, reset_commands,
           sizeof(reset_commands));

    // Private probes return to reset and never publish their TDO to the caller.
    return mpsse_transfer(client->device_handle, commands, sizeof(commands), signature,
                          AUTOTUNE_SCAN_BYTES);
}

static unsigned int signature_bit(const unsigned char *signature, unsigned int bit)
{
    return (signature[bit / 8U] >> (bit % 8U)) & 1U;
}

static BOOL valid_signature(const unsigned char *signature)
{
    unsigned int position = 0;
    for (unsigned int devices = 0; devices <= AUTOTUNE_MAX_DEVICES; devices++)
    {
        uint32_t idcode = 0;
        for (unsigned int bit = 0; bit < 32U; bit++)
        {
            idcode |= (uint32_t)signature_bit(signature, position + bit) << bit;
        }
        if (idcode == UINT32_MAX)
        {
            // A disconnected/stuck-high TDO must not qualify as a fast chain.
            if (!devices)
            {
                return FALSE;
            }
            for (; position < AUTOTUNE_SCAN_BYTES * 8U; position++)
            {
                if (!signature_bit(signature, position))
                {
                    return FALSE;
                }
            }
            return TRUE;
        }
        if (devices == AUTOTUNE_MAX_DEVICES)
        {
            return FALSE;
        }
        if (idcode & 1U)
        {
            unsigned int manufacturer = (idcode >> 1) & 0x7FFU;
            if (!manufacturer || manufacturer == 0x7FFU)
            {
                return FALSE;
            }
            position += 32U;
        }
        else
        {
            position++; // A device without IDCODE resets to its one-bit BYPASS register.
        }
    }
    return FALSE;
}

static FT_STATUS matches_reference(JTAGClientData *client, const unsigned char *reference,
                                   BOOL *matches)
{
    unsigned char signature[AUTOTUNE_SCAN_BYTES];
    *matches = FALSE;
    for (unsigned int repeat = 0; repeat < AUTOTUNE_REPETITIONS; repeat++)
    {
        FT_STATUS status = read_reset_chain(client, signature);
        if (status != FT_OK)
        {
            return status;
        }
        if (memcmp(reference, signature, sizeof(signature)) != 0)
        {
            return FT_OK;
        }
    }
    *matches = TRUE;
    return FT_OK;
}

static FT_STATUS select_frequency(JTAGClientData *client, uint32_t frequency)
{
    unsigned short divisor;
    FT_STATUS status = mpsse_calculate_clock_divider(client, frequency, &divisor);
    return status == FT_OK ? mpsse_program_clock_divider(client, divisor) : status;
}

FT_STATUS mpsse_auto_adjust_clock(JTAGClientData *client)
{
    static const uint32_t reference_rates[] = {1000000U, 100000U, 10000U};
    static const uint32_t candidate_rates[] = {
        30000000U, 15000000U, 10000000U, 7500000U, 6000000U, 3000000U, 1000000U, 500000U, 100000U,
    };
    unsigned char reference[AUTOTUNE_SCAN_BYTES];
    unsigned short original_divisor = client->clock_divisor;
    uint32_t reference_rate = 0;

    // Called only at a caller-requested TAP reset, after its queue is flushed.
    for (unsigned int rate = 0; rate < sizeof(reference_rates) / sizeof(reference_rates[0]); rate++)
    {
        FT_STATUS status = select_frequency(client, reference_rates[rate]);
        if (status != FT_OK)
        {
            return status;
        }
        status = read_reset_chain(client, reference);
        if (status != FT_OK)
        {
            return status;
        }
        if (!valid_signature(reference))
        {
            continue;
        }
        BOOL matches;
        status = matches_reference(client, reference, &matches);
        if (status != FT_OK)
        {
            return status;
        }
        if (matches)
        {
            reference_rate = client->jtag_clock;
            break;
        }
    }

    if (!reference_rate)
    {
        // Leave an absent/unsupported chain for the caller's normal scan to diagnose.
        return mpsse_program_clock_divider(client, original_divisor);
    }

    unsigned short selected_divisor = client->clock_divisor;
    uint32_t maximum_rate = mpsse_clock_base_hz(client) / 2U;
    for (unsigned int rate = 0; rate < sizeof(candidate_rates) / sizeof(candidate_rates[0]); rate++)
    {
        if (candidate_rates[rate] > maximum_rate || candidate_rates[rate] <= reference_rate)
        {
            continue;
        }
        FT_STATUS status = select_frequency(client, candidate_rates[rate]);
        if (status != FT_OK)
        {
            return status;
        }
        BOOL matches;
        status = matches_reference(client, reference, &matches);
        if (status != FT_OK)
        {
            return status;
        }
        if (matches)
        {
            selected_divisor = client->clock_divisor;
            break;
        }
    }

    // A rejected high-speed probe may have lost its final TMS transitions.
    // Re-establish reset at the verified reference rate before returning.
    FT_STATUS status = select_frequency(client, reference_rate);
    if (status != FT_OK)
    {
        return status;
    }
    status = mpsse_transfer(client->device_handle, (unsigned char *)reset_commands,
                            sizeof(reset_commands), NULL, 0);
    if (status != FT_OK)
    {
        return status;
    }
    status = mpsse_program_clock_divider(client, selected_divisor);
    if (status == FT_OK && client->clock_setting)
    {
        client->clock_setting->divisor = selected_divisor;
    }
    return status;
}
