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

#include "xa_menu.h"
#include "xa_global.h"

#include "k_main.h"
#include "app_man.h"
#include "c_window.h"
#include "menuwidg.h"
#include "obtree.h"
#include "rectlist.h"
#include "taskman.h"
#include "util.h"
#include "widgets.h"

/*
 * This file provides the interface between XaAES's (somewhat strange) menu
 * system and the standard GEM calls. Most GEM apps will only care about the
 * menu they install onto the desktop, although XaAES can allow seperate menus
 * for each window...
 */

/*
 * Install a menu bar onto the desktop window
 */
unsigned long
XA_menu_bar(enum locks lock, struct xa_client *client, AESPB *pb)
{
	bool swap = true;
	XA_TREE *menu_bar;
	XA_TREE *menu = client->std_menu;

	OBJECT *mnu = (OBJECT*)pb->addrin[0];
	struct xa_window *wl;
	struct xa_client *top_owner;

	CONTROL(1,1,1)

	menu_bar = get_menu();

	pb->intout[0] = 0;

	DIAG((D_menu, NULL, "menu_bar for %s, %lx", c_owner(client), mnu));

	switch (pb->intin[0])
	{
	case MENU_INSTL:
	{
		swap = false;
		/* fall through... */
	}
	case MENU_INSTALL:
	{
		DIAG((D_menu,NULL,"MENU_INSTALL"));

		if (validate_obtree(client, mnu, "XA_menu_bar:"))
		{
			XA_TREE *mwt = obtree_to_wt(client, mnu);

			if (!mwt || (mwt && mwt != menu))
			{
				if (!mwt)
					mwt = new_widget_tree(client, mnu);
				
				assert(mwt);
				
				/* Do a special fix on the menu  */
				fix_menu(client, mwt/*mnu*/, root_window, true);
				DIAG((D_menu,NULL,"fixed menu"));

#if GENERATE_DIAGS
				{
					int i = 0;
					while ((mnu[i].ob_flags & OF_LASTOB) == 0)
						i++;
					mwt->lastob = i;
				}
#endif
				/* HR: std_menu is now a complete widget_tree :-) */
				mwt->is_menu = true;
				mwt->menu_line = true;

				if (swap)
				{
					top_owner = get_app_infront();
					if (client == top_owner || !top_owner->std_menu)
						swap_menu(lock|winlist, client, mwt, false, true, 6);
					else
					{
						client->nxt_menu = mwt;
						app_in_front(lock, client, true, true);
					}
				}

				pb->intout[0] = 1;
				DIAG((D_menu, NULL, "done display, lastob = %d", mwt->lastob));
			}
			else if (mwt && swap)
			{
				top_owner = get_app_infront();
				wt_menu_area(mwt);
				if (client == top_owner || !top_owner->std_menu)
					swap_menu(lock|winlist, client, NULL, false, true, 7);
				else
				{
					client->nxt_menu = mwt;
					app_in_front(lock, client, true, true);
				}
			}
		}
		break;
	}
	case MENU_REMOVE:
	{
		DIAG((D_menu,NULL,"MENU_REMOVE"));
		
		if (!menu)
			menu = client->nxt_menu;

		if (menu)
		{
			if (menustruct_locked() == client->p)
				popout(TAB_LIST_START);

			client->std_menu = client->nxt_menu = NULL;
			
			top_owner = C.Aes;
			wl = window_list;
			while (wl)
			{
				if (   wl->owner != client
				    && wl->owner->std_menu)
				{
					top_owner = wl->owner;
					break;
				}
				wl = wl->next;
			}
			swap_menu(lock|winlist, top_owner, NULL, false, true, 7);
			remove_attachments(lock|winlist, client, menu);
			
			pb->intout[0] = 1;
		}
		break;
	}
	case MENU_INQUIRE:
	{
		DIAG((D_menu, NULL, "MENU_INQUIRE := %d", menu_bar->owner->p->pid));
		
		pb->intout[0] = menu_bar->owner->p->pid;
		break;
	}
	}

	DIAG((D_menu,NULL,"done menu_bar()"));
	return XAC_DONE;
}

static void
upd_menu(enum locks lock, struct xa_client *client, OBJECT *tree, short item, bool redraw)
{
	XA_TREE *wt;

	wt = obtree_to_wt(client, tree);
	if (wt && tree[item].ob_type == G_TITLE)
	{
		wt_menu_area(wt);
		if (wt == get_menu())
		{
			set_rootmenu_area(client);
			if (redraw)
				redraw_menu(lock);
		}
	}
}
	
/*
 * Highlight / un-highlight a menu title
 * - Actually, this isn't really needed as XaAES cancels the highlight itself...
 * ...it's only here for compatibility. 
 */
unsigned long
XA_menu_tnormal(enum locks lock, struct xa_client *client, AESPB *pb)
{
	OBJECT *tree = (OBJECT *)pb->addrin[0];
	short i = pb->intin[0], obj, state;
	
	CONTROL(2,1,1)

	if (!validate_obtree(client, tree, "XA_menu_tnormal:"))
		return XAC_DONE;

	obj = i & ~0x8000;
	
	state = tree[obj].ob_state;
	
	if (state & OS_DISABLED)
	{
		pb->intout[0] = 0;
	}
	else
	{
		DIAG((D_menu, client, "menu_tnormal: tree=%lx, obj=%d(%d), state=%d",
			tree, obj, i, pb->intin[1]));

		if (pb->intin[1])
			state &= ~OS_SELECTED;
		else
			state |= OS_SELECTED;

		if (tree[obj].ob_state != state)
		{
			tree[obj].ob_state = state;
			upd_menu(lock, client, tree, obj, true);
		}
		pb->intout[0] = 1;
	}
	return XAC_DONE;
}

/*
 * Enable/Disable a menu item
 */
unsigned long
XA_menu_ienable(enum locks lock, struct xa_client *client, AESPB *pb)
{
	OBJECT *tree = (OBJECT *)pb->addrin[0];
	short i = pb->intin[0], obj, state;
	bool redraw;
	
	CONTROL(2,1,1)
	
	if (!validate_obtree(client, tree, "XA_menu_ienable:"))
		return XAC_DONE;
	
	redraw = i & 0x8000 ? true : false;
	obj = i & ~0x8000;
	state = tree[obj].ob_state;

	DIAG((D_menu, client, "menu_ienable: tree=%lx, obj=%d(%d), state=%d",
		tree, obj, i, pb->intin[1]));
	
	/* Change the disabled status of a menu item */
	if (pb->intin[1])
		state &= ~OS_DISABLED;
	else
		state |= OS_DISABLED;

	if (tree[obj].ob_state != state)
	{
		tree[obj].ob_state = state;
		upd_menu(lock, client, tree, obj, redraw);
	}
	
	pb->intout[0] = 1;
	return XAC_DONE;
}

/*
 * Check / un-check a menu item
 */
unsigned long
XA_menu_icheck(enum locks lock, struct xa_client *client, AESPB *pb)
{
	OBJECT *tree = (OBJECT *)pb->addrin[0];
	short i = pb->intin[0], obj, state;

	CONTROL(2,1,1)

	if (!validate_obtree(client, tree, "XA_menu_icheck:"))
		return XAC_DONE;

	obj = i & ~0x8000;
	state = tree[obj].ob_state;
	
	DIAG((D_menu, client, "menu_icheck: tree=%lx, obj=%d(%d), state=%d",
		tree, obj, i, pb->intin[1]));
	/* Change the disabled status of a menu item */
	if (pb->intin[1])
		state |= OS_CHECKED;
	else
		state &= ~OS_CHECKED;

	if (tree[obj].ob_state != state)
	{
		tree[obj].ob_state = state;
		upd_menu(lock, client, tree, obj, false);
	}
	
	pb->intout[0] = 1;
	
	return XAC_DONE;
}

/*
 * Change a menu item's text
 */
unsigned long
XA_menu_text(enum locks lock, struct xa_client *client, AESPB *pb)
{
	const char *text = (const char *)pb->addrin[1];
	OBJECT *tree = (OBJECT *)pb->addrin[0];
	short i = pb->intin[0], obj;

	CONTROL(1,1,2)

	if (!validate_obtree(client, tree, "XA_menu_text:"))
		return XAC_DONE;
	
	obj = i & ~0x8000;
	
	strcpy(object_get_spec(&tree[obj])->free_string, text);

	upd_menu(lock, client, tree, obj, false);
	
	pb->intout[0] = 1;
	
	return XAC_DONE;
}

/*
 * Register an apps 'pretty' & 'official' names.
 */
unsigned long
XA_menu_register(enum locks lock, struct xa_client *client, AESPB *pb)
{
	const char *n = (const char *)pb->addrin[0];

	CONTROL(1,1,1)

	if (n)
	{
		pb->intout[0] = client->p->pid;

		if (pb->intin[0] != -1)
		{
			int l;

			l = strlen(n);
			if (l >= NICE_NAME)
			{
				strncpy(client->name, n, NICE_NAME-1);
				*(client->name + NICE_NAME - 1) = 0;
			}
			else
				strcpy(client->name, n);

			/* refresh the name change in the taskmanager */
			update_tasklist(lock);

			DIAGS(("menu_register 'nice' for %d: '%s'",
				client->p->pid, client->name));
		}
		else
		{
			int f;

			/* copy over */
			strncpy(client->proc_name, n, 8);
			client->proc_name[8] = '\0';

			/* fill with space */
			for (f = strlen(client->proc_name); f < 8; f++)
				client->proc_name[f] = ' ';

			/* uppercase */
			strnupr(client->proc_name, 8);

			/* refresh the name change in the taskmanager */
			update_tasklist(lock);

			DIAGS(("menu_register 'proc' for %d: '%s'",
				client->p->pid, client->proc_name));
		}
	}
	else
		pb->intout[0] = -1;

	return XAC_DONE;
}

int
menu_popup(enum locks lock, struct xa_client *client, MENU *mn, MENU *result, short px, short py, bool usr_evnt)
{
	if (mn && result)
	{
		Tab *tab;
		short x, y;
		OBJECT *ob;

		if (TAB_LIST_START)
			popout(TAB_LIST_START);

		tab = nest_menutask(NULL);
		ob = mn->mn_tree;
		
		if (tab && validate_obtree(client, ob, "_menu_popup:"))		/* else already locked */
		{	
			XA_TREE *wt;
			short old_x, old_y;

			wt = obtree_to_wt(client, ob);
			if (!wt)
				wt = new_widget_tree(client, ob);
			if (!wt)
				return 0;

			*result = *mn;

			DIAG((D_menu,NULL,"_menu_popup %lx + %d",ob, mn->mn_menu));

		//	tab->pb = NULL;
			tab->locker = client->p->pid;
			tab->client = client;
			tab->lock = lock;
			
			old_x = ob->ob_x;
			old_y = ob->ob_y;

			ob->ob_x = ob->ob_y = wt->dx = wt->dy = 0;
			obj_offset(wt, mn->mn_menu, &x, &y);
			tab->wind = NULL;
			tab->widg = NULL;
			tab->ty = POP_UP;
			tab->scroll = mn->mn_scroll != 0;
			
			tab->usr_evnt = usr_evnt;
			tab->data = result;
			
			start_popup_session(tab, wt, mn->mn_menu,
				 click_popup_entry,
				 px - x,
				 py - y);

			client->status |= CS_BLOCK_MENU_NAV;
			Block(client, 1);
			client->status &= ~CS_BLOCK_MENU_NAV;

			ob->ob_x = old_x;
			ob->ob_y = old_y;
			return 1;
		}
	}
	return 0;
}


/*
 * Display and handle a popup menu.  HR: march 2000
 */

/* Ozk 180603: Absolutely hair-raising usage of Psemaphore across
 * different PIDs! I removed the LOCKSCREEN from the Ktab flags,
 * since the lock_screen() call from XA_handler was done under the
 * clients pid, while the unlock_screen() happens under the kernel
 * pid. Guess the results - total LOCKUP until offending client killed!
*/ 
unsigned long
XA_menu_popup(enum locks lock, struct xa_client *client, AESPB *pb)
{
	CONTROL(2,1,2)

	MENU *result = (MENU *)pb->addrin[1];
	
	if (menu_popup(lock, client, (MENU *)pb->addrin[0], result, pb->intin[0], pb->intin[1], 1))
		pb->intout[0] = result->mn_item < 0 ? 0 : 1;
	else
		pb->intout[0] = -1;

	return XAC_DONE;
#if 0		
	pb->intout[0] = -1;

	if (pb->addrin[0] && pb->addrin[1])
	{
		Tab *tab;
		MENU *mn, *md;
		short x, y;
		OBJECT *ob;

		if (TAB_LIST_START)
			popout(TAB_LIST_START);

		tab = nest_menutask(NULL);
		mn = (MENU*)pb->addrin[0];
		md = (MENU*)pb->addrin[1];
		ob = mn->mn_tree;
		
		if (tab)		/* else already locked */
		{	
			XA_TREE *wt;
			short old_x, old_y;

			wt = obtree_to_wt(client, ob);
			if (!wt)
				wt = new_widget_tree(client, ob);
			if (!wt)
				return XAC_DONE;

			*md = *mn;

			DIAG((D_menu,NULL,"menu_popup %lx + %d",ob, mn->mn_menu));

			tab->pb = pb;
			tab->locker = client->p->pid;
			tab->client = client;
			tab->lock = lock;
			
			old_x = ob->ob_x;
			old_y = ob->ob_y;

			ob->ob_x = ob->ob_y = wt->dx = wt->dy = 0;
			obj_offset(wt, mn->mn_menu, &x, &y);
			tab->wind = NULL;
			tab->widg = NULL;
			tab->ty = POP_UP;
			tab->scroll = mn->mn_scroll != 0;
			
			start_popup_session(tab, wt, mn->mn_menu,
				 click_popup_entry,
				 pb->intin[0] - x,
				 pb->intin[1] - y);

			client->status |= CS_BLOCK_MENU_NAV;
			Block(client, 1);
			client->status &= ~CS_BLOCK_MENU_NAV;
			
			ob->ob_x = old_x;
			ob->ob_y = old_y;
			return XAC_DONE;
		}
	}
 	return XAC_DONE;
#endif
}

/*
 * HR 051101: MagiC popup function.
 */
unsigned long
XA_form_popup(enum locks lock, struct xa_client *client, AESPB *pb)
{
	OBJECT *ob = (OBJECT*)pb->addrin[0];
	CONTROL(2,1,1)

	pb->intout[0] = -1;
	
	if (validate_obtree(client, ob, "XA_form_popup:"))
	{
		Tab *tab;

		if (TAB_LIST_START)
			popout(TAB_LIST_START);

		tab = nest_menutask(NULL);

		if (tab)		/* else already locked */
		{
			XA_TREE *wt;
			short x, y, old_x, old_y;

			wt = obtree_to_wt(client, ob);
			if (!wt)
				wt = new_widget_tree(client, ob);
			if (!wt)
				return XAC_DONE;

			DIAG((D_menu,NULL,"form_popup %lx",ob));

		//	tab->pb = pb;
			tab->locker = client->p->pid;
			tab->client = client;
			tab->lock = lock;
			tab->wind = NULL;
			tab->widg = NULL;
			tab->ty = POP_UP;
			tab->scroll = false;
			tab->data = &pb->intout[0];
			tab->usr_evnt = 1;

			old_x = ob->ob_x;
			old_y = ob->ob_y;

			x = pb->intin[0];
			y = pb->intin[1];
			if (x == 0 && y == 0)
			{
				x = ob->ob_x;
				y = ob->ob_y;
			}
			else
			{
				x -= ob->ob_width/2;
				y -= ob->ob_height/2;
			}
			if (x < 4)
				x = 4;
			if (y < get_menu_widg()->r.h)
				y = get_menu_widg()->r.h;
			ob->ob_x = 0;
			ob->ob_y = 0;

			start_popup_session(tab, wt, 0,
				 click_form_popup_entry,
				 x,
				 y);

			client->status |= CS_BLOCK_MENU_NAV;
			Block(client, 1);
			client->status &= ~CS_BLOCK_MENU_NAV;

			ob->ob_x = old_x;
			ob->ob_y = old_y;
			return XAC_DONE;
		}
	}
	return XAC_DONE;
}

/*
 * Attach a submenu to a menu item.  HR: march 2000
 */
unsigned long
XA_menu_attach(enum locks lock, struct xa_client *client, AESPB *pb)
{
	CONTROL(2,1,2)

	DIAG((D_menu, client, "menu_attach %d", pb->intin[0]));

	pb->intout[0] = 0;

	if (validate_obtree(client, (OBJECT *)pb->addrin[0], "XA_menu_attach:") && pb->addrin[1])
	{
		XA_TREE *wt;
		MENU *mn;
		XAMENU xamn;

		wt = obtree_to_wt(client, (OBJECT *)pb->addrin[0]);
		if (!wt)
			wt = new_widget_tree(client, (OBJECT *)pb->addrin[0]);
		assert(wt);
		
		switch (pb->intin[0])
		{
		case ME_ATTACH:
		{
			mn = (MENU *)pb->addrin[1];

			if (is_attach(client, wt, pb->intin[1], NULL))
				detach_menu(lock, client, wt, pb->intin[1]);

			if (mn)
			{
				xamn.wt = obtree_to_wt(client, mn->mn_tree);
				if (!xamn.wt)
					xamn.wt = new_widget_tree(client, mn->mn_tree);

				assert(xamn.wt);

				xamn.menu = *mn;

				pb->intout[0] = attach_menu(lock,
							    client,
							    wt,
							    pb->intin[1],
							    &xamn);
			}
			break;
		}
		case ME_REMOVE:
		{
			pb->intout[0] = detach_menu(lock,
						    client,
						    wt,
						    pb->intin[1]);
			break;
		}
		case ME_INQUIRE:
		{
			mn = (MENU *)pb->addrin[1];
			pb->intout[0] = inquire_menu(lock,
						     client,
						     wt,
						     pb->intin[1],
						     &xamn);
			
			*mn = xamn.menu;
			break;
		}
		}
	}
	else
		pb->intout[0] = 1;

 	return XAC_DONE;
}

/*
 * Align a submenu.
 */
unsigned long
XA_menu_istart(enum locks lock, struct xa_client *client, AESPB *pb)
{
	CONTROL(1,1,1)

	DIAG((D_menu,client,"menu_istart"));

	pb->intout[0] = 0;
 	return XAC_DONE;
}

/*
 * Influence behaviour.
 */
unsigned long
XA_menu_settings(enum locks lock, struct xa_client *client, AESPB *pb)
{
	CONTROL(1,1,1)

	DIAG((D_menu,client,"menu_settings %d", pb->intin[0]));

	/* accepted, no implementation planned */
	pb->intout[0] = 1;
	if (pb->intin[0] == 0)
	{
		MN_SET *mn = (MN_SET*)pb->addrin[0];
		mn->display = 200;
		mn->drag = 10000;
		mn->delay = 250;
		mn->speed = 0;
		mn->height = root_window->wa.h/screen.c_max_h;
	}
	return XAC_DONE;
}
