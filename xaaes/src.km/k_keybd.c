/*
 * $Id$
 *
 * XaAES - XaAES Ain't the AES (c) 1992 - 1998 C.Graham
 *                                 1999 - 2003 H.Robbers
 *                                        2004 F.Naumann & O.Skancke
 *
 * A multitasking AES replacement for FreeMiNT
 *
 * This file is part of XaAES.
 *
 * XaAES is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * XaAES is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with XaAES; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include RSCHNAME

#include "k_keybd.h"
#include "c_keybd.h"
#include "xa_global.h"

#include "app_man.h"
#include "c_window.h"
#include "cnf_xaaes.h"
#include "desktop.h"
#include "init.h"
#include "k_main.h"
#include "k_mouse.h"
#include "nkcc.h"
#include "scrlobjc.h"
#include "taskman.h"
#include "widgets.h"

#include "obtree.h"

#include "xa_form.h"
#include "xa_rsrc.h"
#include "xa_shel.h"
#include "trnfm.h"

#include "mint/dcntl.h"
#include "mint/fcntl.h"
#include "mint/ioctl.h"
#include "mint/signal.h"


/* Keystrokes must be put in a queue if no apps are waiting yet.
 * There also may be an error in the timer event code.
 */
//struct key_queue pending_keys;

/*
 * Keyboard input handler
 * ======================
 *
 * Courtesy Harald Siegmund:
 *
 * nkc_tconv: TOS key code converter
 *
 * This is the most important function within NKCC: it takes a key code
 * returned by TOS and converts it to the sophisticated normalized format.
 *
 * Note: the raw converter does no deadkey handling, ASCII input or
 *       Control key emulation.
 *
 * In:   D0.L           key code in TOS format:
 *                                  0                    1
 *                      bit 31:     ignored              ignored
 *                      bit 30:     ignored              ignored
 *                      bit 29:     ignored              ignored
 *                      bit 28:     no CapsLock          CapsLock
 *                      bit 27:     no Alternate         Alternate pressed
 *                      bit 26:     no Control           Control pressed
 *                      bit 25:     no left Shift key    left Shift pressed
 *                      bit 24:     no right Shift key   right Shift pressed
 *
 *                      bits 23...16: scan code
 *                      bits 15...08: ignored
 *                      bits 07...00: ASCII code (or rubbish in most cases
 *                         when Control or Alternate is pressed ...)
 *
 * Out:  D0.W           normalized key code:
 *                      bits 15...08: flags:
 *                                  0                    1
 *                      NKF?_FUNC   printable char       "function key"
 *                      NKF?_RESVD  ignore it            ignore it
 *                      NKF?_NUM    main keypad          numeric keypad
 *                      NKF?_CAPS   no CapsLock          CapsLock
 *                      NKF?_ALT    no Alternate         Alternate pressed
 *                      NKF?_CTRL   no Control           Control pressed
 *                      NKF?_LSH    no left Shift key    left Shift pressed
 *                      NKF?_RSH    no right Shift key   right Shift pressed
 *
 *                      bits 07...00: key code
 *                      function (NKF?_FUNC set):
 *                         < 32: special key (NK_...)
 *                         >=32: printable char + Control and/or Alternate
 *                      no function (NKF?_FUNC not set):
 *                         printable character (0...255!!!)
 */
void
cancel_keyqueue(struct xa_client *client)
{
	while (client->kq_tail)
	{
		struct keyqueue *kq = client->kq_tail;
		client->kq_tail = kq->next;
		kfree(kq);
	}
	client->kq_tail = client->kq_head = NULL;
}

void
queue_key(struct xa_client *client, const struct rawkey *key)
{
	struct keyqueue *kq;

	kq = kmalloc(sizeof(*kq));

	if (kq)
	{
		kq->next = NULL;
		kq->key = *key;

		if (client->kq_head)
		{
			client->kq_head->next = kq;
			client->kq_head = kq;
		}
		else
			client->kq_head = client->kq_tail = kq;
	}
}

bool
unqueue_key(struct xa_client *client, struct rawkey *key)
{
	bool ret = false;
	struct keyqueue *kq;
	
	if ((kq = client->kq_tail))
	{
		*key = kq->key;

		if (client->kq_tail == client->kq_head)
			client->kq_tail = client->kq_head = NULL;
		else
			client->kq_tail = kq->next;

		kfree(kq);
		ret = true;
	}
	return ret;
}
		

static void
XA_keyboard_event(enum locks lock, const struct rawkey *key)
{
	struct xa_window *keywind;
	struct xa_client *locked_client;
	struct xa_client *client;
	struct rawkey *rk;
	bool waiting;

	if (!(client = find_focus(true, &waiting, &locked_client, &keywind)))
		return;

	DIAG((D_keybd,client,"XA_keyboard_event: %s; update_lock:%d, focus: %s, window_list: %s",
		waiting ? "waiting" : "", update_locked() ? update_locked()->pid : 0,
		c_owner(client), w_owner(keywind)));

	/* Found either (MU_KEYBD|MU_NORM_KEYBD) or keypress handler. */
	if (waiting)
	{
		rk = kmalloc(sizeof(*rk));
		if (rk)
		{
			*rk = *key;
			if (keywind && keywind == client->alert)
				post_cevent(client, cXA_keypress, rk, keywind, 0,0, NULL,NULL);
			else if (client->fmd.keypress)
				post_cevent(client, cXA_fmdkey, rk, NULL, 0,0, NULL, NULL);
			else if (keywind && keywind == client->fmd.wind)
				post_cevent(client, cXA_keypress, rk, keywind, 0,0, NULL,NULL);
			else
			{
				if (keywind && is_hidden(keywind))
					unhide_window(lock, keywind, true);
				if (keywind && keywind->keypress)
					post_cevent(client, cXA_keypress, rk, keywind, 0,0, NULL,NULL);
				else if (client->waiting_pb)
					post_cevent(client, cXA_keybd_event, rk, NULL, 0,0, NULL,NULL);
#if GENERATE_DIAGS
				else
					DIAGS(("XA_keyboard_event: INTERNAL ERROR: No waiting pb."));			
#endif
			}
		}
	}
	else
	{
		/*
		 * We dont queue the key when we are sure the client dont want it
		 */
		if ( !client->waiting_pb )
			queue_key(client, key);
		else
			cancel_keyqueue(client);
	}
}
#if 0
static char *vecnames[] =
{
	"Stack after reset ",
	"PC after reset    ",
	"Bus               ",
	"Address           ",
	"Illegal           ",
	"Div by Zero       ",
	"CHK inst          ",
	"TRAPV             ",
	"Privilege         ",
	"Trace             ",
	"Line-A            ",
	"Line-F            ",
	"reserved          ",
	"FPU prot violation",
	"Format error      ",
	"Uninitialized int ",
	"Reserved          ",
	"Reserved          ",
	"Reserved          ",
	"Reserved          ",
	"Reserved          ",
	"Reserved          ",
	"Reserved          ",
	"Reserved          ",
	"Spurious int      ",
	"Lev 1             ",
	"Lev 2 (HBL)       ",
	"Lev 3             ",
	"Lev 4 (VBL)       ",
	"Lev 5             ",
	"lev 6 (MFP)       ",
	"lev 7 (NMI)       ",
};
#endif

static bool
kernel_key(enum locks lock, struct rawkey *key)
{
#if ALT_CTRL_APP_OPS	
	if ((key->raw.conin.state & (K_CTRL|K_ALT)) == (K_CTRL|K_ALT))
	{
		struct xa_client *client;
		short nk;

		key->norm = nkc_tconv(key->raw.bcon);
		nk = key->norm & 0x00ff;

		DIAG((D_keybd, NULL,"CTRL+ALT+%04x --> %04x '%c'", key->aes, key->norm, nk));

#if 0
//#if GENERATE_DIAGS
		/* CTRL|ALT|number key is emulate wheel. */
		if (   nk=='U' || nk=='N'
		    || nk=='H' || nk=='J')
		{
			short  wheel = 0;
			short  click = 0;
			struct moose_data md = mainmd; //{ 0, 0, 0, 0, 0, 0, 0, 0 };

			md.x = md.sx;
			md.y = md.sy;

			switch (nk)
			{
			case 'U':
				wheel = cfg.ver_wheel_id, click = -1;
				break;
			case 'N':
				wheel = cfg.ver_wheel_id, click = 1;
				break;
			case 'H':
				wheel = cfg.hor_wheel_id, click = -1;
				break;
			case 'J':
				wheel = cfg.hor_wheel_id, click = 1;
				break;
			}

			md.state = wheel;
			md.clicks = click;

			XA_wheel_event(lock, &md);
			return true;
		}
//#endif
#endif

		switch (nk)
		{
		case NK_TAB:				/* TAB, switch menu bars */
		{
			client = next_app(lock, false, false);  /* including the AES for its menu bar. */
			if (client)
			{
				DIAGS(("next_app() :: %s", c_owner(client)));
				app_in_front(lock, client, true, true);
				if (client->type & APP_ACCESSORY)
				{
					send_app_message(lock, NULL, client, AMQ_NORM, QMF_CHKDUP,
							 AC_OPEN, 0, 0, 0,
							 client->p->pid, 0, 0, 0);
				}

			}
			return true;
		}
		case 'R':				/* attempt to recover a hung system */
		{
			recover();
			return true;
		}
		case 'L':				/* open the task manager */
		case NK_ESC:
		{
			open_taskmanager(lock);
			return true;
		}
		case 'T':				/* ctrl+alt+T    Tidy screen */
		case NK_HOME:				/*     "    Home       "     */
		{
			update_windows_below(lock, &screen.r, NULL, window_list, NULL);
			redraw_menu(lock);
		}
		case 'M':				/* ctrl+alt+M  recover mouse */
		{
			forcem();
			return true;
		}
		case 'A':
		{
			struct cfg_name_list *nl = NULL;

			if (!(key->raw.conin.state & (K_RSHIFT|K_LSHIFT)))
				nl = cfg.ctlalta;
			
			DIAGS(("Quit all apps by CtlAlt A"));
			quit_all_clients(lock, nl, NULL);
			return true;
		}
		case 'Q':
		{
			DIAGS(("shutdown by CtlAlt Q"));
			dispatch_shutdown(0);
			return true;
		}
		case 'H':
		{
			DIAGS(("shutdown by CtlAlt H"));
			dispatch_shutdown(HALT_SYSTEM);
			return true;
		}
		case 'Y':				/* ctrl+alt+Y, hide current app. */
		{
			hide_app(lock, focus_owner());
			return true;
		}
		case 'X':				/* ctrl+alt+X, hide all other apps. */
		{
			hide_other(lock, focus_owner());
			return true;
		}
		case 'V':				/* ctrl+alt+V, unhide all. */
		{
			unhide_all(lock, focus_owner());
			return true;
		}
		case 'W':
		{
			struct xa_window *wind;
			wind = next_wind(lock);
			if (wind)
			{
				top_window(lock, true, true, wind, (void *)-1L);
			}
			return true;
		}
	#if 0
		case 'P':
		{
			int i;
			volatile long *vec = NULL;
			display("Vecors...");
			for (i = 0; i < 32; i++)
				display("%s %lx (%d)", vecnames[i], *vec++, i);
			return true;
		}
	#endif		
	#if 0
		case 'P':
		{
			display("set_syspalette");
			set_syspalette(C.vh, screen.palette);
			return true;
		}
		case 'I':
		{
			display("Select image");
			open_imgload(lock);
			return true;
		}
	#endif	
	
	#if GENERATE_DIAGS
		case 'D':				/* ctrl+alt+D, turn on debugging output */
		{
			D.debug_level = 4;
			return true;
		}
	#endif
#if GENERATE_DIAGS
		case 'O':
		{
			cfg.menu_locking ^= true;
			return true;
		}
#endif
		}
	}
#endif
	return false;
}

void
keyboard_input(enum locks lock)
{
	/* Did we get some keyboard input? */
	while (f_instat(C.KBD_dev))
	{
		struct rawkey key;

		key.raw.bcon = f_getchar(C.KBD_dev, RAW);

		/* Translate the BIOS raw data into AES format */
		key.aes = (key.raw.conin.scan<<8) | key.raw.conin.code;
		key.norm = 0;

		DIAGS(("f_getchar: 0x%08lx, AES=%x, NORM=%x", key.raw.bcon, key.aes, key.norm));

		if (!kernel_key(lock, &key))
			XA_keyboard_event(lock, &key);
	}
}
