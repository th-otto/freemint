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

#include "xa_types.h"
#include "xa_global.h"

#include "c_window.h"
#include "rectlist.h"

#define max(x,y) (((x)>(y))?(x):(y))
#define min(x,y) (((x)<(y))?(x):(y))

struct xa_rect_list *
make_rect_list(struct xa_window *wind, bool swap)
{
	struct xa_window *wl;
	struct xa_rect_list *rl, *nrl, *rl_next, *rl_prev;
	RECT r_ours, r_win;

	if (swap)
	{
		DIAGS(("Freeing old rect_list for %d", wind->handle));
		free_rect_list(wind->rect_start);
		wind->rect_list = wind->rect_user = wind->rect_start = NULL;
	}

	DIAGS(("make_rect_list for wind %d", wind->handle));

	nrl = kmalloc(sizeof(*nrl));
	assert(nrl);
	nrl->next = NULL;
	nrl->r = wind->r;

	if (nrl && wind->prev)
	{
		short flag, win_x2, win_y2, our_x2, our_y2;
		short w, h;
#if GENERATE_DIAGS
		short i;
#endif

		wl = wind->prev;

		while (wl)
		{
#if GENERATE_DIAGS
			i = 0;
#endif
			rl_prev = NULL;

			for (rl = nrl; rl; rl = rl_next)
			{

				r_win = wl->r;		
				r_ours = rl->r;

				win_x2 = r_win.x + r_win.w;
				win_y2 = r_win.y + r_win.h;

				flag = 0;

				h = r_win.y - r_ours.y;
				w = r_win.x - r_ours.x;

				DIAGS((" -- nrl=%lx, rl_prev=%lx", nrl, rl_prev));
				DIAGS((" -- [%d] ours=(%d/%d/%d/%d), wind %d = (%d/%d/%d/%d)", i, r_ours, wl->handle, r_win));
				DIAGS((" --      win2=%d/%d, w=%d, h=%d",
					win_x2, win_y2, w, h));
#if GENERATE_DIAGS
				i++;
#endif

				if (   h < r_ours.h
				    && w < r_ours.w
				    && win_x2 > r_ours.x
				    && win_y2 > r_ours.y)
				{
					our_x2 = r_ours.x + r_ours.w;
					our_y2 = r_ours.y + r_ours.h;

					if (r_win.y > r_ours.y)
					{
						rl->r.x = r_ours.x;
						rl->r.y = r_ours.y;
						rl->r.w = r_ours.w;
						rl->r.h = h;

						r_ours.y += h;
						r_ours.h -= h;
						
						flag |= 1;
						DIAGS((" -- 1. new=(%d/%d/%d/%d), remain=(%d/%d/%d/%d)", rl->r, r_ours));
					}
					if (r_win.x > r_ours.x)
					{
						if (flag & 1)
						{
							rl_prev = rl;
							rl = kmalloc(sizeof(*rl));
							assert(rl);
							rl->next = rl_prev->next;
							rl_prev->next = rl;
							DIAGS((" -- 2. alloc new=%lx", rl));
						}
#if GENERATE_DIAGS
						else
							DIAGS((" -- 2. using orig=%lx", rl));
#endif
						rl->r.x = r_ours.x;
						rl->r.y = r_ours.y;
						rl->r.w = w;
						rl->r.h = r_ours.h;

						r_ours.x += w;
						r_ours.w -= w;
						flag |= 2;
						DIAGS((" -- 2. new=(%d/%d/%d/%d), remain=(%d/%d/%d/%d)", rl->r, r_ours));
					}
					if (our_y2 > win_y2)
					{
						if (flag & 3)
						{
							rl_prev = rl;
							rl = kmalloc(sizeof(*rl));
							assert(rl);
							rl->next = rl_prev->next;
							rl_prev->next = rl;
							DIAGS((" -- 3. alloc new=%lx", rl));
						}
#if GENERATE_DIAGS
						else
							DIAGS((" -- 3. using orig=%lx", rl));
#endif
						rl->r.x = r_ours.x;
						rl->r.y = win_y2;
						rl->r.w = r_ours.w;
						rl->r.h = our_y2 - win_y2;

						r_ours.h -= rl->r.h;
						flag |= 4;
						DIAGS((" -- 3. new=(%d/%d/%d/%d), remain=(%d/%d/%d/%d)", rl->r, r_ours));
					}
					if (our_x2 > win_x2)
					{
						if (flag & 7)
						{
							rl_prev = rl;
							rl = kmalloc(sizeof(*rl));
							assert(rl);
							rl->next = rl_prev->next;
							rl_prev->next = rl;
							DIAGS((" -- 4. alloc new=%lx", rl));
						}
#if GENERATE_DIAGS
						else
							DIAGS((" -- 4. using orig=%lx", rl));
#endif

						rl->r.x = win_x2;
						rl->r.y = r_ours.y;
						rl->r.w = our_x2 - win_x2; //win_x2 - our_x2;
						rl->r.h = r_ours.h;

						r_ours.w -= rl->r.w;
						flag |= 8;
						DIAGS((" -- 4. new=(%d/%d/%d/%d), remain=(%d/%d/%d/%d)", rl->r, r_ours));
					}
				}
				else
				{
					flag |= 1;
					DIAGS((" -- 1. whole=(%d/%d/%d/%d)", rl->r));
				}

				rl_next = rl->next;

				if (!flag)
				{
					DIAGS((" covered, releasing (nrl=%lx) %lx=(%d/%d/%d/%d) rl_prev=%lx(%lx)",
						nrl, rl, rl->r, rl_prev, rl_prev ? (long)rl_prev->next : 0));
					if (rl == nrl)
						nrl = rl_next;
					if (rl_prev)
						rl_prev->next = rl_next;
					kfree(rl);
				}
				else
				{
					rl_prev = rl;
				}
			} /* for (rl = nrl; rl; rl = rl_next) */
			wl = wl->prev;			
		} /* while (wl) */
	} /* if (nrl && w->prev) */
#if GENERATE_DIAGS
	else
		DIAGS((" -- wind is topped"));
#endif

	if (swap)
		wind->rect_list = wind->rect_user = wind->rect_start = nrl;
		
#if GENERATE_DIAGS
	{
		short i = 0;
		rl = nrl;
		while (rl)
		{
			i++;
			rl = rl->next;
		}
		DIAGS((" make_rect_list: created %d rectangles, first=%lx", i, nrl));
	}
#endif

	return nrl;
}

void
free_rect_list(struct xa_rect_list *first)
{
	struct xa_rect_list *next;
#if GENERATE_DIAGS
	short i = 0;
#endif

	DIAGS(("free_rect_list: start=%lx", first));
	while (first)
	{
		DIAGS((" -- freeing %lx, next=%lx",
			first, (long)first->next));
#if GENERATE_DIAGS
		i++;
#endif
		next = first->next;
		kfree(first);
		first = next;
	}
	DIAGS((" -- freed %d rectnagles", i));
}

struct xa_rect_list *
rect_get_user_first(struct xa_window *w)
{
	w->rect_user = w->rect_start;
	return w->rect_user;
}

struct xa_rect_list *
rect_get_system_first(struct xa_window *w)
{
	w->rect_list = w->rect_start;
	return w->rect_list;
}

struct xa_rect_list *
rect_get_user_next(struct xa_window *w)
{
	if (w->rect_user)
		w->rect_user = w->rect_user->next;
	return w->rect_user;
}

struct xa_rect_list *
rect_get_system_next(struct xa_window *w)
{
	if (w->rect_list)
		w->rect_list = w->rect_list->next;
	return w->rect_list;
}

bool
was_visible(struct xa_window *w)
{
	RECT o, n;
	if (w->rect_prev && w->rect_start)
	{
		o = w->rect_prev->r;
		n = w->rect_start->r;
		DIAG((D_r, w->owner, "was_visible? prev (%d/%d,%d/%d) start (%d/%d,%d/%d)", o, n));
		if (o.x == n.x && o.y == n.y && o.w == n.w && o.h == n.h)
			return true;
	}
	return false;
}

/*
 * Compute intersection of two rectangles; put result rectangle
 * into *d; return true if intersection is nonzero.
 *
 * (Original version of this function taken from Digital Research's
 * GEM sample application `DEMO' [aka `DOODLE'],  Version 1.1,
 * March 22, 1985)
 */
bool
xa_rc_intersect(RECT s, RECT *d)
{
	short w1 = s.x + s.w;
	short w2 = d->x + d->w;
	short h1 = s.y + s.h;
	short h2 = d->y + d->h;

	d->x = max(s.x, d->x);
	d->y = max(s.y, d->y);
	d->w = min(w1, w2) - d->x;
	d->h = min(h1, h2) - d->y;

	return (d->w > 0) && (d->h > 0);
}
/* Ozk:
 * This is my (ozk) version of the xa_rc_intersect.
 * Takes pointers to source, destination and result
 * rectangle structures.
 */
bool
xa_rect_clip(RECT *s, RECT *d, RECT *r)
{
	short w1 = s->x + s->w;
	short w2 = d->x + d->w;
	short h1 = s->y + s->h;
	short h2 = d->y + d->h;

	r->x = s->x > d->x ? s->x : d->x;	//max(s->x, d->x);
	r->y = s->y > d->y ? s->y : d->y;	//max(s->y, d->y);
	r->w = (w1 < w2 ? w1 : w2) - r->x; 		//min(w1, w2) - d->x;
	r->h = (h1 < h2 ? h1 : h2) - r->y;		//min(h1, h2) - d->y;

	return (r->w > 0) && (r->h > 0);
}
	