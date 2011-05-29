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
#include "xa_strings.h"

//#include "debug.h"
#include "about.h"
#include "app_man.h"
#include "xa_appl.h"
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
#include "menuwidg.h"

#include "obtree.h"

#include "xa_form.h"
#include "xa_rsrc.h"
#include "xa_shel.h"
#include "trnfm.h"
#include "keycodes.h"

#include "mint/dcntl.h"
#include "mint/fcntl.h"
#include "mint/ioctl.h"
#include "mint/signal.h"
#include "mint/ssystem.h"

short key_conv( struct xa_client *client, short key )
{
	switch( key )
	{
		case SC_INSERT:
			if( client->options.insert_key )
				key = client->options.insert_key;
		break;
		case SC_SPACE:
			if( client->options.space_key )
				key = client->options.space_key;
		break;
	}

	return key;
}

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
	client->kq_count = 0;
}

//#include "mint/filedesc.h"
void
queue_key(struct xa_client *client, const struct rawkey *key)
{
	struct keyqueue *kq;
	DIAGS(("queue key for %s", client->proc_name));
	if (client->kq_count < MAX_KEYQUEUE)
	{
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
			client->kq_count++;
		}
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

		DIAGS(("unqueue key for %s", client->proc_name));

		if (client->kq_tail == client->kq_head)
			client->kq_tail = client->kq_head = NULL;
		else
			client->kq_tail = kq->next;

		client->kq_count--;
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
	bool waiting, chlp_lock = update_locked() == C.Hlp->p;

	if (!chlp_lock && TAB_LIST_START)
	{
		rk = kmalloc(sizeof(*rk));
		if (rk)
		{
			*rk = *key;
			post_cevent(TAB_LIST_START->client, cXA_menu_key, rk, NULL, 0,0, NULL,NULL);
		}
		return;
	}

	if (!(client = find_focus(true, &waiting, &locked_client, &keywind)))
	{
// 		display("no focus?!");
		return;
	}

	DIAG((D_keybd,client,"XA_keyboard_event: %s; update_lock:%d, focus: %s, window_list: %s",
		waiting ? "waiting" : "", update_locked() ? update_locked()->pid : 0,
		c_owner(client), keywind ? w_owner(keywind) : "no keywind"));


// 	display("XA_keyboard_event: %s; update_lock:%d, focus: %s, window_list: %s, waiting_pb=%lx",
// 		waiting ? "waiting" : "", update_locked() ? update_locked()->pid : 0,
// 		client->name, keywind ? keywind->owner->name : "no keywind", client->waiting_pb);

	/* Found either (MU_KEYBD|MU_NORM_KEYBD) or keypress handler. */
	if (waiting)
	{
		rk = kmalloc(sizeof(*rk));
		if (rk)
		{
			*rk = *key;
			rk->norm = nkc_tconv(key->raw.bcon);

			if (keywind && keywind == client->alert)
			{
				post_cevent(client, cXA_keypress, rk, keywind, 0,0, NULL,NULL);
			}
			else if (client->fmd.keypress)
			{
				post_cevent(client, cXA_fmdkey, rk, NULL, 0,0, NULL, NULL);
			}
			else if (keywind && keywind == client->fmd.wind)
			{
				post_cevent(client, cXA_keypress, rk, keywind, 0,0, NULL,NULL);
			}
			else
			{
				if (keywind && is_hidden(keywind))
					unhide_window(lock, keywind, true);
				if (keywind && keywind->keypress)
				{
					post_cevent(client, cXA_keypress, rk, keywind, 0,0, NULL,NULL);
				}
				else if (client->waiting_pb)
				{
					post_cevent(client, cXA_keybd_event, rk, NULL, 0,0, NULL,NULL);
				}
#if GENERATE_DIAGS
				else
					DIAGS(("XA_keyboard_event: INTERNAL ERROR: No waiting pb."));
#endif
			}
		}
	}
	else
	{
		/*!!!TEST!!!*/
		queue_key(client, key);
		/*
		 * We dont queue the key when we are sure the client dont want it
		 */
		/*if ( !client->waiting_pb )
			queue_key(client, key);
		else
			cancel_keyqueue(client);
			*/
	}
}

/*
 * md=-2: off if was -1
 * md=-1: on temp.
 * else: set md
 */
void toggle_menu(enum locks lock, short md)
{
	struct xa_client *client = find_focus(true, NULL, NULL, 0);

	if( !client )
		return;
	if( md == -2 )
	{
		if( cfg.menu_bar == -1 )
			md = 0;
		else return;
	}
	if( md != -1 ){
		if( cfg.menu_bar == md )
		{
			redraw_menu(lock);
			return;
		}
		cfg.menu_bar = md;
	}
	else
		if( ++cfg.menu_bar > 1 )
			cfg.menu_bar = 0;

	set_standard_point( client );
	if( cfg.menu_bar )
	{
		redraw_menu(lock);
	}
	else
	{
		RECT r = screen.r;
		popout(TAB_LIST_START);
		r.h = 20;	//screen.c_max_h + 2;	// widget-height
		update_windows_below(lock, &r, NULL, window_list, NULL);
	}
	generate_redraws(lock, root_window, &screen.r, RDRW_ALL);

}


/******************************************************************************
 from "unofficial XaAES":

+----------------------------------------------------------------------------------------------------------------------------+
|Key combo             |Action                                                                                               |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+CLRHOME      |Redraw screen                                                                                        |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+A            |Terminates all applications (A list of exceptions can be specified)                                  |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+D            |Open the screenshot dialog (XAAESNAP.PRG is required. See 6.6.2)                                     |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+H            |Halt the system                                                                                      |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+L            |Open task manager                                                                                    |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+P            |Restore palette in colour depth of 8-bits or less                                                    |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+Q            |Quit XaAES                                                                                           |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+R            |Change resolution                                                                                    |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+V            |Unhide all applications                                                                              |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+W            |Global window cycling                                                                                |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+X            |Hide all except the currently focused application                                                    |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+Y            |Hide currently focused application                                                                   |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+SPACE        |Open main menu bar                                                                                   |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+SHIFT+SPACE  |Open menu in current window if it has one, else open main menu bar                                   |
|----------------------+-----------------------------------------------------------------------------------------------------|
|CTRL+ALT+TAB          |Cycle open applications                                                                              |
+----------------------------------------------------------------------------------------------------------------------------+
********************************************************************************/
static bool
kernel_key(enum locks lock, struct rawkey *key)
{
#if ALT_CTRL_APP_OPS
	if ((key->raw.conin.state & (K_CTRL|K_ALT)) == (K_CTRL|K_ALT))
	{
		struct xa_client *client;
		struct kernkey_entry *kkey = C.kernkeys;
		short nk;
		short sdmd = 0;

		key->norm = nkc_tconv(key->raw.bcon);
		nk = key->norm & 0x00ff;

		DIAG((D_keybd, NULL,"CTRL+ALT+%04x --> %04x '%c'", key->aes, key->norm, nk ));

		while (kkey)
		{
			if (kkey->key == nk)
				break;
			kkey = kkey->next_key;
		}
		if (kkey)
		{
			if (kkey->act)
			{
				post_cevent(C.Hlp, ceExecfunc, kkey->act, NULL, 1,1, NULL, NULL);
			}
			return true;
		}
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

		if( (nk == cfg.keyboards.c && (key->raw.conin.state & (K_RSHIFT|K_LSHIFT))) || (tolower(nk) == cfg.keyboards.c) )
		{
			static short kbdnum = 0;
			unsigned long dummy;
			char tblpath[PATH_MAX];
			long out;
			char *tbname = cfg.keyboards.keyboard[kbdnum++];

			if( !tbname )
			{
				kbdnum = 0;
				tbname = cfg.keyboards.keyboard[kbdnum++];
			}
			if( !tbname )
				return true;

			sprintf( tblpath, sizeof(tblpath), "%s%s.tbl", sysdir, tbname );

			out = s_system(S_LOADKBD, (unsigned long)tblpath, (unsigned long)&dummy);
			if( out )
			{
				ALERT((xa_strings[AL_KBD]/*"keyboard-table not loaded"*/, tbname, sysdir, out));
			}
			return true;
		}
		switch (nk)
		{
		case NK_TAB:				/* TAB, switch menu bars */
		{
			client = next_app(lock, false, false);  /* including the AES for its menu bar. */
			app_or_acc_in_front( lock, client );
			return true;
		}
		case '0':	// toggle main-menubar
			toggle_menu(lock, -1);
		return true;
		//case NK_ESC:
		case ' ':
		{
			struct xa_window *wind;
			struct xa_widget *widg;
			short mb = cfg.menu_bar;

//			if (nk == NK_ESC && !TAB_LIST_START)
//				goto otm;

			client = NULL;
			widg = NULL;

			if (key->raw.conin.state & (K_RSHIFT|K_LSHIFT))
			{
				client = find_focus(true, NULL, NULL, &wind);
				if (client)
				{
					if (wind)
					{
						widg = get_widget(wind, XAW_MENU);
						if (!wdg_is_act(widg))
							client = NULL;
					}
					else
						client = NULL;
				}
			}
			else
				toggle_menu(lock, 1);

			if (!client)
			{
				if (TAB_LIST_START)
				{
					client = TAB_LIST_START->client;
					widg = TAB_LIST_START->widg;
					wind = TAB_LIST_START->wind;
				}
				else
				{
					widg = get_menu_widg();
					client = menu_owner();
					wind = root_window;
				}
			}
			if (client)
				post_cevent(client, cXA_open_menubykbd, wind,widg, 0,0, NULL,NULL);
			if( !mb )
				cfg.menu_bar = -1;
			return true;
		}
#if GENERATE_DIAGS == 0
		case 'D':	/* screen-dump */
		{
			short mode;
			if (key->raw.conin.state & (K_RSHIFT|K_LSHIFT))
				mode = 0;
			else
				mode = 1;
			post_cevent(C.Hlp, ceExecfunc, screen_dump, NULL, mode,0, NULL,NULL);

			return true;
		}
#endif
		case 'R':				/* attempt to recover a hung system */
		{
			if ((key->raw.conin.state & (K_RSHIFT|K_LSHIFT)))
			{
				if (C.reschange )
					post_cevent(C.Hlp, ceExecfunc, C.reschange, NULL, 1,0, NULL, NULL);
			}
			else
			{
				recover();
			}
			return true;
		}
		case 'F':				/* open the task manager */
#if !GENERATE_DIAGS
		case 'L':				/* open the task manager */
#endif
		if( !C.update_lock )
		{
//otm:
			post_cevent(C.Hlp, ceExecfunc, open_taskmanager,NULL, 1,0, NULL,NULL);
		}
		return true;

		case 'I':	/* (un-)iconify window */
			return 	iconify_action(lock, TOP_WINDOW, 0);
		case NK_INS:
			TOP_WINDOW->send_message(lock, TOP_WINDOW, NULL, AMQ_NORM, QMF_CHKDUP,
			   WM_FULLED, 0, 0, TOP_WINDOW->handle, 0, 0, 0, 0);
		return true;

#define WGROW	16
		case NK_UP:
		case NK_DOWN:
			if( TOP_WINDOW )
			{
				struct xa_window *wind = TOP_WINDOW;
				short g = WGROW, s = key->raw.conin.state & (K_RSHIFT|K_LSHIFT);
				RECT r = wind->r;
				if( nk == NK_DOWN )
					g = -g;
				g *= 2;
				r.y -= g;
				if( !s )
				{
					if( wind->dial & (created_for_ALERT | created_for_FORM_DO | created_for_WDIAL) || (wind->window_status & (XAWS_ICONIFIED | XAWS_HIDDEN)) )
						return true;
					r.x -= g;
					r.w += g * 2;
					r.h += g * 2;
					if( nk == NK_UP )
					{
						if (r.x < 0)
							r.x = 0;
						if (r.y < 0)
							r.y = 0;
						if( r.y + r.h > screen.r.h )
							r.h = screen.r.h - r.y;
						if( r.x + r.w > screen.r.w )
							r.w = screen.r.w - r.x;
					}
					else if( nk == NK_DOWN )
					{
						if( r.w < WGROW * 8 )
						{
							r.x = wind->r.x;
							r.w = wind->r.w;
						}
						if( r.h < WGROW * 10 )
						{
							r.y = wind->r.y;
							r.h = wind->r.h;
						}
					}
				}
				if( r.y >= screen.r.h )
					r.y = screen.r.h - WGROW;
				if( r.y < root_window->wa.y )
					r.y = root_window->wa.y;
				if( memcmp(&r, &wind->r, sizeof(RECT)) )
				{
					wind->send_message(lock, wind, NULL, AMQ_NORM, QMF_CHKDUP,
							   s ? WM_MOVED : WM_SIZED, 0, 0, wind->handle,
							   r.x, r.y, r.w, r.h);
					/* resize wdialogs */
					/*if( !s && wind->do_message == NULL || (wind->dial & created_for_WDIAL) )
					{
						short msg[8] = {WM_SIZED, 0, 0, 0, r.x, r.y, r.w, r.h};
						do_formwind_msg(wind, client, 0, 0, msg);
					}*/
				}
			}
		return true;
		case NK_RIGHT:
		case NK_LEFT:
			if( TOP_WINDOW )
			{
				struct xa_window *wind = TOP_WINDOW;
				short s = key->raw.conin.state & (K_RSHIFT|K_LSHIFT);
				RECT r = wind->r;
				if( !s )
				{
					short g = WGROW;

					if( wind->dial & (created_for_ALERT | created_for_FORM_DO | created_for_WDIAL) || (wind->window_status & (XAWS_ICONIFIED | XAWS_HIDDEN)) )
						return true;
					if( nk == NK_LEFT )
						g = -g;
					r.x -= g;
					r.w += g * 2;
					wind->send_message(lock, wind, NULL, AMQ_NORM, QMF_CHKDUP,
					   WM_SIZED, 0, 0, wind->handle,
					   r.x, r.y, r.w, r.h);
					/*if( (wind->do_message == NULL || (wind->dial & created_for_WDIAL)) )
					{
						short msg[8] = {WM_SIZED, 0, 0, 0, r.x, r.y, r.w, r.h};
						do_formwind_msg(wind, client, 0, 0, msg);
					}*/
				}
				else
				{
					if( nk == NK_RIGHT )
					{
						r.x += WGROW;
						if( r.x >= screen.r.w )
							return true;
					}
					else{
						r.x -= WGROW;
						if( r.x + wind->r.w <= 0 )
							return true;
					}
					wind->send_message(lock, wind, NULL, AMQ_NORM, QMF_CHKDUP,
						   WM_MOVED, 0, 0, wind->handle,
						   r.x, r.y, r.w, r.h);
				}
			}
		return true;
		case 'E':	/* open windows-submenu on top-window */

		if( TOP_WINDOW && !C.update_lock )
		{
			struct moose_data md;

			/* init moose_data */
			memset( &md, 0, sizeof(md) );
			md.x = TOP_WINDOW->r.x + TOP_WINDOW->r.w/2;
			md.y = TOP_WINDOW->r.y + TOP_WINDOW->r.h/2;
			md.state = 1;//MBS_RIGHT;
			post_cevent(C.Hlp, CE_winctxt, TOP_WINDOW, NULL, 0,0, NULL, &md);
		}
		return true;
		case 'B':	/* system-window ('S' eaten by MiNT?)*/
		if( !C.update_lock )
		{
			post_cevent(C.Hlp, ceExecfunc, open_systemalerts,NULL, 1, 0, NULL,NULL);
		}
		return true;
		case NK_HELP:	/* about-window */
		if( !C.update_lock )
		{
			post_cevent(C.Hlp, ceExecfunc, open_about,NULL, 1,0, NULL,NULL);
		}
		return true;
		case 'K':	/* launcher */
		if( !C.update_lock )
		{
			post_cevent(C.Hlp, ceExecfunc, open_launcher,NULL, 1,0, NULL,NULL);
		}
		return true;
		case 'T':				/* ctrl+alt+T    Tidy screen */
		case NK_HOME:				/*     "    Home       "     */
		{
			update_windows_below(lock, &screen.r, NULL, window_list, NULL);
			redraw_menu(lock);
			return true;
		}
		case 'M':				/* ctrl+alt+M  recover mouse */
		{
			forcem();
			return true;
		}
#if HOTKEYQUIT
		case 'A':
		{
			DIAGS(("Quit all clients by CtlAlt A"));
			post_cevent(C.Hlp, ceExecfunc, ce_quit_all_clients, NULL, !(key->raw.conin.state & (K_RSHIFT|K_LSHIFT)),0, NULL, NULL);
			return true;
		}
#endif
		case 'P':
		{
			DIAGS(("Recover palette"));
			if (screen.planes <= 8)
			{
				set_syspalette( C.Aes->vdi_settings->handle, screen.palette);
			}
			return true;
		}
#if HOTKEYQUIT
		case 'J':
			sdmd = RESTART_XAAES;
		case 'H':
			if( !sdmd )
				sdmd = HALT_SYSTEM;
		case 'G':
		case 'Q':
		{
			struct proc *p;
			const char *sdmaster = get_env(0, "SDMASTER=");

			if (sdmaster)
			{
				int ret = create_process(sdmaster, NULL, NULL, &p, 0, NULL);
				if (ret < 0)
					ALERT((xa_strings[AL_SDMASTER], sdmaster));
				else
					return true;
			}
			else	/* the ALERT comes after ce_dispatch_shutdown, so the else must stay for now */
			{
				post_cevent(C.Hlp, ceExecfunc, ce_dispatch_shutdown, NULL, sdmd,1, NULL, NULL);
			}

			return true;
		}
#endif
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
		case 'U':
		{
			struct xa_window *wind;
			wind = TOP_WINDOW;
			if( wind )
			{
				wind->send_message(lock, wind, NULL, AMQ_NORM, QMF_CHKDUP,
							   WM_CLOSED, 0, 0, wind->handle, 0,0,0,0);
			}
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
				top_window(lock, true, true, wind);
			}
			return true;
		}
#if GENERATE_DIAGS
		case 'L':				/* ctrl+alt+L, turn on debugging output */
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
#endif	/*/ALT_CTRL_APP_OPS	*/
	return false;
}

void
keyboard_input(enum locks lock)
{
	/* Did we get some keyboard input? */
#if EIFFEL_SUPPORT
	while (1)
#endif
	{
		struct rawkey key;

		key.raw.bcon = f_getchar(C.KBD_dev, RAW);
// 		display("f_getchar: 0x%08lx, AES=%x, NORM=%x", key.raw.bcon, key.aes, key.norm);

	// this produces wheel-events on some F-keys (eg. S-F10)
#if EIFFEL_SUPPORT
		if ( !key.raw.conin.state && cfg.eiffel_support && eiffel_wheel((unsigned short)key.raw.conin.scan & 0xff))
		{
			if( f_instat(C.KBD_dev) )
				continue;
			else
				break;
		}
#endif
		/* Translate the BIOS raw data into AES format */
		key.aes = (key.raw.conin.scan << 8) | key.raw.conin.code;
		key.norm = 0;

// 		display("f_getchar: 0x%08lx, AES=%x, NORM=%x", key.raw.bcon, key.aes, key.norm);

		if (kernel_key(lock, &key) == false )
		{
			XA_keyboard_event(lock, &key);
		}
#if EIFFEL_SUPPORT
		break;
#endif
	}
}
