/* Code and data for the IBM console driver.
 *
 * The 6845 video controller used by the IBM PC shares its video memory with
 * the CPU somewhere in the 0xB0000 memory bank.  To the 6845 this memory
 * consists of 16-bit words.  Each word has a character code in the low byte
 * and a so-called attribute byte in the high byte.  The CPU directly modifies
 * video memory to display characters, and sets two registers on the 6845 that
 * specify the video origin and the cursor position.  The video origin is the
 * place in video memory where the first character (upper left corner) can
 * be found.  Moving the origin is a fast way to scroll the screen.  Some
 * video adapters wrap around the top of video memory, so the origin can
 * move without bounds.  For other adapters screen memory must sometimes be
 * moved to reset the origin.  All computations on video memory use character
 * (word) addresses for simplicity and assume there is no wrapping.  The
 * assembly support functions translate the word addresses to byte addresses
 * and the scrolling function worries about wrapping.
 */

#include "kernel.h"
#include <termios.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include "protect.h"
#include "tty.h"
#include "proc.h"

/* Definitions used by the console driver. */
#define MONO_BASE    0xB0000L	/* base of mono video memory */
#define COLOR_BASE   0xB8000L	/* base of color video memory */
#define MONO_SIZE     0x1000	/* 4K mono video memory */
#define COLOR_SIZE    0x4000	/* 16K color video memory */
#define EGA_SIZE      0x8000	/* EGA & VGA have at least 32K */
#define BLANK_COLOR   0x0700	/* determines cursor color on blank screen */
#define SCROLL_UP          0	/* scroll forward */
#define SCROLL_DOWN        1	/* scroll backward */
#define BLANK_MEM ((u16_t *) 0)	/* tells mem_vid_copy() to blank the screen */
#define CONS_RAM_WORDS    80	/* video ram buffer size */
#define MAX_ESC_PARMS      2	/* number of escape sequence params allowed */

/* Constants relating to the controller chips. */
#define M_6845         0x3B4	/* port for 6845 mono */
#define C_6845         0x3D4	/* port for 6845 color */
#define EGA            0x3C4	/* port for EGA card */
#define INDEX              0	/* 6845's index register */
#define DATA               1	/* 6845's data register */
#define VID_ORG           12	/* 6845's origin register */
#define CURSOR            14	/* 6845's cursor register */

/* Beeper. */
#define BEEP_FREQ     0x0533	/* value to put into timer to set beep freq */
#define B_TIME		   3	/* length of CTRL-G beep is ticks */

/* definitions used for font management */
#define GA_SEQUENCER_INDEX	0x3C4
#define GA_SEQUENCER_DATA	0x3C5
#define GA_GRAPHICS_INDEX	0x3CE
#define GA_GRAPHICS_DATA	0x3CF
#define GA_VIDEO_ADDRESS	0xA0000L
#define GA_FONT_SIZE		8192

/* Private variables used by the console driver. */
PRIVATE int vid_port;		/* I/O port for accessing 6845 */
PRIVATE int softscroll;		/* 1 = software scrolling, 0 = hardware */
PRIVATE int annoying_beep=1;	/* 1 = annoy people, 0 = make them happy */
PRIVATE int beeping;		/* speaker is beeping? */
/* Data per a physical screen */
typedef struct video {
	unsigned vid_seg;	/* Selector or segment of video RAM
				 * starts either at 0xb0000, or 0xb80000 */
	unsigned vid_size;	/* 0x2000 for colour or 0x0800 for mono */
	unsigned vid_mask;	/* 0x1fff for colour or 0x07ff for mono */
	unsigned blank_color;	/* attribute byte for a blank char */
	int	 vid_port;	/* I/O port to access M6845 */
	int	 wrap;		/* Can hardware wrap? */
	int	 soft_scroll;	/* Software (== 1) or hardware (== 0) scroll*/
	long unsigned vid_base;	/* Base video RAM, either 0xb000 or 0xb800 */
} display_t;

#define scr_width	80	/* # characters on a line */
#define scr_lines	25	/* # lines on the screen */
#define scr_size	(80*25)	/* # characters on the screen */

/* Per console data. */
typedef struct console {
  tty_t *c_tty;			/* associated TTY struct */
  int c_column;			/* current column number (0-origin) */
  int c_row;			/* current row (0 at top of screen) */
  int c_rwords;			/* number of WORDS (not bytes) in outqueue */
  unsigned c_start;		/* start of video memory of this console */
  unsigned c_limit;		/* limit of this console's video memory */
  unsigned c_org;		/* location in RAM where 6845 base points */
  unsigned c_cur;		/* current position of cursor in video RAM */
  unsigned c_attr;		/* character attribute */
  unsigned c_blank;		/* blank attribute */
  char c_esc_state;		/* 0=normal, 1=ESC, 2=ESC[ */
  char c_esc_intro;		/* Distinguishing character following ESC */
  int *c_esc_parmp;		/* pointer to current escape parameter */
  int c_esc_parmv[MAX_ESC_PARMS];	/* list of escape parameters */
  u16_t c_ramqueue[CONS_RAM_WORDS];	/* buffer for video RAM */
  display_t * display;		/* pointer to display structure */
} console_t;

PRIVATE int nr_cons= 1;		/* actual number of consoles */
PRIVATE int nr_displays = 1;	/* actual number of screens */
PRIVATE display_t display_table [2];
PRIVATE console_t cons_table[NR_CONS];
PRIVATE console_t *curcons;	/* currently visible */

/* Map from ANSI colors to the attributes used by the PC */
PRIVATE int ansi_colors[8] = {0, 4, 2, 6, 1, 5, 3, 7};

/* Structure used for font management */
struct sequence {
	unsigned short index;
	unsigned char port;
	unsigned char value;
};

FORWARD _PROTOTYPE( void cons_write, (struct tty *tp)			);
FORWARD _PROTOTYPE( void cons_echo, (tty_t *tp, int c)			);
FORWARD _PROTOTYPE( void out_char, (console_t *cons, int c)		);
FORWARD _PROTOTYPE( void beep, (void)					);
FORWARD _PROTOTYPE( void do_escape, (console_t *cons, int c)		);
FORWARD _PROTOTYPE( void flush, (console_t *cons)			);
FORWARD _PROTOTYPE( void parse_escape, (console_t *cons, int c)		);
FORWARD _PROTOTYPE( void scroll_screen, (console_t *cons, int dir)	);
FORWARD _PROTOTYPE( void set_6845, (int reg, unsigned val, display_t * d));
FORWARD _PROTOTYPE( void stop_beep, (void)				);
FORWARD _PROTOTYPE( void cons_org0, (void)				);
FORWARD _PROTOTYPE( void ga_program, (struct sequence *seq) );


/*===========================================================================*
 *				cons_write				     *
 *===========================================================================*/
PRIVATE void cons_write(tp)
register struct tty *tp;	/* tells which terminal is to be used */
{
/* Copy as much data as possible to the output queue, then start I/O.  On
 * memory-mapped terminals, such as the IBM console, the I/O will also be
 * finished, and the counts updated.  Keep repeating until all I/O done.
 */

  int count;
  register char *tbuf;
  char buf[64];
  phys_bytes user_phys;
  console_t *cons = tp->tty_priv;

  /* Check quickly for nothing to do, so this can be called often without
   * unmodular tests elsewhere.
   */
  if ((count = tp->tty_outleft) == 0 || tp->tty_inhibited) return;

  /* Copy the user bytes to buf[] for decent addressing. Loop over the
   * copies, since the user buffer may be much larger than buf[].
   */
  do {
	if (count > sizeof(buf)) count = sizeof(buf);
	user_phys = proc_vir2phys(proc_addr(tp->tty_outproc), tp->tty_out_vir);
	phys_copy(user_phys, vir2phys(buf), (phys_bytes) count);
	tbuf = buf;

	/* Update terminal data structure. */
	tp->tty_out_vir += count;
	tp->tty_outcum += count;
	tp->tty_outleft -= count;

	/* Output each byte of the copy to the screen.  Avoid calling
	 * out_char() for the "easy" characters, put them into the buffer
	 * directly.
	 */
	do {
		if ((unsigned) *tbuf < ' ' || cons->c_esc_state > 0
			|| cons->c_column >= scr_width
			|| cons->c_rwords >= buflen(cons->c_ramqueue))
		{
			out_char(cons, *tbuf++);
		} else {
			cons->c_ramqueue[cons->c_rwords++] =
					cons->c_attr | (*tbuf++ & BYTE);
			cons->c_column++;
		}
	} while (--count != 0);
  } while ((count = tp->tty_outleft) != 0 && !tp->tty_inhibited);

  flush(cons);			/* transfer anything buffered to the screen */

  /* Reply to the writer if all output is finished. */
  if (tp->tty_outleft == 0) {
	tty_reply(tp->tty_outrepcode, tp->tty_outcaller, tp->tty_outproc,
							tp->tty_outcum);
	tp->tty_outcum = 0;
  }
}


/*===========================================================================*
 *				cons_echo				     *
 *===========================================================================*/
PRIVATE void cons_echo(tp, c)
register tty_t *tp;		/* pointer to tty struct */
int c;				/* character to be echoed */
{
/* Echo keyboard input (print & flush). */
  console_t *cons = tp->tty_priv;

  out_char(cons, c);
  flush(cons);
}


/*===========================================================================*
 *				out_char				     *
 *===========================================================================*/
PRIVATE void out_char(cons, c)
register console_t *cons;	/* pointer to console struct */
int c;				/* character to be output */
{
/* Output a character on the console.  Check for escape sequences first. */
  if (cons->c_esc_state > 0) {
	parse_escape(cons, c);
	return;
  }

  switch(c) {
	case 000:		/* null is typically used for padding */
		return;		/* better not do anything */

	case 007:		/* ring the bell */
		flush(cons);	/* print any chars queued for output */
		if (annoying_beep) {
			beep();
		}
		return;

	case '\b':		/* backspace */
		if (--cons->c_column < 0) {
			if (--cons->c_row >= 0) cons->c_column += scr_width;
		}
		flush(cons);
		return;

	case '\n':		/* line feed */
		if ((cons->c_tty->tty_termios.c_oflag & (OPOST|ONLCR))
						== (OPOST|ONLCR)) {
			cons->c_column = 0;
		}
		/*FALL THROUGH*/
	case 013:		/* CTRL-K */
	case 014:		/* CTRL-L */
		if (cons->c_row == scr_lines-1) {
			scroll_screen(cons, SCROLL_UP);
		} else {
			cons->c_row++;
		}
		flush(cons);
		return;

	case '\r':		/* carriage return */
		cons->c_column = 0;
		flush(cons);
		return;

	case '\t':		/* tab */
		cons->c_column = (cons->c_column + TAB_SIZE) & ~TAB_MASK;
		if (cons->c_column > scr_width) {
			cons->c_column -= scr_width;
			if (cons->c_row == scr_lines-1) {
				scroll_screen(cons, SCROLL_UP);
			} else {
				cons->c_row++;
			}
		}
		flush(cons);
		return;

	case 033:		/* ESC - start of an escape sequence */
		flush(cons);	/* print any chars queued for output */
		cons->c_esc_state = 1;	/* mark ESC as seen */
		return;

	default:		/* printable chars are stored in ramqueue */
		if (cons->c_column >= scr_width) {
			if (!LINEWRAP) return;
			if (cons->c_row == scr_lines-1) {
				scroll_screen(cons, SCROLL_UP);
			} else {
				cons->c_row++;
			}
			cons->c_column = 0;
			flush(cons);
		}
		if (cons->c_rwords == buflen(cons->c_ramqueue)) flush(cons);
		cons->c_ramqueue[cons->c_rwords++] = cons->c_attr | (c & BYTE);
		cons->c_column++;			/* next column */
		return;
  }
}


/*===========================================================================*
 *				scroll_screen				     *
 *===========================================================================*/
PRIVATE void scroll_screen(cons, dir)
register console_t *cons;	/* pointer to console struct */
int dir;			/* SCROLL_UP or SCROLL_DOWN */
{
  unsigned new_line, new_org, chars;
  display_t * display;

  display = cons->display;

  flush(cons);
  chars = scr_size - scr_width;		/* one screen minus one line */

  /* Scrolling the screen is a real nuisance due to the various incompatible
   * video cards.  This driver supports software scrolling (Hercules?),
   * hardware scrolling (mono and CGA cards) and hardware scrolling without
   * wrapping (EGA cards).  In the latter case we must make sure that
   *		c_start <= c_org && c_org + scr_size <= c_limit
   * holds, because EGA doesn't wrap around the end of video memory.
   */
  if (dir == SCROLL_UP) {
	/* Scroll one line up in 3 ways: soft, avoid wrap, use origin. */
	if (softscroll) {
		vid_vid_copy(cons->c_start + scr_width, cons->c_start, chars,
			display);
	} else
	if (!display->wrap && cons->c_org + scr_size + scr_width
		 >= cons->c_limit) {
		vid_vid_copy(cons->c_org + scr_width, cons->c_start, chars,
			display);
		cons->c_org = cons->c_start;
	} else {
		cons->c_org = (cons->c_org + scr_width) & display->vid_mask;
	}
	new_line = (cons->c_org + chars) & display->vid_mask;
  } else {
	/* Scroll one line down in 3 ways: soft, avoid wrap, use origin. */
	if (softscroll) {
		vid_vid_copy(cons->c_start, cons->c_start + scr_width, chars,
			display);
	} else
	if (!display->wrap && cons->c_org < cons->c_start + scr_width) {
		new_org = cons->c_limit - scr_size;
		vid_vid_copy(cons->c_org, new_org + scr_width, chars,
			display);
		cons->c_org = new_org;
	} else {
		cons->c_org = (cons->c_org - scr_width) & display->vid_mask;
	}
	new_line = cons->c_org;
  }
  /* Blank the new line at top or bottom. */
  mem_vid_copy(BLANK_MEM, new_line, scr_width, display);

  /* Set the new video origin. */
  /* The next line is commented to fix the scrolling issue in dual-monitor
	configuration. But it works only with two teletypes, it doesn't work
	with more than two (I didn't check, but I know). */
  /* if (cons == curcons) */
  set_6845(VID_ORG, cons->c_org, display);
  flush(cons);
}


/*===========================================================================*
 *				flush					     *
 *===========================================================================*/
PRIVATE void flush(cons)
register console_t *cons;	/* pointer to console struct */
{
/* Send characters buffered in 'ramqueue' to screen memory, check the new
 * cursor position, compute the new hardware cursor position and set it.
 */
  unsigned cur;
  tty_t *tp = cons->c_tty;

  /* Have the characters in 'ramqueue' transferred to the screen. */
  if (cons->c_rwords > 0) {
	mem_vid_copy(cons->c_ramqueue, cons->c_cur, cons->c_rwords,
		cons->display);
	cons->c_rwords = 0;

	/* TTY likes to know the current column and if echoing messed up. */
	tp->tty_position = cons->c_column;
	tp->tty_reprint = TRUE;
  }

  /* Check and update the cursor position. */
  if (cons->c_column < 0) cons->c_column = 0;
  if (cons->c_column > scr_width) cons->c_column = scr_width;
  if (cons->c_row < 0) cons->c_row = 0;
  if (cons->c_row >= scr_lines) cons->c_row = scr_lines - 1;
  cur = cons->c_org + cons->c_row * scr_width + cons->c_column;
  if (cur != cons->c_cur) {
	if (cons == curcons)
	set_6845(CURSOR, cur, cons->display);
	cons->c_cur = cur;
  }
}


/*===========================================================================*
 *				parse_escape				     *
 *===========================================================================*/
PRIVATE void parse_escape(cons, c)
register console_t *cons;	/* pointer to console struct */
char c;				/* next character in escape sequence */
{
/* The following ANSI escape sequences are currently supported.
 * If n and/or m are omitted, they default to 1.
 *   ESC [nA moves up n lines
 *   ESC [nB moves down n lines
 *   ESC [nC moves right n spaces
 *   ESC [nD moves left n spaces
 *   ESC [m;nH" moves cursor to (m,n)
 *   ESC [J clears screen from cursor
 *   ESC [K clears line from cursor
 *   ESC [nL inserts n lines ar cursor
 *   ESC [nM deletes n lines at cursor
 *   ESC [nP deletes n chars at cursor
 *   ESC [n@ inserts n chars at cursor
 *   ESC [nm enables rendition n (0=normal, 4=bold, 5=blinking, 7=reverse)
 *   ESC M scrolls the screen backwards if the cursor is on the top line
 */

  switch (cons->c_esc_state) {
    case 1:			/* ESC seen */
	cons->c_esc_intro = '\0';
	cons->c_esc_parmp = cons->c_esc_parmv;
	cons->c_esc_parmv[0] = cons->c_esc_parmv[1] = 0;
	switch (c) {
	    case '[':	/* Control Sequence Introducer */
		cons->c_esc_intro = c;
		cons->c_esc_state = 2;
		break;
	    case 'M':	/* Reverse Index */
		do_escape(cons, c);
		break;
	    default:
		cons->c_esc_state = 0;
	}
	break;

    case 2:			/* ESC [ seen */
	if (c >= '0' && c <= '9') {
		if (cons->c_esc_parmp < bufend(cons->c_esc_parmv))
			*cons->c_esc_parmp = *cons->c_esc_parmp * 10 + (c-'0');
	} else
	if (c == ';') {
		if (++cons->c_esc_parmp < bufend(cons->c_esc_parmv))
			*cons->c_esc_parmp = 0;
	} else {
		do_escape(cons, c);
	}
	break;
  }
}


/*===========================================================================*
 *				do_escape				     *
 *===========================================================================*/
PRIVATE void do_escape(cons, c)
register console_t *cons;	/* pointer to console struct */
char c;				/* next character in escape sequence */
{
  int value, n;
  unsigned src, dst, count;

  /* Some of these things hack on screen RAM, so it had better be up to date */
  flush(cons);

  if (cons->c_esc_intro == '\0') {
	/* Handle a sequence beginning with just ESC */
	switch (c) {
	    case 'M':		/* Reverse Index */
		if (cons->c_row == 0) {
			scroll_screen(cons, SCROLL_DOWN);
		} else {
			cons->c_row--;
		}
		flush(cons);
		break;

	    default: break;
	}
  } else
  if (cons->c_esc_intro == '[') {
	/* Handle a sequence beginning with ESC [ and parameters */
	value = cons->c_esc_parmv[0];
	switch (c) {
	    case 'A':		/* ESC [nA moves up n lines */
		n = (value == 0 ? 1 : value);
		cons->c_row -= n;
		flush(cons);
		break;

	    case 'B':		/* ESC [nB moves down n lines */
		n = (value == 0 ? 1 : value);
		cons->c_row += n;
		flush(cons);
		break;

	    case 'C':		/* ESC [nC moves right n spaces */
		n = (value == 0 ? 1 : value);
		cons->c_column += n;
		flush(cons);
		break;

	    case 'D':		/* ESC [nD moves left n spaces */
		n = (value == 0 ? 1 : value);
		cons->c_column -= n;
		flush(cons);
		break;

	    case 'H':		/* ESC [m;nH" moves cursor to (m,n) */
		cons->c_row = cons->c_esc_parmv[0] - 1;
		cons->c_column = cons->c_esc_parmv[1] - 1;
		flush(cons);
		break;

	    case 'J':		/* ESC [sJ clears in display */
		switch (value) {
		    case 0:	/* Clear from cursor to end of screen */
			count = scr_size - (cons->c_cur - cons->c_org);
			dst = cons->c_cur;
			break;
		    case 1:	/* Clear from start of screen to cursor */
			count = cons->c_cur - cons->c_org;
			dst = cons->c_org;
			break;
		    case 2:	/* Clear entire screen */
			count = scr_size;
			dst = cons->c_org;
			break;
		    default:	/* Do nothing */
			count = 0;
			dst = cons->c_org;
		}
		mem_vid_copy(BLANK_MEM, dst, count, cons->display);
		break;

	    case 'K':		/* ESC [sK clears line from cursor */
		switch (value) {
		    case 0:	/* Clear from cursor to end of line */
			count = scr_width - cons->c_column;
			dst = cons->c_cur;
			break;
		    case 1:	/* Clear from beginning of line to cursor */
			count = cons->c_column;
			dst = cons->c_cur - cons->c_column;
			break;
		    case 2:	/* Clear entire line */
			count = scr_width;
			dst = cons->c_cur - cons->c_column;
			break;
		    default:	/* Do nothing */
			count = 0;
			dst = cons->c_cur;
		}
		mem_vid_copy(BLANK_MEM, dst, count, cons->display);
		break;

	    case 'L':		/* ESC [nL inserts n lines at cursor */
		n = value;
		if (n < 1) n = 1;
		if (n > (scr_lines - cons->c_row))
			n = scr_lines - cons->c_row;

		src = cons->c_org + cons->c_row * scr_width;
		dst = src + n * scr_width;
		count = (scr_lines - cons->c_row - n) * scr_width;
		vid_vid_copy(src, dst, count, cons->display);
		mem_vid_copy(BLANK_MEM, src, n * scr_width, cons->display);
		break;

	    case 'M':		/* ESC [nM deletes n lines at cursor */
		n = value;
		if (n < 1) n = 1;
		if (n > (scr_lines - cons->c_row))
			n = scr_lines - cons->c_row;

		dst = cons->c_org + cons->c_row * scr_width;
		src = dst + n * scr_width;
		count = (scr_lines - cons->c_row - n) * scr_width;
		vid_vid_copy(src, dst, count, cons->display);
		mem_vid_copy(BLANK_MEM, dst + count, n * scr_width,
			cons->display);
		break;

	    case '@':		/* ESC [n@ inserts n chars at cursor */
		n = value;
		if (n < 1) n = 1;
		if (n > (scr_width - cons->c_column))
			n = scr_width - cons->c_column;

		src = cons->c_cur;
		dst = src + n;
		count = scr_width - cons->c_column - n;
		vid_vid_copy(src, dst, count, cons->display);
		mem_vid_copy(BLANK_MEM, src, n, cons->display);
		break;

	    case 'P':		/* ESC [nP deletes n chars at cursor */
		n = value;
		if (n < 1) n = 1;
		if (n > (scr_width - cons->c_column))
			n = scr_width - cons->c_column;

		dst = cons->c_cur;
		src = dst + n;
		count = scr_width - cons->c_column - n;
		vid_vid_copy(src, dst, count, cons->display);
		mem_vid_copy(BLANK_MEM, dst + count, n, cons->display);
		break;

	    case 'm':		/* ESC [nm enables rendition n */
		switch (value) {
			int color;
			color = cons->display->vid_base==C_6845 ? 1 : 0;
		    case 1:	/* BOLD  */
			if (color) {
				/* Can't do bold, so use yellow */
				cons->c_attr = (cons->c_attr & 0xf0ff) | 0x0E00;
			} else {
				/* Set intensity bit */
				cons->c_attr |= 0x0800;
			}
			break;

		    case 4:	/* UNDERLINE */
			if (color) {
				/* Use light green */
				cons->c_attr = (cons->c_attr & 0xf0ff) | 0x0A00;
			} else {
				cons->c_attr = (cons->c_attr & 0x8900);
			}
			break;

		    case 5:	/* BLINKING */
			if (color) {
				/* Use magenta */
				cons->c_attr = (cons->c_attr & 0xf0ff) | 0x0500;
			} else {
				/* Set the blink bit */
				cons->c_attr |= 0x8000;
			}
			break;

		    case 7:	/* REVERSE */
			if (color) {
				/* Swap fg and bg colors */
				cons->c_attr =
					((cons->c_attr & 0xf000) >> 4) |
					((cons->c_attr & 0x0f00) << 4);
			} else
			if ((cons->c_attr & 0x7000) == 0) {
				cons->c_attr = (cons->c_attr & 0x8800) | 0x7000;
			} else {
				cons->c_attr = (cons->c_attr & 0x8800) | 0x0700;
			}
			break;

		    default:	/* COLOR */
		        if (30 <= value && value <= 37) {
				cons->c_attr =
					(cons->c_attr & 0xf0ff) |
					(ansi_colors[(value - 30)] << 8);
				cons->c_blank =
					(cons->c_blank & 0xf0ff) |
					(ansi_colors[(value - 30)] << 8);
				cons->display->blank_color = cons->c_blank;
			} else
			if (40 <= value && value <= 47) {
				cons->c_attr =
					(cons->c_attr & 0x0fff) |
					(ansi_colors[(value - 40)] << 12);
				cons->c_blank =
					(cons->c_blank & 0x0fff) |
					(ansi_colors[(value - 40)] << 12);
				cons->display->blank_color = cons->c_blank;
			} else {
				cons->c_attr = cons->c_blank;
			}
			break;
		}
		break;
	}
  }
  cons->c_esc_state = 0;
}


/*===========================================================================*
 *				set_6845				     *
 *===========================================================================*/
PRIVATE void set_6845(reg, val, screen)
int reg;			/* which register pair to set */
unsigned val;			/* 16-bit value to set it to */
display_t * screen;		/* pointer to screen structure */
{
/* Set a register pair inside the 6845.
 * Registers 12-13 tell the 6845 where in video ram to start
 * Registers 14-15 tell the 6845 where to put the cursor
 */
  lock();			/* try to stop h/w loading in-between value */
  out_byte(screen->vid_port + INDEX, reg);		/* set the index register */
  out_byte(screen->vid_port + DATA, (val>>8) & BYTE);	/* output high byte */
  out_byte(screen->vid_port + INDEX, reg + 1);		/* again */
  out_byte(screen->vid_port + DATA, val&BYTE);		/* output low byte */
  unlock();
}


/*===========================================================================*
 *				beep					     *
 *===========================================================================*/
PRIVATE void beep()
{
/* Making a beeping sound on the speaker (output for CRTL-G).
 * This routine works by turning on the bits 0 and 1 in port B of the 8255
 * chip that drive the speaker.
 */

  message mess;

  if (beeping) return;
  out_byte(TIMER_MODE, 0xB6);	/* set up timer channel 2 (square wave) */
  out_byte(TIMER2, BEEP_FREQ & BYTE);	/* load low-order bits of frequency */
  out_byte(TIMER2, (BEEP_FREQ >> 8) & BYTE);	/* now high-order bits */
  lock();			/* guard PORT_B from keyboard intr handler */
  out_byte(PORT_B, in_byte(PORT_B) | 3);	/* turn on beep bits */
  unlock();
  beeping = TRUE;

  mess.m_type = SET_ALARM;
  mess.CLOCK_PROC_NR = TTY;
  mess.DELTA_TICKS = B_TIME;
  mess.FUNC_TO_CALL = (sighandler_t) stop_beep;
  sendrec(CLOCK, &mess);
}


/*===========================================================================*
 *				stop_beep				     *
 *===========================================================================*/
PRIVATE void stop_beep()
{
/* Turn off the beeper by turning off bits 0 and 1 in PORT_B. */

  lock();			/* guard PORT_B from keyboard intr handler */
  out_byte(PORT_B, in_byte(PORT_B) & ~3);
  beeping = FALSE;
  unlock();
}

/*===========================================================================*
 *				set_mda					     *
 *===========================================================================*/
PRIVATE void set_mda (void)
{
	char init_data[16] = {0x61, 0x50, 0x52, 0x0f, 0x19, 0x06, 0x19,
				0x19, 0x02, 0x0d, 0x0b, 0x0c,
				0x00, 0x00, 0x00, 0x00};
	char i = 0;
	lock ();
	out_byte (0x03b8, 0x01);
	for (i=0;i<16;i++)
	{
		out_byte (0x03b4, i);
		out_byte (0x03b5, init_data[i]);
	}
	out_byte (0x03b8, 0x29);
	/* Set cursor type */
	out_byte (0x03b4, 10);
	out_byte (0x03b5, 0x00);
	out_byte (0x03b4, 11);
	out_byte (0x03b5, 14);
	unlock ();
}

/*===========================================================================*
 *				init_display								     *
display_t *display;
u16_t crtbase;
int ega_or_vga;
 *===========================================================================*/
void init_display (display_t * display, u16_t crtport, int ega_or_vga)
{
  int display_nr;
  display_nr = display - &(display_table[0]);
  if ((display_nr > 1) || (display_nr < 0))
	return;
  display->vid_port = crtport; 
  if (display->vid_port == C_6845)
  {
	display->vid_base = COLOR_BASE;
	display->vid_size = COLOR_SIZE;
  } else {
	display->vid_base = MONO_BASE;
	display->vid_size = MONO_SIZE;
  }
  if (ega_or_vga) display->vid_size = EGA_SIZE;
  display->wrap = !ega_or_vga;

  if (display_nr == 0)
  {
  	display->vid_seg = protected_mode ? VIDEO_SELECTOR0 :
		physb_to_hclick(display->vid_base);
  	init_dataseg(&gdt[VIDEO_INDEX0], display->vid_base,
		(phys_bytes) display->vid_size, TASK_PRIVILEGE);
  }
  else {
  	display->vid_seg = protected_mode ? VIDEO_SELECTOR1 :
		physb_to_hclick(display->vid_base);
  	init_dataseg(&gdt[VIDEO_INDEX1], display->vid_base,
		(phys_bytes) display->vid_size, TASK_PRIVILEGE);
  }
  display->vid_size >>= 1;		/* word count */
  display->vid_mask = display->vid_size - 1;
}

/*===========================================================================*
 *				scr_init				     *
 *===========================================================================*/
PUBLIC void scr_init(tp)
tty_t *tp;
{
/* Initialize the screen driver. */
  console_t *cons;
  phys_bytes vid_base;
  u16_t bios_crtbase;
  int line;
  unsigned page_size;

  /* Associate console and TTY. */
  line = tp - &tty_table[0];
  if (line >= nr_cons) return;
  cons = &cons_table[line];
  cons->c_tty = tp;
  tp->tty_priv = cons;

  /* Initialize the keyboard driver. */
  kb_init(tp);
  /* Output functions. */
  tp->tty_devwrite = cons_write;
  tp->tty_echo = cons_echo;

  /* Get the BIOS parameters that tells the VDU I/O base register. */
  /* Configure the standard display (No. 0) */
  phys_copy(0x463L, vir2phys(&bios_crtbase), 2L);
  /* Standard screen, recognized by BIOS: */
  init_display (&(display_table[0]), bios_crtbase, ega);
  /* Monochrome adapter */
  init_display (&(display_table[1]), M_6845, 0);

  /* There can be as many consoles as video memory allows. */
  nr_cons = (display_table[0].vid_size +
	display_table[1].vid_size) / scr_size;
  if (nr_cons > NR_CONS) nr_cons = NR_CONS;
  if (nr_cons > 1) display_table[0].wrap = display_table[1].wrap = 0;
  /* If we have at least two consoles --- initialize monochrome adapter */
  if (line == (nr_cons-1) && (line!=0))
  {
	set_mda ();
	cons->c_start = 0;
	cons->c_limit = 2048; /* half of MDA memory, slightly above a scr */
	cons->c_org = cons->c_start;
	cons->display = &(display_table[1]);
  }
  else
  {
	page_size = display_table[0].vid_size / nr_cons;
	cons->c_start = line * page_size;
	cons->c_limit = cons->c_start + page_size;
	cons->c_org = cons->c_start;
	cons->display = &(display_table[0]);
  }
  cons->c_attr = cons->c_blank = BLANK_COLOR;
  /* Clear console. */
  cons->display->blank_color = BLANK_COLOR;
  mem_vid_copy(BLANK_MEM, cons->c_start, scr_size, cons->display);
  select_console(0);
}


/*===========================================================================*
 *				putk					     *
 *===========================================================================*/
PUBLIC void putk(c)
int c;				/* character to print */
{
/* This procedure is used by the version of printf() that is linked with
 * the kernel itself.  The one in the library sends a message to FS, which is
 * not what is needed for printing within the kernel.  This version just queues
 * the character and starts the output.
 */

  if (c != 0) {
	if (c == '\n') putk('\r');
	out_char(&cons_table[0], (int) c);
  } else {
	flush(&cons_table[0]);
  }
}

/*===========================================================================*
 *				toggle_scroll				     *
 *===========================================================================*/
PUBLIC void toggle_beeping()
{
	/* Toggle between beeping and not beeping */
	annoying_beep = !annoying_beep;
}

/*===========================================================================*
 *				toggle_scroll				     *
 *===========================================================================*/
PUBLIC void toggle_scroll()
{
/* Toggle between hardware and software scroll. */

  cons_org0();
  softscroll = !softscroll;
  printf("%sware scrolling enabled.\n", softscroll ? "Soft" : "Hard");
}


/*===========================================================================*
 *				cons_stop				     *
 *===========================================================================*/
PUBLIC void cons_stop()
{
/* Prepare for halt or reboot. */
  cons_org0();
  softscroll = 1;
  select_console(0);
  cons_table[0].c_attr = cons_table[0].c_blank = BLANK_COLOR;
  display_table[0].blank_color = BLANK_COLOR;
}


/*===========================================================================*
 *				cons_org0				     *
 *===========================================================================*/
PRIVATE void cons_org0()
{
/* Scroll video memory back to put the origin at 0. */
  int cons_line;
  console_t *cons;
  unsigned n;

  for (cons_line = 0; cons_line < nr_cons; cons_line++) {
	cons = &cons_table[cons_line];
	while (cons->c_org > cons->c_start) {
		/* amount of unused memory */
		n = cons->display->vid_size - scr_size;
		if (n > cons->c_org - cons->c_start)
			n = cons->c_org - cons->c_start;
		vid_vid_copy(cons->c_org, cons->c_org - n, scr_size,
			cons->display);
		cons->c_org -= n;
	}
	flush(cons);
  }
  select_console(current);
}


/*===========================================================================*
 *				select_console				     *
 *===========================================================================*/
PUBLIC void select_console(int cons_line)
{
/* Set the current console to console number 'cons_line'. */

  if (cons_line < 0 || cons_line >= nr_cons) return;
  current = cons_line;
  curcons = &cons_table[cons_line];
  set_6845(VID_ORG, curcons->c_org, curcons->display);
  set_6845(CURSOR, curcons->c_cur, curcons->display);
}


/*===========================================================================*
 *				con_loadfont				     *
 *===========================================================================*/

PUBLIC int con_loadfont(user_phys)
phys_bytes user_phys;
{
/* Load a font into the EGA or VGA adapter. */
  static struct sequence seq1[7] = {
	{ GA_SEQUENCER_INDEX, 0x00, 0x01 },
	{ GA_SEQUENCER_INDEX, 0x02, 0x04 },
	{ GA_SEQUENCER_INDEX, 0x04, 0x07 },
	{ GA_SEQUENCER_INDEX, 0x00, 0x03 },
	{ GA_GRAPHICS_INDEX, 0x04, 0x02 },
	{ GA_GRAPHICS_INDEX, 0x05, 0x00 },
	{ GA_GRAPHICS_INDEX, 0x06, 0x00 },
  };
  static struct sequence seq2[7] = {
	{ GA_SEQUENCER_INDEX, 0x00, 0x01 },
	{ GA_SEQUENCER_INDEX, 0x02, 0x03 },
	{ GA_SEQUENCER_INDEX, 0x04, 0x03 },
	{ GA_SEQUENCER_INDEX, 0x00, 0x03 },
	{ GA_GRAPHICS_INDEX, 0x04, 0x00 },
	{ GA_GRAPHICS_INDEX, 0x05, 0x10 },
	{ GA_GRAPHICS_INDEX, 0x06,    0 },
  };
  /* MDA has no uploading fonts, and EGA should be colourful*/
  seq2[6].value=0x0e; /*color ? 0x0E : 0x0A;*/

  if (!ega) return(ENOTTY);

  lock();
  ga_program(seq1);	/* bring font memory into view */

  phys_copy(user_phys, (phys_bytes)GA_VIDEO_ADDRESS, (phys_bytes)GA_FONT_SIZE);

  ga_program(seq2);	/* restore */
  unlock();

  return(OK);
}


/*===========================================================================*
 *				ga_program				     *
 *===========================================================================*/

PRIVATE void ga_program(seq)
struct sequence *seq;
{
  int len= 7;
  do {
	out_byte(seq->index, seq->port);
	out_byte(seq->index+1, seq->value);
	seq++;
  } while (--len > 0);
}
