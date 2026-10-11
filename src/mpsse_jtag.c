/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2024 tow3rs
 * Copyright (C) 2026 Sergey Kharenko
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "mpsse_internal.h"

#include <errno.h>
#include <string.h>
#ifndef _WIN32
#include <time.h>
#else
#include <windows.h>
#endif

JtagBool mpsse_flush(JTAGClientData *mpsse_blaster, JtagBool store, int position)
{
    (void)store;
    (void)position;
    if (!mpsse_blaster || !mpsse_blaster->device_handle)
    {
        return FALSE;
    }

    unsigned int tdo_position = 0;
    FT_STATUS status = mpsse_blaster->transport_status;
    if (status != FT_OK)
    {
        return FALSE;
    }
    if (mpsse_blaster->bytes_to_send)
    {
        mpsse_blaster->ft_io_buffer[mpsse_blaster->bytes_to_send++] = FT_OPCODE_SEND_IMMEDIATE;
        status = mpsse_transfer(mpsse_blaster->device_handle, mpsse_blaster->ft_io_buffer,
                                mpsse_blaster->bytes_to_send, mpsse_blaster->ft_io_buffer,
                                mpsse_blaster->bytes_to_receive);
        if (status != FT_OK)
        {
            goto reset_pending_transfer;
        }

        for (unsigned int inbyte_position = 0; inbyte_position < mpsse_blaster->bytes_to_receive;
             inbyte_position++)
        {
            unsigned char bit_count = mpsse_blaster->tdo_bit_count_per_byte[inbyte_position];
            unsigned char aligned_byte =
                mpsse_blaster->ft_io_buffer[inbyte_position] >> (8 - bit_count);
            for (int in_pos = 0; in_pos < bit_count; in_pos++)
            {
                unsigned char tdo_single_bit = 1 & (aligned_byte >> in_pos);
                mpsse_blaster->tdo_bits[tdo_position / 8] |= tdo_single_bit << tdo_position % 8;
                tdo_position++;
            }
        }
    }

    if (mpsse_blaster->bits_to_send)
    {
        unsigned char buffer[4] = {FT_OPCODE_CLOCK_DATA_BITS, mpsse_blaster->bits_to_send - 1,
                                   mpsse_blaster->current_byte, FT_OPCODE_SEND_IMMEDIATE};

        status = mpsse_transfer(mpsse_blaster->device_handle, buffer, 4, buffer, 1);
        if (status != FT_OK)
        {
            goto reset_pending_transfer;
        }
        unsigned char tdo_partial_byte = buffer[0] >> (8 - mpsse_blaster->bits_to_send);

        for (unsigned char bit_count = 0; bit_count < mpsse_blaster->bits_to_send; bit_count++)
        {
            unsigned char tdo_single_bit = 1 & (tdo_partial_byte >> bit_count);
            mpsse_blaster->tdo_bits[tdo_position / 8] |= tdo_single_bit << (tdo_position % 8);
            tdo_position++;
        }
    }

    if (tdo_position)
    {
        if (mpsse_blaster->server_operations && mpsse_blaster->server_operations->StoreTDO)
        {
            mpsse_blaster->server_operations->StoreTDO(mpsse_blaster->server_instance,
                                                       mpsse_blaster->tdo_bits, tdo_position);
        }

        if (mpsse_blaster->server_operations && mpsse_blaster->server_operations->IndicateFlush)
        {
            mpsse_blaster->server_operations->IndicateFlush(mpsse_blaster->server_instance);
        }
    }

reset_pending_transfer:
    // A partially executed stream cannot be safely retried.  Keep the error
    // latched until mpsse_close_hardware/mpsse_open_hardware reinitializes the adapter.
    mpsse_blaster->transport_status = status;
    mpsse_blaster->bytes_to_receive = 0;
    memset(mpsse_blaster->tdo_bits, 0, MPSSE_BUFFER_SIZE);
    memset(mpsse_blaster->tdo_bit_count_per_byte, 0, MPSSE_BUFFER_SIZE);
    mpsse_blaster->bits_to_send = 0;
    mpsse_blaster->bytes_to_send = 0;
    mpsse_blaster->status = MPSSE_BUFFER_FIRST_BYTE;

    return status == FT_OK;
}

uint32_t mpsse_delay(JTAGClientData *mpsse_blaster, uint32_t microseconds)
{
    if (!mpsse_blaster || !mpsse_blaster->device_handle)
    {
        return FT_INVALID_PARAMETER;
    }

    // mpsse_delay is a barrier in the client ABI: queued MPSSE commands must reach
    // the adapter before sleeping.  Quartus expresses this value in
    // microseconds (the server-side operation is named delay_microseconds).
    if (!mpsse_flush(mpsse_blaster, TRUE, 0))
    {
        return FT_IO_ERROR;
    }

#ifdef _WIN32
    Sleep(microseconds / 1000U + (microseconds % 1000U != 0));
#else
    struct timespec request = {
        .tv_sec = (time_t)(microseconds / 1000000UL),
        .tv_nsec = (long)((microseconds % 1000000UL) * 1000UL),
    };
    while (nanosleep(&request, &request) != 0)
    {
        if (errno != EINTR)
        {
            return FT_IO_ERROR;
        }
    }
#endif
    return FT_OK;
}

uint32_t mpsse_clock_bits(JTAGClientData *mpsse_blaster, int tms, int tdi, int bit_count,
                          int position)
{
    if (!mpsse_blaster || !mpsse_blaster->device_handle || bit_count < 0)
    {
        return FT_INVALID_PARAMETER;
    }
    if (mpsse_blaster->transport_status != FT_OK)
    {
        return mpsse_blaster->transport_status;
    }
    tms = !!tms;
    tdi = !!tdi;
    for (int current_bit = 0; current_bit < bit_count; current_bit++)
    {
        if (mpsse_blaster->last_tms == tms)
        {
            if (mpsse_blaster->bits_to_send == 0)
            {
                mpsse_blaster->current_byte = 0;
            }

            mpsse_blaster->current_byte |= tdi << mpsse_blaster->bits_to_send;
            mpsse_blaster->bits_to_send++;

            if (mpsse_blaster->bits_to_send == 8)
            {
                if (mpsse_blaster->status == MPSSE_BUFFER_FIRST_BYTE)
                {
                    mpsse_blaster->ft_io_buffer[mpsse_blaster->bytes_to_send++] =
                        FT_OPCODE_CLOCK_DATA_BYTES;
                    mpsse_blaster->number_of_bytes_in_chunk =
                        &mpsse_blaster->ft_io_buffer[mpsse_blaster->bytes_to_send];
                    mpsse_blaster->bytes_to_send += 2;
                    mpsse_blaster->number_of_bytes_in_chunk[0] = 0;
                    mpsse_blaster->number_of_bytes_in_chunk[1] = 0;
                    mpsse_blaster->status = MPSSE_BUFFER_MULTIBYTE;
                }
                else
                {
                    // MPSSE lengths are little endian and may be unaligned.
                    if (++mpsse_blaster->number_of_bytes_in_chunk[0] == 0)
                    {
                        mpsse_blaster->number_of_bytes_in_chunk[1]++;
                    }
                }
                mpsse_blaster->ft_io_buffer[mpsse_blaster->bytes_to_send++] =
                    mpsse_blaster->current_byte;
                mpsse_blaster->tdo_bit_count_per_byte[mpsse_blaster->bytes_to_receive++] = 8;
                mpsse_blaster->bits_to_send = 0;
                mpsse_blaster->current_byte = 0;
            }
        }
        else
        {
            if (mpsse_blaster->bits_to_send)
            {
                mpsse_blaster->ft_io_buffer[mpsse_blaster->bytes_to_send++] =
                    FT_OPCODE_CLOCK_DATA_BITS;
                mpsse_blaster->ft_io_buffer[mpsse_blaster->bytes_to_send++] =
                    mpsse_blaster->bits_to_send - 1;
                mpsse_blaster->ft_io_buffer[mpsse_blaster->bytes_to_send++] =
                    mpsse_blaster->current_byte;
                mpsse_blaster->tdo_bit_count_per_byte[mpsse_blaster->bytes_to_receive++] =
                    mpsse_blaster->bits_to_send;
                mpsse_blaster->bits_to_send = 0;
            }

            mpsse_blaster->ft_io_buffer[mpsse_blaster->bytes_to_send++] = FT_OPCODE_CLOCK_TMS;
            mpsse_blaster->ft_io_buffer[mpsse_blaster->bytes_to_send++] = 0;
            mpsse_blaster->ft_io_buffer[mpsse_blaster->bytes_to_send++] =
                (unsigned char)((tdi ? 0x80 : 0) | (tms ? 0x3 : 0));
            mpsse_blaster->tdo_bit_count_per_byte[mpsse_blaster->bytes_to_receive++] = 1;
            mpsse_blaster->last_tms = tms;
            mpsse_blaster->status = MPSSE_BUFFER_FIRST_BYTE;
        }

        if (mpsse_blaster->bytes_to_send > MPSSE_BUFFER_SIZE - 8)
        {
            if (!mpsse_flush(mpsse_blaster, TRUE, position))
            {
                return mpsse_blaster->transport_status;
            }
        }
    }

    return 0;
}

uint32_t mpsse_clock_bytes(JTAGClientData *mpsse_blaster, int tms, unsigned char *tdi,
                           int bit_count, int position)
{
    if (!mpsse_blaster || !mpsse_blaster->device_handle || bit_count < 0 || (bit_count && !tdi))
    {
        return FT_INVALID_PARAMETER;
    }
    for (int bit_position = 0; bit_position < bit_count; bit_position++)
    {
        DWORD status = mpsse_clock_bits(mpsse_blaster, tms,
                                        (tdi[bit_position / 8] >> (bit_position % 8)) & 0x1, 1,
                                        position + bit_position);
        if (status != FT_OK)
        {
            return status;
        }
    }

    return 0;
}
