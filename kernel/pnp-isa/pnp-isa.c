/* Code for Plug and Play support (experimental)
 *
 * The specification has been obtained here:
 *
 * http://download.microsoft.com/download/1/6/1
 * /161ba512-40e2-4cc9-843a-923143f3456c/PNPISA.rtf
 *
 */

#include "kernel.h"

#include "proto.h"  /* milli_delay (int) */
#include <string.h>

#include "pnp-isa.h"

pnp_isa_t pnp_table [PNP_TABLE_SIZE];

/************************************************************************
 *                         pnp_isa_init_key                             *
 *                                                                      *
 * Send initialization sequence to the ADDRESS_PORT                     *
 * See "Initiation Key" section in the specification manual             *
 ************************************************************************/
void pnp_isa_init_key ()
{
    /* The table for linear feedback shift register algorithm (LFSR),
     * initial value: 0x6a */
    u8_t initiation_key [PNP_INITIATION_KEY_LEN] = {
        0x6a, 0xb5, 0xdA, 0xed, 0xf6, 0xfb, 0x7d, 0xbe,
        0xdf, 0x6f, 0x37, 0x1b, 0x0d, 0x86, 0xc3, 0x61, 
        0xb0, 0x58, 0x2c, 0x16, 0x8b, 0x45, 0xa2, 0xd1, 
        0xe8, 0x74, 0x3a, 0x9d, 0xce, 0xe7, 0x73, 0x39 };
    int i;
    /* Reset all PnP cards */
    out_byte (PNP_ADDRESS_PORT, 0x00);
    out_byte (PNP_ADDRESS_PORT, 0x00);
    /* Send Initiation Key to set all PnP cards into the Config state */
    for (i = 0; i < PNP_INITIATION_KEY_LEN; i++)
    {
        out_byte (PNP_ADDRESS_PORT, initiation_key[i]);
    }
    /* Now each card expects 72 pairs of I/O access to the READ_DATA */
}

/************************************************************************
 *                         pnp_isa_isolate_card                         *
 * pnp_read_port: read port, must be in range PNP_READ_DATA_LO:_HI      *
 * pnp_card_id:   array of PNP_ID_LEN size (last byte is checksum)      *
 ************************************************************************/
int pnp_isa_isolate_card (u16_t pnp_read_port, u8_t * pnp_card_id)
{
    u8_t read_res_1, read_res_2;
    /* Checksum is determined by the LFSR algorithm */
    u8_t lfsr = 0x6a;
    int card_detected = 0;
    int i;
    u8_t bit;
    /* must be delay (1*TIME_MSEC); */
    milli_delay (1);
    for (i = 0; i < PNP_ISOLATE_SEQ_LEN; i++)
    {
        /* Read two bytes from the port we suppose to isolate */
        read_res_1 = read_res_2 = 0;
        read_res_1 = in_byte (pnp_read_port);
        /* Must be delay (250*TIME_uSEC); */
        milli_delay (1);
        read_res_2 = in_byte (pnp_read_port);
        /* Must be delay (250*TIME_uSEC); */
        milli_delay (1);
        /* Check if magic happens */
        if ((read_res_1 == PNP_ISOLATE_MAGIC_1) &&
            (read_res_2 == PNP_ISOLATE_MAGIC_2))
        {
            /* set ith bit to 1 */
            pnp_card_id [i/8] |= (1<<(i%8))&0xff;
            /* At least one magic happens means at least one card present */
            card_detected = 1;
            /* Bit for checksum */
            bit = 1;
        }
        else
        {
            /* set ith bit to 0 */
            pnp_card_id [i/8] &= (~(1<<(i%8)))&0xff;
            bit = 0;
        }
        if (i < (PNP_ID_LEN-1)*8) /* i < 64 */
            lfsr = ((bit ^ (lfsr&0x01) ^ ((lfsr>>1)&0x01))<<7) + (lfsr>>1);
    }
    if (card_detected)
        if (lfsr == pnp_card_id[PNP_ID_LEN-1]) /* checksum */
            return PNP_PORT_ASSIGNED;
        else
            return PNP_CHECKSUM_NOT_MATCH;
    else
        return PNP_CARD_NOT_DETECTED;
}

/************************************************************************
 *                         pnp_isa_fill_table                           *
 ************************************************************************/
void pnp_isa_fill_table ()
{
    u8_t id_buffer [PNP_ID_LEN];
    int port, i;
    for (port = PNP_READ_DATA_LO, i = 0; port <= PNP_READ_DATA_HI;
        port += PNP_READ_DATA_STEP, i++)
    {
        int res = PNP_CARD_NOT_DETECTED;
        res = pnp_isa_isolate_card (port, id_buffer);
        if (res == 0)
        {
            pnp_table[i].port_read_data = port;
            strncpy (pnp_table[i].vendor_ID, id_buffer, PNP_VENDOR_ID_LEN);
            strncpy (pnp_table[i].serial_number, id_buffer+PNP_VENDOR_ID_LEN,
                PNP_SN_LEN);
            pnp_table[i].checksum = id_buffer [PNP_ID_LEN-1];
        }
        else
        {
            pnp_table[i].port_read_data = 0;
        }
    }
}

/************************************************************************
 *                         pnp_isa_task                                 *
 * Main driver's function, which distributes jobs around different      *
 * subroutines.                                                         *
 ************************************************************************/
PUBLIC void pnp_isa_task ()
{
    message pnp_isa_message;

    printf ("Plug and Pray^W Play ISA task (experimental)\n");
    /* Initialization step */
    /* 1. Send Initialization key */
    pnp_isa_init_key ();
    /* 2. Fill table of ISA devices */
    pnp_isa_fill_table ();
    while (TRUE)
    {
        receive (ANY, &pnp_isa_message);
    }
}
