/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2024 tow3rs
 * Copyright (C) 2026 Sergey Kharenko
 */

#ifndef JTAG_MPSSE_BLASTER_H
#define JTAG_MPSSE_BLASTER_H

#include <ftd2xx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 65536

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
    ULONG Type;
    USHORT VID;
    USHORT PID;
} Device;

typedef enum
{
    Iddle,
    FirstByte,
    MultiByte,
} StateMachine;

typedef struct
{
    ULONG Size;
    void (*StoreTDO)(void *, unsigned char *, unsigned long);
    void (*StoreUnknownTDO)(void *, unsigned long);
    void (*StoreNeededTDO)(void *, unsigned char *, unsigned long);
    int (*TDONeeded)(void *, int);
    void (*IndicateFlush)(void *);
} JTAGServerOperations;

typedef struct
{
    JTAGServerOperations *serverOperations;
    void *serverInstance;
    BOOL isHighSpeed;
    DWORD jtagClock;
    StateMachine status;
    FT_HANDLE deviceHandle;
    int lastTMS;

    unsigned int bytesToSend;
    unsigned char bitsToSend;
    unsigned char currentByte;
    unsigned int bytesToReceive;
    unsigned short *numberOfBytesInChunk;

    unsigned char ftIOBuffer[BUFFER_SIZE];
    unsigned char tdoBits[BUFFER_SIZE];
    unsigned char tdoBitCountPerByte[BUFFER_SIZE];

} JTAGClientData;

typedef struct
{
    ULONG Size;
    UCHAR Name[32];
    DWORD Features;
    unsigned long long Flags;
    void *Reserved1;
    BOOL (*ScanPorts)(unsigned int, char *portName, int);
    void *Reserved2;
    DWORD (*OpenHardware)(JTAGClientData **mpsseBlaster, char *deviceName,
                          JTAGServerOperations *serverOperations, void *serverInstance);
    DWORD (*CloseHardware)(JTAGClientData *mpsseBlaster);
    DWORD (*SetParam)(JTAGClientData *mpsseBlaster, char *param_name, unsigned long val);
    DWORD (*GetParam)(JTAGClientData *mpsseBlaster, char *param_name, unsigned long *retval);
    DWORD (*DirectControl)(JTAGClientData *mpsseBlaster, unsigned long, unsigned long *);
    DWORD (*ClockRaw)(JTAGClientData *mpsseBlaster, int, int, int, int);
    DWORD (*ClockMultiple)(JTAGClientData *mpsseBlaster, int, unsigned char *, int, int);
    DWORD (*Delay)(JTAGClientData *mpsseBlaster, unsigned long);
    BOOL (*Flush)(JTAGClientData *mpsseBlaster, BOOL store, int size);
    DWORD (*EnableRecirculate)(JTAGClientData *mpsseBlaster, unsigned long);
    DWORD (*DisableRecirculate)(JTAGClientData *mpsseBlaster);
    DWORD (*Recirculate)(JTAGClientData *mpsseBlaster, unsigned long, int, int);
    DWORD (*SetParamBlock)(JTAGClientData *mpsseBlaster, char *name, unsigned char *buffer,
                           unsigned long size);
    DWORD (*GetParamBlock)(JTAGClientData *mpsseBlaster, char *name, unsigned char *buffer,
                           unsigned long *size);
    DWORD (*InitDriver)(JTAGClientData **mpsseBlaster, char *deviceName,
                        JTAGServerOperations *serverOperations, void *serverInstance);
    DWORD (*EnableActiveSerial)(BOOL enable);
    DWORD (*RemoveDriver)(char *);
} JtagClientOperations;

#endif
