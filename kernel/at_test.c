#include "at_wini.h"
#if ENABLE_AT_WINI
#ifdef IF_CF_XT_TEST
/*============================================================================*
 *				w_test_and_panic			      *	
 *============================================================================*/
PUBLIC void w_test_and_panic (struct wini * w_wn)
{
  int cyl, head=0, sector=0;
  struct command cmd;

  cmd.command = CMD_READVERIFY;
  cmd.sector = sector + 1;
  cmd.count = 1;

  for (cyl = 0; cyl < w_wn->pcylinders; cyl++) 
  for (head = 0; head < w_wn->pheads; head++) {
	if (!(cyl% 50)) printf ("\rTest is ongoing: %d of %d cylinder",
		cyl, w_wn->pcylinders);
  	cmd.ldh = w_wn->ldhpref | (0x0f & head);
	cmd.cyl_lo = cyl & 0xff;
	cmd.cyl_hi = (cyl>>8) & 0xff;
	if (com_out (&cmd) != OK) {
  		printf ("\nCheck CHS: %dx%dx%d ", cyl, cmd.ldh & 0x0f,
			cmd.sector);
		printf ("failed\n");
		printf ("Status reg: %X, error reg: %X \n", in_byte (w_wn->base
			+ REG_STATUS), in_byte (w_wn->base + REG_ERROR));
		break;
	}
  }
  printf ("\nBase port: %X, LDH pref: %X\n", w_wn->base, w_wn->ldhpref);
  printf ("BIOS CHS:  %dx%dx%d\n", w_wn->lcylinders, w_wn->lheads,
	w_wn->lsectors);
  printf ("CARD CHS:  %dx%dx%d\n", w_wn->pcylinders, w_wn->pheads,
	w_wn->psectors);

  panic ("CF XT test is done.", NO_NUM);
}
#endif /* ifdef IF_CF_XT_TEST */
#endif /* ENABLE_AT_WINI */
