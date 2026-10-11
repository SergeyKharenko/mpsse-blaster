/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2024 tow3rs
 * Copyright (C) 2026 Sergey Kharenko
 */

#include "quartus_abi.h"

#include <inttypes.h>
#include <stdio.h>

typedef struct
{
    unsigned char data[8];
    uint32_t bit_count;
    int overflow;
} ScanResult;

static void store_tdo(void *instance, unsigned char *buffer, uint32_t size)
{
    ScanResult *result = instance;
    if (size > sizeof(result->data) * 8 - result->bit_count)
    {
        result->overflow = 1;
        return;
    }
    for (uint32_t i = 0; i < size; i++)
    {
        unsigned char bit = (buffer[i / 8] >> (i % 8)) & 1;
        result->data[result->bit_count / 8] |= bit << (result->bit_count % 8);
        result->bit_count++;
    }
}

/* Reset the TAP, then read the first device's default 32-bit IDCODE register. */
static int scan_idcode(const JtagClientOperations *ops, JTAGClientData *client, ScanResult *result)
{
    static const struct
    {
        int tms;
        int count;
    } steps[] = {
        {1, 5},  /* Test-Logic-Reset */
        {0, 1},  /* Run-Test/Idle */
        {1, 1},  /* Select-DR-Scan */
        {0, 1},  /* Capture-DR */
        {0, 1},  /* Shift-DR */
        {0, 31}, /* First 31 IDCODE bits */
        {1, 1},  /* Last bit, Exit1-DR */
        {1, 1},  /* Update-DR */
        {0, 1},  /* Run-Test/Idle */
    };
    int position = 0;
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
    {
        if (ops->ClockRaw(client, steps[i].tms, 1, steps[i].count, position) != 0)
        {
            return 0;
        }
        position += steps[i].count;
    }
    return ops->Flush(client, 1, position) && !result->overflow &&
           result->bit_count == (uint32_t)position;
}

static uint32_t read_idcode(const ScanResult *result)
{
    uint32_t idcode = 0;
    /* The first nine returned bits move the TAP from reset to Shift-DR. */
    for (unsigned int i = 0; i < 32; i++)
    {
        unsigned int position = 9 + i;
        uint32_t bit = (result->data[position / 8] >> (position % 8)) & 1;
        idcode |= bit << i;
    }
    return idcode;
}

int main(void)
{
    const JtagClientOperations *ops = get_supported_hardware(0);
    if (!ops)
    {
        puts("No devices");
        return 1;
    }

    JTAGServerOperations server_ops = {.Size = sizeof(server_ops), .StoreTDO = store_tdo};
    do
    {
        char port_name[80];
        unsigned int ports_found = 0;
        for (unsigned int index = 0; ops->ScanPorts(index, port_name, sizeof(port_name)); index++)
        {
            ScanResult result = {0};
            JTAGClientData *client = NULL;
            ports_found++;
            if (!ops->OpenHardware(&client, port_name, &server_ops, &result) || !client)
            {
                fprintf(stderr, "Unable to open port [%s]\n", port_name);
                continue;
            }
            int success = scan_idcode(ops, client, &result);
            ops->CloseHardware(client);
            if (success)
            {
                uint32_t idcode = read_idcode(&result);
                printf("Port [%s]: IDCODE %08" PRIX32 "\n", port_name, idcode);
                if (!(idcode & 1) || idcode == UINT32_MAX)
                {
                    fputs("Check the JTAG connection: no valid IDCODE was read.\n", stderr);
                }
            }
            else
            {
                fprintf(stderr, "JTAG scan failed on port [%s]\n", port_name);
            }
        }
        if (!ports_found)
        {
            puts("No ports available");
        }
        puts("Press Enter to scan again, or Ctrl+C to exit.");
    } while (getchar() != EOF);
    return 0;
}
