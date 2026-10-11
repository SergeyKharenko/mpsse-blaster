/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2024 tow3rs
 * Copyright (C) 2026 Sergey Kharenko
 */

#include "mpsse_internal.h"

#include <stdlib.h>
#include <string.h>

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>

static const MpsseDeviceId valid_devices[] = {
    {.type = FT_DEVICE_232H, .vid = 0x0403, .pid = 0x6014} // Single RS232-HS - (FT232H)
};

// jtagd closes a cable between clients. Preserve its selected divider for
// subsequent opens in this server process, keyed by FTDI serial number.
static MpsseClockSetting *clock_settings;

static void clear_clock_settings(void)
{
    while (clock_settings)
    {
        MpsseClockSetting *next = clock_settings->next;
        free(clock_settings);
        clock_settings = next;
    }
}

#ifndef _WIN32
__attribute__((destructor)) static void finalize_clock_settings(void)
{
    clear_clock_settings();
}
#endif

static MpsseClockSetting *find_clock_setting(unsigned int device_index, const char *serial_number,
                                             BOOL is_high_speed)
{
    for (MpsseClockSetting *setting = clock_settings; setting; setting = setting->next)
    {
        if (setting->is_high_speed == is_high_speed &&
            strcmp(setting->serial_number, serial_number) == 0 &&
            (serial_number[0] || setting->device_index == device_index))
        {
            return setting;
        }
    }
    MpsseClockSetting *setting = calloc(1, sizeof(*setting));
    if (setting)
    {
        memcpy(setting->serial_number, serial_number, sizeof(setting->serial_number));
        setting->device_index = device_index;
        setting->is_high_speed = is_high_speed;
        setting->divisor = is_high_speed ? 2 : 0;
        setting->next = clock_settings;
        clock_settings = setting;
    }
    return setting;
}

#ifdef _WIN32
static BOOL ftdi_initialized = FALSE;

static void finalize_d2xx(void)
{
    clear_clock_settings();
    if (ftdi_initialized)
    {
        FT_Finalise();
        ftdi_initialized = FALSE;
    }
}
#endif

static FT_STATUS initialize_d2xx(void)
{
#ifdef _WIN32
    if (!ftdi_initialized)
    {
        FT_STATUS status = FT_Initialise();
        if (status != FT_OK)
        {
            return status;
        }
        if (atexit(finalize_d2xx) != 0)
        {
            FT_Finalise();
            return FT_INSUFFICIENT_RESOURCES;
        }
        ftdi_initialized = TRUE;
    }
#endif
    return FT_OK;
}

static BOOL is_supported_device(DWORD type, DWORD id)
{
    for (size_t i = 0; i < sizeof(valid_devices) / sizeof(valid_devices[0]); i++)
    {
        if (type == valid_devices[i].type && (id >> 16) == valid_devices[i].vid &&
            (id & 0xFFFF) == valid_devices[i].pid)
        {
            return TRUE;
        }
    }
    return FALSE;
}

/* D2XX statuses are enum values, not flags. Stop at the first failed call. */
static FT_STATUS configure_device(FT_HANDLE handle)
{
    FT_STATUS status = FT_ResetDevice(handle);
    if (status != FT_OK)
    {
        return status;
    }
    status = FT_SetUSBParameters(handle, MPSSE_BUFFER_SIZE, MPSSE_BUFFER_SIZE - 1);
    if (status != FT_OK)
    {
        return status;
    }
    status = FT_SetChars(handle, 0, 0, 0, 0);
    if (status != FT_OK)
    {
        return status;
    }
    status = FT_SetTimeouts(handle, 200, 200);
    if (status != FT_OK)
    {
        return status;
    }
    status = FT_SetLatencyTimer(handle, 10);
    if (status != FT_OK)
    {
        return status;
    }
    status = FT_SetFlowControl(handle, FT_FLOW_RTS_CTS, 0, 0);
    if (status != FT_OK)
    {
        return status;
    }
    status = FT_SetBitMode(handle, 0, FT_BITMODE_RESET);
    if (status != FT_OK)
    {
        return status;
    }
    status = FT_SetBitMode(handle, 0, FT_BITMODE_MPSSE);
    if (status != FT_OK)
    {
        return status;
    }
    return mpsse_synchronize(handle);
}

JTAGClientData *mpsse_device_open(unsigned int dev_num, JTAGServerOperations *server_operations,
                                  void *server_instance)
{
    FT_STATUS status;
    unsigned char buffer[16];
    DWORD dw_num_bytes_to_send;
    int clock_divisor;
    DWORD num_devices;
    DWORD dw_flags, device_type, device_id;
    char serial_number[16] = {0};

    status = initialize_d2xx();

    if (status != FT_OK)
    {
        return NULL;
    }

    status = FT_CreateDeviceInfoList(&num_devices);
    if (status != FT_OK || dev_num >= num_devices || dev_num > INT_MAX)
    {
        return NULL;
    }

    status = FT_GetDeviceInfoDetail(dev_num, &dw_flags, &device_type, &device_id, NULL,
                                    serial_number, NULL, NULL);
    serial_number[sizeof(serial_number) - 1] = '\0';

    if ((status != FT_OK) || (dw_flags & FT_FLAGS_OPENED) ||
        !is_supported_device(device_type, device_id))
    {
        return NULL;
    }

    JTAGClientData *hw_data = calloc(1, sizeof(*hw_data));

    if (!hw_data)
    {
        return NULL;
    }

    hw_data->server_operations = server_operations;
    hw_data->server_instance = server_instance;
    hw_data->last_tms = -1;
    hw_data->status = MPSSE_BUFFER_IDLE;
    hw_data->is_high_speed = dw_flags & FT_FLAGS_HISPEED;
    hw_data->clock_setting = find_clock_setting(dev_num, serial_number, hw_data->is_high_speed);
    if (!hw_data->clock_setting)
    {
        free(hw_data);
        return NULL;
    }

    status = FT_Open((int)dev_num, &hw_data->device_handle);
    if (status != FT_OK)
    {
        goto fail;
    }
    status = configure_device(hw_data->device_handle);
    if (status != FT_OK)
    {
        goto fail;
    }

    // Set initial states of the MPSSE interface - low byte, both pin directions and output values
    // Pin    Signal    Direction
    // ADBUS0 TCK       output 1
    // ADBUS1 TDI       output 1
    // ADBUS2 TDO       input  0
    // ADBUS3 TMS       output 1
    // ADBUS4 GPIOL0    input 0
    // ADBUS5 GPIOL1    input 0
    // ADBUS6 GPIOL2    input 0
    // ADBUS7 GPIOL3    input 0

    dw_num_bytes_to_send = 0;
    buffer[dw_num_bytes_to_send++] = FT_OPCODE_SET_DATA_BITS_LOW_BYTE;
    buffer[dw_num_bytes_to_send++] = 0x08; // Initial state
    buffer[dw_num_bytes_to_send++] = 0x0B; // Direction

    // Set initial states of the MPSSE interface - high byte, both pin directions and output values
    // Pin    Signal     Direction
    // ACBUS0 GPIOH0     input 0
    // ACBUS1 GPIOH1     input 0
    // ACBUS2 GPIOH2     input 0
    // ACBUS3 GPIOH3     input 0
    // ACBUS4 GPIOH4     input 0
    // ACBUS5 GPIOH5     input 0
    // ACBUS6 GPIOH6     input 0
    // ACBUS7 GPIOH7     input 0

    buffer[dw_num_bytes_to_send++] = FT_OPCODE_SET_DATA_BITS_HIGH_BYTE;
    buffer[dw_num_bytes_to_send++] = 0; // Initial state
    buffer[dw_num_bytes_to_send++] = 0; // Direction
    status = mpsse_transfer(hw_data->device_handle, buffer, dw_num_bytes_to_send, NULL, 0);
    if (status != FT_OK)
    {
        goto fail;
    }

    dw_num_bytes_to_send = 0;
    buffer[dw_num_bytes_to_send++] = FT_OPCODE_DISABLE_LOOPBACK;

    if (hw_data->is_high_speed)
    {
        buffer[dw_num_bytes_to_send++] =
            FT_OPCODE_DISABLE_DIVIDE_BY_5; // Use 60MHz master clock (disable divide by 5)
    }
    clock_divisor = hw_data->clock_setting->divisor;
    hw_data->clock_divisor = (unsigned short)clock_divisor;
    hw_data->jtag_clock = mpsse_clock_base_hz(hw_data) / (2 * (clock_divisor + 1));
    buffer[dw_num_bytes_to_send++] = FT_OPCODE_DISABLE_ADAPTIVE_CLOCKING;
    buffer[dw_num_bytes_to_send++] = FT_OPCODE_DISABLE_THREE_PHASE_CLOCKING;
    buffer[dw_num_bytes_to_send++] = FT_OPCODE_SET_CLOCK_DIVIDER;
    buffer[dw_num_bytes_to_send++] = clock_divisor & 0xFF;        //Set 0xValueL of clock divisor
    buffer[dw_num_bytes_to_send++] = (clock_divisor >> 8) & 0xFF; //Set 0xValueH of clock divisor

    status = mpsse_transfer(hw_data->device_handle, buffer, dw_num_bytes_to_send, NULL, 0);

    if (status != FT_OK)
    {
        goto fail;
    }
    return hw_data;

fail:
    if (hw_data->device_handle)
    {
        FT_Close(hw_data->device_handle);
    }
    free(hw_data);
    return NULL;
}

JtagBool mpsse_scan_ports(unsigned int index, char *port_name, int size)
{
    if (!port_name || size <= 0 || initialize_d2xx() != FT_OK)
    {
        return FALSE;
    }
    port_name[0] = '\0';
    DWORD num_devices = 0;
    if (FT_CreateDeviceInfoList(&num_devices) != FT_OK || index >= num_devices)
    {
        return FALSE;
    }
    unsigned int matched = 0;
    for (DWORD device_index = 0; device_index < num_devices; device_index++)
    {
        DWORD type = 0, id = 0;
        char description[64] = {0};
        if (FT_GetDeviceInfoDetail(device_index, NULL, &type, &id, NULL, NULL, description, NULL) !=
                FT_OK ||
            !is_supported_device(type, id))
        {
            continue;
        }
        description[sizeof(description) - 1] = '\0';
        if (matched++ == index)
        {
            /* The ordinal selects the match; the name carries the real D2XX index. */
            int length = snprintf(port_name, (size_t)size, "%02u %s", (unsigned int)device_index,
                                  description);
            if (length < 0 || length >= size)
            {
                port_name[0] = '\0';
                return FALSE;
            }
            return TRUE;
        }
    }
    return FALSE;
}

uint32_t mpsse_open_hardware(JTAGClientData **client, char *device_name,
                             JTAGServerOperations *server_operations, void *server_instance)
{
    if (!client)
    {
        return FALSE;
    }
    *client = NULL;
    if (!device_name || !isdigit((unsigned char)device_name[0]))
    {
        return FALSE;
    }
    char *end;
    errno = 0;
    unsigned long device_index = strtoul(device_name, &end, 10);
    if (errno == ERANGE || device_index > INT_MAX || (*end && !isspace((unsigned char)*end)))
    {
        return FALSE;
    }
    *client = mpsse_device_open((unsigned int)device_index, server_operations, server_instance);
    return *client != NULL;
}

uint32_t mpsse_init_driver(JTAGClientData **mpsse_blaster, char *device_name,
                           JTAGServerOperations *server_operations, void *server_instance)
{
    // Unlike the legacy mpsse_open_hardware boolean, mpsse_init_driver returns zero on
    // success.  jtagd uses 0x56 when its legacy open path fails.
    return mpsse_open_hardware(mpsse_blaster, device_name, server_operations, server_instance)
               ? FT_OK
               : JTAG_OPEN_FAILED;
}

uint32_t mpsse_close_hardware(JTAGClientData *mpsse_blaster)
{
    if (mpsse_blaster)
    {
        if (mpsse_blaster->device_handle)
        {
            FT_Close(mpsse_blaster->device_handle);
        }
        free(mpsse_blaster);
    }
    return 0;
}

JtagBool mpsse_has_supported_device(void)
{
    DWORD dw_id;
    DWORD dw_type;
    DWORD num_devices = 0;

    FT_STATUS ft_status = initialize_d2xx();

    if (ft_status != FT_OK)
    {
        return FALSE;
    }

    ft_status = FT_CreateDeviceInfoList(&num_devices);
    if (ft_status != FT_OK || !num_devices)
    {
        return FALSE;
    }

    for (DWORD device_index = 0; device_index < num_devices; device_index++)
    {
        ft_status =
            FT_GetDeviceInfoDetail(device_index, NULL, &dw_type, &dw_id, NULL, NULL, NULL, NULL);
        if (ft_status != FT_OK)
        {
            continue;
        }

        if (is_supported_device(dw_type, dw_id))
        {
            return TRUE;
        }
    }

    return FALSE;
}
