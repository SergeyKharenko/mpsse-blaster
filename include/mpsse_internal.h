/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2024 tow3rs
 * Copyright (C) 2026 Sergey Kharenko
 */

#ifndef MPSSE_BLASTER_INTERNAL_H
#define MPSSE_BLASTER_INTERNAL_H

#include "quartus_abi.h"
#include <ftd2xx.h>

#define JTAG_CLOCK_PARAM             "JtagClock"
#define JTAG_CLOCK_AUTO_ADJUST_PARAM "JtagClockAutoAdjust"
#define JTAG_UNSUPPORTED_PARAMETER   0x2D
#define JTAG_UNIMPLEMENTED           0x7E
#define JTAG_OPEN_FAILED             0x56

typedef struct MpsseClockSetting
{
    struct MpsseClockSetting *next;
    char serial_number[16];
    unsigned int device_index;
    BOOL is_high_speed;
    unsigned short divisor;
} MpsseClockSetting;

#define MPSSE_BUFFER_SIZE 65536

// Opcodes
#define FT_OPCODE_CLOCK_TMS                    0x6B
#define FT_OPCODE_SEND_IMMEDIATE               0x87
#define FT_OPCODE_INVALID_COMMAND              0xAA
#define FT_OPCODE_CLOCK_DATA_BITS              0x3B
#define FT_OPCODE_DISABLE_LOOPBACK             0x85
#define FT_OPCODE_CLOCK_DATA_BYTES             0x39
#define FT_OPCODE_SET_CLOCK_DIVIDER            0x86
#define FT_OPCODE_DISABLE_DIVIDE_BY_5          0x8A
#define FT_OPCODE_SET_DATA_BITS_LOW_BYTE       0x80
#define FT_OPCODE_SET_DATA_BITS_HIGH_BYTE      0x82
#define FT_OPCODE_DISABLE_ADAPTIVE_CLOCKING    0x97
#define FT_OPCODE_DISABLE_THREE_PHASE_CLOCKING 0x8D

typedef struct
{
    ULONG type;
    USHORT vid;
    USHORT pid;
} MpsseDeviceId;

typedef enum
{
    MPSSE_BUFFER_IDLE,
    MPSSE_BUFFER_FIRST_BYTE,
    MPSSE_BUFFER_MULTIBYTE,
} MpsseBufferState;

struct JTAGClientData
{
    JTAGServerOperations *server_operations;
    void *server_instance;
    BOOL is_high_speed;
    DWORD jtag_clock;
    unsigned short clock_divisor;
    struct MpsseClockSetting *clock_setting;
    FT_STATUS transport_status;
    MpsseBufferState status;
    FT_HANDLE device_handle;
    int last_tms;

    unsigned int bytes_to_send;
    unsigned char bits_to_send;
    unsigned char current_byte;
    unsigned int bytes_to_receive;
    unsigned char *number_of_bytes_in_chunk;

    unsigned char ft_io_buffer[MPSSE_BUFFER_SIZE];
    unsigned char tdo_bits[MPSSE_BUFFER_SIZE];
    unsigned char tdo_bit_count_per_byte[MPSSE_BUFFER_SIZE];
};

FT_STATUS mpsse_transfer(FT_HANDLE handle, unsigned char *out_buffer, unsigned int out_size,
                         unsigned char *in_buffer, unsigned int in_size);
FT_STATUS mpsse_synchronize(FT_HANDLE handle);
uint32_t mpsse_clock_base_hz(const JTAGClientData *client);
JTAGClientData *mpsse_device_open(unsigned int index, JTAGServerOperations *server_ops,
                                  void *server_instance);
JtagBool mpsse_scan_ports(unsigned int index, char *port_name, int size);
JtagBool mpsse_has_supported_device(void);
uint32_t mpsse_open_hardware(JTAGClientData **client, char *device_name,
                             JTAGServerOperations *server_ops, void *server_instance);
uint32_t mpsse_init_driver(JTAGClientData **client, char *device_name,
                           JTAGServerOperations *server_ops, void *server_instance);
uint32_t mpsse_close_hardware(JTAGClientData *client);
JtagBool mpsse_flush(JTAGClientData *client, JtagBool store, int position);
uint32_t mpsse_delay(JTAGClientData *client, uint32_t microseconds);
uint32_t mpsse_clock_bits(JTAGClientData *client, int tms, int tdi, int bit_count, int position);
uint32_t mpsse_clock_bytes(JTAGClientData *client, int tms, unsigned char *tdi, int bit_count,
                           int position);
uint32_t mpsse_get_param(JTAGClientData *client, char *name, uint32_t *value);
uint32_t mpsse_set_param(JTAGClientData *client, char *name, uint32_t value);
uint32_t mpsse_get_param_block(JTAGClientData *client, char *name, unsigned char *buffer,
                               uint32_t *size);

#endif /* MPSSE_BLASTER_INTERNAL_H */
