/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2024 tow3rs
 * Copyright (C) 2026 Sergey Kharenko
 */

#include "mpsse_internal.h"

FT_STATUS mpsse_synchronize(FT_HANDLE device)
{
    FT_STATUS status = FT_Purge(device, FT_PURGE_RX | FT_PURGE_TX);
    if (status != FT_OK)
    {
        return status;
    }

    // Each invalid command must return its own FA xx reply before JTAG data is read.
    for (unsigned int step = 0; step < 2; step++)
    {
        unsigned char command[2] = {(unsigned char)(0xAA + step), FT_OPCODE_SEND_IMMEDIATE};
        unsigned char reply[2] = {0};
        DWORD transferred = 0;
        status = FT_Write(device, command, sizeof(command), &transferred);
        if (status != FT_OK)
        {
            return status;
        }
        if (transferred != sizeof(command))
        {
            return FT_IO_ERROR;
        }
        status = FT_Read(device, reply, sizeof(reply), &transferred);
        if (status != FT_OK)
        {
            return status;
        }
        if (transferred != sizeof(reply) || reply[0] != 0xFA || reply[1] != command[0])
        {
            return FT_IO_ERROR;
        }
    }
    return FT_OK;
}

FT_STATUS mpsse_transfer(FT_HANDLE ftdih, unsigned char *out_buffer, unsigned int out_size,
                         unsigned char *in_buffer, unsigned int in_size)
{
    DWORD temp_count = 0;
    FT_STATUS status = FT_OK;
    unsigned int total_progress;
    unsigned char *buffer_pointer;
    unsigned int partial_count;

    if (!ftdih || (out_size && !out_buffer) || (in_size && !in_buffer))
    {
        return FT_INVALID_PARAMETER;
    }

    if (out_size)
    {
        total_progress = 0;
        buffer_pointer = out_buffer;
        partial_count = out_size;
        while (total_progress < out_size)
        {
            temp_count = 0;
            status = FT_Write(ftdih, buffer_pointer, partial_count, &temp_count);
            if (status != FT_OK)
            {
                return status;
            }
            // D2XX may report FT_OK with a short write.  Zero progress would
            // otherwise turn this loop into an infinite loop.
            if (temp_count == 0 || temp_count > partial_count)
            {
                return FT_IO_ERROR;
            }
            buffer_pointer += temp_count;
            partial_count -= temp_count;
            total_progress += temp_count;
        }
    }

    if (in_size)
    {
        total_progress = 0;
        buffer_pointer = in_buffer;
        partial_count = in_size;
        while (total_progress < in_size)
        {
            temp_count = 0;
            // FT_Read waits up to the timeout configured by mpsse_device_open.  Do
            // not spin on FT_GetQueueStatus: a disconnected adapter or missing
            // reply must eventually report failure.
            status = FT_Read(ftdih, buffer_pointer, partial_count, &temp_count);
            if (status != FT_OK)
            {
                return status;
            }
            if (temp_count == 0 || temp_count > partial_count)
            {
                return FT_IO_ERROR;
            }
            buffer_pointer += temp_count;
            partial_count -= temp_count;
            total_progress += temp_count;
        }
    }

    return FT_OK;
}
