#ifndef _PNP_ISA_H_
#define _PNP_ISA_H_
#include <sys/types.h>

#define PNP_ADDRESS_PORT    0x0279
#define PNP_WRITE_DATA      (PNPN_ADDRESS_PORT + 0x0800)
#define PNP_READ_DATA_LO    0x0200
#define PNP_READ_DATA_STEP  0x0001
#define PNP_READ_DATA_HI    0x03ff
#define PNP_TABLE_LEN       (((PNP_READ_DATA_HI+1)-PNP_READ_DATA_LO)/PNP_READ_DATA_STEP)

#define PNP_INITIATION_KEY_LEN  32

#define PNP_ISOLATE_SEQ_LEN 72
#define PNP_ID_LEN (PNP_ISOLATE_SEQ_LEN/8)
#define PNP_VENDOR_ID_LEN   4
#define PNP_SN_LEN          4
#define PNP_ISOLATE_MAGIC_1 0x55
#define PNP_ISOLATE_MAGIC_2 0xAA

#define PNP_PORT_ASSIGNED       0
#define PNP_CHECKSUM_NOT_MATCH  -1
#define PNP_CARD_NOT_DETECTED   -2

typedef struct {
    u16_t   port_read_data;
    char    vendor_ID [PNP_VENDOR_ID_LEN];
    char    serial_number [PNP_SN_LEN];
    u8_t    checksum;
} pnp_isa_t;

#endif /*_PNP_ISA_H_*/
