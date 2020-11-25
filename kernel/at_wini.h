#ifndef AT_WINI_H
#define AT_WINI_H
/* 
 * This file contains definitions necessary for at_wini.c driver.
 *
 * The file contains one entry point:
 *
 *   at_winchester_task:	main entry when system is brought up
 *
 *
 * Changes:
 *	23 Nov 2020 by Luke Skywalker: adding XT-CF-lite suuport
 *	13 Apr 1992 by Kees J. Bot: device dependent/independent split.
 */
#include "kernel.h"
#include "driver.h"
#include "drvlib.h"

#if ENABLE_AT_WINI
#define IF_IDE		0	/* standard IDE interface (ports 1f0/170) */
#define IF_CF_XT	1	/* Compact Flash XT lite rev 4.1, no IRQ  */
#define IF_CF_XT_TEST
/* I/O Ports used by winchester disk controllers. */

/* Read and write registers */
#if IF_IDE
#define REG_BASE0	0x1F0	/* base register of controller 0 */
#define REG_BASE1	0x170	/* base register of controller 1 */
#elif IF_CF_XT
#define REG_BASE	0x320	/* Must coincide with the card sw1--3 */
#endif /* IF_IDE */
#define REG_DATA	    0	/* data register (offset from the base reg.) */
#if IF_IDE
#define REG_PRECOMP	    1	/* start of write precompensation */
#define REG_COUNT	    2	/* sectors to transfer */
#define REG_SECTOR	    3	/* sector number */
#define REG_CYL_LO	    4	/* low byte of cylinder number */
#define REG_CYL_HI	    5	/* high byte of cylinder number */
#define REG_LDH		    6	/* lba, drive and head */
#elif IF_CF_XT
#define REG_FEATURE	  2*1	/* Compact Flash feature register */
#define REG_COUNT	  2*2	/* sectors to transfer */
#define REG_SECTOR	  2*3	/* sector number */
#define REG_CYL_LO	  2*4	/* low byte of cylinder number */
#define REG_CYL_HI	  2*5	/* high byte of cylinder number */
#define REG_LDH		  2*6	/* lba, drive and head */
#endif /* IF_IDE */
#define   LDH_LBA		0x40	/* Use LBA addressing */
#define   LDH_DEFAULT		0xA0	/* ECC enable, 512 bytes per sector */
#define   ldh_init(drive)	(LDH_DEFAULT | ((drive) << 4))

/* Read only registers */
#if IF_IDE
#define REG_STATUS	    7	/* status */
#elif IF_CF_XT
#define REG_STATUS	  2*7	/* status */
#endif /* IF_IDE */

#define   STATUS_BSY		0x80	/* controller busy */
#define	  STATUS_RDY		0x40	/* drive ready */
#define	  STATUS_WF		0x20	/* write fault */
#define	  STATUS_SC		0x10	/* seek complete (obsolete) */
#define	  STATUS_DRQ		0x08	/* data transfer request */
#define	  STATUS_CRD		0x04	/* corrected data */
#define	  STATUS_IDX		0x02	/* index pulse */
#define	  STATUS_ERR		0x01	/* error */
#if IF_IDE
#define REG_ERROR	    1	/* error code */
#elif IF_CF_XT
#define REG_ERROR	  2*1	/* CF XT error code*/
#endif /* IF_IDE */
#define	  ERROR_BB		0x80	/* bad block */
#define	  ERROR_ECC		0x40	/* bad ecc bytes */
#define	  ERROR_ID		0x10	/* id not found */
#define	  ERROR_AC		0x04	/* aborted command */
#define	  ERROR_TK		0x02	/* track zero error */
#define	  ERROR_DM		0x01	/* no data address mark */

/* Write only registers */
#if IF_IDE
#define REG_COMMAND	    7	/* command */
#elif IF_CF_XT
#define REG_COMMAND	  2*7	/* command */
#endif /* IF_IDE */
#define   ATA_IDENTIFY		0xEC	/* identify drive */
#define   CMD_RECALIBRATE	0x10	/* recalibrate drive */
#define   CMD_READ		0x20	/* read data */
#define   CMD_WRITE		0x30	/* write data */
#define   CMD_READVERIFY	0x40	/* read verify */
#define   CMD_FORMAT		0x50	/* format track */
#define   CMD_SEEK		0x70	/* seek cylinder */
#define   CMD_DIAG		0x90	/* execute device diagnostics */
#define   CMD_SPECIFY		0x91	/* specify parameters */
#if IF_IDE
#define   CMD_IDLE		0x00	/* for w_command: drive idle */
#define REG_CTL		0x206	/* control register */
#define   CTL_NORETRY		0x80	/* disable access retry */
#define   CTL_NOECC		0x40	/* disable ecc retry */
#define   CTL_EIGHTHEADS	0x08	/* more than eight heads */
#define   CTL_RESET		0x04	/* reset controller */
#define   CTL_INTDISABLE	0x02	/* disable interrupts */
#elif IF_CF_XT
#define   CMD_IDLE		0xE3	/* for w_command: drive idle */
#define REG_CTL		 0x06		/* control register (see PCB scm) */
#define   CTL_RESET		0x04	/* reset controller */
#define   CTL_INTDISABLE	0x02	/* disable interrupts */
#endif /* IF_IDE */
/* Interrupt request lines. */
#if IF_IDE
#define AT_IRQ0		14	/* interrupt number for controller 0 */
#define AT_IRQ1		15	/* interrupt number for controller 1 */
#endif /* IF_IDE */

/* Common command block */
struct command {
#if IF_IDE
  u8_t	precomp;	/* REG_PRECOMP, etc. */
#elif IF_CF_XT
  u8_t  feature;	/* REG_FEATURE, etc */
#endif /* IF_IDE */
  u8_t	count;
  u8_t	sector;
  u8_t	cyl_lo;
  u8_t	cyl_hi;
  u8_t	ldh;
  u8_t	command;
};
int com_out (struct command *cmd);

/* Error codes */
#define ERR		 (-1)	/* general error */
#define ERR_BAD_SECTOR	 (-2)	/* block marked bad detected */

/* Some controllers don't interrupt, the clock will wake us up. */
#define WAKEUP		(32*HZ)	/* drive may be out for 31 seconds max */

/* Miscellaneous. */
#if IF_IDE
#define MAX_DRIVES         4	/* this driver supports 4 drives (hd0 - hd19) */
#elif IF_CF_XT
#define MAX_DRIVES	   2	/* Set to 2 up to now */
#endif /* IF_IDE */
#if _WORD_SIZE > 2
#define MAX_SECS	 256	/* controller can transfer this many sectors */
#else
#define MAX_SECS	 127	/* but not to a 16 bit process */
#endif /* _WORD_SIZE */
#define MAX_ERRORS         4	/* how often to try rd/wt before quitting */
#define NR_DEVICES      (MAX_DRIVES * DEV_PER_DRIVE)
#define SUB_PER_DRIVE	(NR_PARTITIONS * NR_PARTITIONS)
#define NR_SUBDEVS	(MAX_DRIVES * SUB_PER_DRIVE)
#define TIMEOUT        32000	/* controller timeout in ms */
#define RECOVERYTIME     500	/* controller recovery time in ms */
#define INITIALIZED	0x01	/* drive is initialized */
#define DEAF		0x02	/* controller must be reset */
#define SMART		0x04	/* drive supports ATA commands */

PRIVATE struct wini {		/* main drive struct, one entry per drive */
  unsigned state;		/* drive state: deaf, initialized, dead */
  unsigned base;		/* base register of the register file */
  unsigned irq;			/* interrupt request line */
  unsigned lcylinders;		/* logical number of cylinders (BIOS) */
  unsigned lheads;		/* logical number of heads */
  unsigned lsectors;		/* logical number of sectors per track */
  unsigned pcylinders;		/* physical number of cylinders (translated) */
  unsigned pheads;		/* physical number of heads */
  unsigned psectors;		/* physical number of sectors per track */
  unsigned ldhpref;		/* top four bytes of the LDH (head) register */
#if IF_IDE
  unsigned precomp;		/* write precompensation cylinder / 4 */
#elif IF_CF_XT
  unsigned feature;		/* CF feature reg */
#endif /* IF_IDE */
  unsigned max_count;		/* max request for this drive */
  unsigned open_ct;		/* in-use count */
  struct device part[DEV_PER_DRIVE];    /* primary partitions: hd[0-4] */
  struct device subpart[SUB_PER_DRIVE]; /* subpartitions: hd[1-4][a-d] */
};

PUBLIC void w_test_and_panic (struct wini * w_wn);

/* w_waitfor loop unrolled once for speed. */
#define waitfor(mask, value)	\
	((in_byte(w_wn->base + REG_STATUS) & mask) == value \
		|| w_waitfor(mask, value))

#if ENABLE_ATAPI
#include "atapi.c"	/* extra code for ATAPI CD-ROM */
#endif /* ENABLE_ATAPI */

#endif /* ENABLE_AT_WINI */
#endif /* AT_WINI_H */
