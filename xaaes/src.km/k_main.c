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

#include "k_main.h"
#include "xa_global.h"

#include "app_man.h"
#include "adiload.h"
#include "c_window.h"
#include "cnf_xaaes.h"
#include "desktop.h"
#include "handler.h"
#include "init.h"
#include "k_init.h"
#include "k_keybd.h"
#include "k_mouse.h"
#include "k_shutdown.h"
#include "nkcc.h"
#include "scrlobjc.h"
#include "taskman.h"
#include "widgets.h"

#include "xa_form.h"
#include "xa_rsrc.h"
#include "xa_shel.h"

#include "mint/dcntl.h"
#include "mint/fcntl.h"
#include "mint/ioctl.h"
#include "mint/signal.h"


/*
 * Kernel Message Handler
 * 
 * This is responsible for accepting requests via the XaAES.cmd pipe and
 * sending (most) replies via the clients' reply pipes.
 * 	
 * We also get keyboard & mouse input data here.
 */

void
cancel_cevents(struct xa_client *client)
{
	struct c_event *ce, *nxt;

	ce = client->cevnt_head;
	while (ce)
	{
		nxt = ce->next;

		DIAG((D_kern, client, "Cancel evnt %lx (next %lx) for %s",
			ce, nxt, client->name));
		(*ce->funct)(0, ce, true);
		kfree(ce);
		ce = nxt;
	}

	client->cevnt_head = NULL;
	client->cevnt_tail = NULL;
	client->cevnt_count = 0;

	if (C.ce_open_menu == client)
		C.ce_open_menu = NULL;
}

void
post_cevent(struct xa_client *client,
	void (*func)(enum locks, struct c_event *, bool cancel),
	void *ptr1, void *ptr2,
	int d0, int d1, RECT *r,
	const struct moose_data *md)
{
	struct c_event *c;

	{
		c = kmalloc(sizeof(*c));
		if (c)
		{
			c->next		= 0;
			c->funct	= func;
			c->client	= client;
			c->ptr1		= ptr1;
			c->ptr2		= ptr2;
			c->d0		= d0;
			c->d1		= d1;

			if (r)
				c->r = *r;
			if (md)
				c->md = *md;

			if (!client->cevnt_head)
			{
				client->cevnt_head = c;
				client->cevnt_tail = c;
			}
			else
			{
				client->cevnt_tail->next = c;
				client->cevnt_tail = c;
			}
			client->cevnt_count++;
		}

		DIAG((D_mouse, client, "added cevnt %lx(%d) (head %lx, tail %lx) for %s",
			c, client->cevnt_count, client->cevnt_head, client->cevnt_tail, client->name));

		if (client != C.Aes)
			Unblock(client, 1, 5000);
		else
			dispatch_cevent(client);
	}
}

int
dispatch_cevent(struct xa_client *client)
{
	struct c_event *ce, *nxt;
	int ret = 0;

	ce = client->cevnt_head;
	if (ce)
	{
		nxt = ce->next;

		if (!nxt)
			client->cevnt_tail = nxt;

		client->cevnt_head = nxt;
		client->cevnt_count--;

		DIAG((D_kern, client, "Dispatch evnt %lx (head %lx, tail %lx, count %d) for %s",
			ce, client->cevnt_head, client->cevnt_tail, client->cevnt_count, client->name));
		(*ce->funct)(0, ce, false);
		kfree(ce);
		ret = 1;
	}
	return ret;
}


void
Block(struct xa_client *client, int which)
{

	/*
	 * Get rid of any event queue
	 */
	while (dispatch_cevent(client))
	{
		if (client->usr_evnt)
		{
			cancel_evnt_multi(client, 1);
			return;
		}
	}

	/*
	 * Getting here if no more client events are in the queue
	 * Looping around doing client events until a user event
	 * happens.. that is, we've got something to pass back to
	 * the application.
	 */
	while (!client->usr_evnt)
	{
		DIAG((D_kern, client, "[%d]Blocked %s", which, c_owner(client)));
		client->inblock = true;
		client->sleeplock = (long)client;
		client->sleepqueue = IO_Q;
		sleep(IO_Q, (long)client);
		client->inblock = false;
		client->sleeplock = 0;
		if ((client->waiting_for & MU_TIMER) && !client->timeout)
		{
			return;
		}
		while (!client->usr_evnt && dispatch_cevent(client) ){}
	}
	cancel_evnt_multi(client, 1);
}

void
Unblock(struct xa_client *client, unsigned long value, int which)
{
	/* the following served as a excellent safeguard on the
	 * internal consistency of the event handling mechanisms.
	 */
	if (value == XA_OK)
	{
		cancel_evnt_multi(client,1);
	}
	wake(IO_Q, (long)client);
	DIAG((D_kern,client,"[%d]Unblocked %s 0x%lx", which, c_owner(client), value));
}


static const char alert_pipe_name[] = "u:\\pipe\\alert";
static const char KBD_dev_name[] = "u:\\dev\\console";
static const char moose_name[] = "u:\\dev\\moose";

static vdi_vec *svmotv = NULL;
static vdi_vec *svbutv = NULL;
static vdi_vec *svwhlv = NULL;
/* static vdi_vec *svtimv = NULL; */

/*
 * initialise the mouse device
 */
static bool
init_moose(void)
{
	bool ret = false;

	C.button_waiter = 0;
	C.redraws = 0;

	C.adi_mouse = adi_name2adi("moose");
	if (C.adi_mouse)
	{
		long aerr;

		aerr = adi_open(C.adi_mouse);
		if (!aerr)
		{
			struct moose_vecsbuf vecs;

			aerr = (*C.adi_mouse->ioctl)(C.adi_mouse, MOOSE_READVECS, (long)&vecs);

			if (vecs.motv)
			{
				vex_motv(C.P_handle, vecs.motv, (void **)(&svmotv));
				vex_butv(C.P_handle, vecs.butv, (void **)(&svbutv));

				if (vecs.whlv)
				{
					vex_wheelv(C.P_handle, vecs.whlv, (void **)(&svwhlv));
					fdisplay(log, "Wheel support present");
				}
				else
					fdisplay(log, "No wheel support!!");
			}

			if ((*C.adi_mouse->ioctl)(C.adi_mouse, MOOSE_DCLICK, (long)lcfg.double_click_time))
				fdisplay(log, "Moose set dclick time failed");

			DIAGS(("Using moose adi"));
			ret = true;
		}
		else
		{
			fdisplay(log, "Error opening moose adi %lx", aerr);	
			C.adi_mouse = NULL;
		}
	}
	else
	{
		fdisplay(log, "Could not find moose.adi, please install in %s!", sysdir);
	}

	return ret;
}


/* The active widgets are intimately connected to the mouse.
 * There is only 1 mouse, so there is only need for 1 global structure.
 */
XA_PENDING_WIDGET widget_active = { NULL }; /* Pending active widget (if any) */

/* Now use the global (once) set values in the button structure */

/*
 * Ozk: multi_intout() may be called by processes not yet called
 * appl_ini(). So, it must not depend on client being valid!
*/
void
multi_intout(struct xa_client *client, short *o, int evnt)
{
	short b, x, y;

	check_mouse(client, &b, &x, &y);

	*o++ = evnt;
	*o++ = x;
	*o++ = y;
	*o++ = b;
	*o++ = mu_button.ks;

	if (evnt)
	{
		*o++ = 0;
		*o++ = 0;
	}
}

static int alert_pending = 0;

static void
alert_input(enum locks lock)
{
	/* System alert? Alert and add it to the log */
	OBJECT *icon;
	char *buf, c;
	long n;

	n = f_instat(C.alert_pipe);
	if (n > 0)
	{
		SCROLL_ENTRY_TYPE flag = FLAG_MAL;
		OBJECT *form;

		form = ResourceTree(C.Aes_rsc, SYS_ERROR);
		buf = kmalloc(n + 4);
		assert(buf);
		f_read(C.alert_pipe, n, buf);

		/* Pretty up the log entry with a nice
		 * icon to match the one in the alert
		 */
		c = buf[1];
		switch(c)
		{
		case '1':
			icon = form + SALERT_IC1;
			break;
		case '2':
			icon = form + SALERT_IC2;
			break;
		case '3':
			icon = form + SALERT_IC3;
			break;
		case '4':
			icon = form + SALERT_IC4;
			break;
		default:
			icon = NULL;
			break;
		}

		if (update_locked())
			flag |= FLAG_PENDING;

		/* HR! not buf+4; you cant free that! */
		/* Add the log entry */
		add_scroll_entry(form, SYSALERT_LIST, icon, buf, flag);

		 /* HR: Now you can always lookup the error in the log. */
		DIAGS(("ALERT PIPE: '%s' %s",buf, update_locked() ? "pending" : "displayed"));
		if (flag & FLAG_PENDING)
		{
			ping();
			alert_pending |= 1;
		}
		else
		{
			/* if a app left the mouse off */
			forcem();
			/* Bring up an alert */
			do_form_alert(lock, C.Aes, 1, buf);
		}
	}
	else
	{
		DIAGS(("No bytes in alert_pipe"));
	}
}

void
dispatch_shutdown(int flags)
{
	C.shutdown = QUIT_NOW | flags;
	wakeselect(C.Aes->p);
}

/*
 * signal handlers
 */
static void
ignore(void)
{
	DIAGS(("signal ignored!"));
}
static void
sigterm(void)
{
	dispatch_shutdown(0);
}
static void
sigchld(void)
{
	long r;

	while ((r = p_waitpid(-1, 1, NULL)) > 0)
	{
		DIAGS(("sigchld -> %li (pid %li)", r, ((r & 0xffff0000L) >> 16)));
	}
}

static void k_exit(void);

/*
 * our XaAES server kernel thread
 * 
 * It have it's own context and can use all syscalls like a normal
 * process (except that it don't go through the syscall handler).
 * 
 * It run in kernel mode and share the kernel memspace, cwd, files
 * and sigs with all other kernel threads (mainly the idle thread
 * alias rootproc).
 */
void
k_main(void *dummy)
{
	unsigned long input_channels;
	int fs_rtn, evnt_count = 0;


	/*
	 * setup kernel thread
	 */

	C.AESpid = p_getpid();

	/* terminating signals */
	p_signal(SIGHUP,   (long) ignore);
	p_signal(SIGINT,   (long) ignore);
	p_signal(SIGQUIT,  (long) ignore);
	p_signal(SIGPIPE,  (long) ignore);
	p_signal(SIGALRM,  (long) ignore);
	p_signal(SIGTSTP,  (long) ignore);
	p_signal(SIGTTIN,  (long) ignore);
	p_signal(SIGTTOU,  (long) ignore);
	p_signal(SIGXCPU,  (long) ignore);
	p_signal(SIGXFSZ,  (long) ignore);
	p_signal(SIGVTALRM,(long) ignore);
	p_signal(SIGPROF,  (long) ignore);
	p_signal(SIGUSR1,  (long) ignore);
	p_signal(SIGUSR2,  (long) ignore);

#if !GENERATE_DIAGS
	/* fatal signals */
	p_signal(SIGILL,   (long) k_exit);
	p_signal(SIGTRAP,  (long) k_exit);
	p_signal(SIGTRAP,  (long) k_exit);
	p_signal(SIGABRT,  (long) k_exit);
	p_signal(SIGFPE,   (long) k_exit);
	p_signal(SIGBUS,   (long) k_exit);
	p_signal(SIGSEGV,  (long) k_exit);
	p_signal(SIGSYS,   (long) k_exit);
#endif

	/* other stuff */
	p_signal(SIGTERM,  (long) sigterm);
	p_signal(SIGCHLD,  (long) sigchld);


	/*
	 * register trap#2 handler
	 */

	if (register_trap2(XA_handler, 0, 0, 0))
	{
		DIAGS(("register_trap2 failed!"));
		goto leave;
	}


	/*
	 * Initialization AES/VDI
	 */
	if (k_init() != 0)
	{
		DIAGS(("k_init failed!"));
		goto leave;
	}


	/* 
	 * Initialization I/O
	 */

	bzero(&pending_button, sizeof(pending_button));


	mu_button.b = 0;
	mu_button.cb = 0;
	mu_button.clicks = 0;
	mu_button.x = 0;
	mu_button.y = 0;
	mu_button.newc = 0;
	mu_button.newr = 0;


	/* Open the MiNT Salert() pipe to be polite about system errors */
	C.alert_pipe = f_open(alert_pipe_name, O_CREAT|O_RDWR);
	if (C.alert_pipe < 0)
	{
		fdisplay(log, "XaAES ERROR: Can't open '%s' :: %ld",
			 alert_pipe_name, C.alert_pipe);
		goto leave;
	}
	fdisplay(log, "Open '%s' to %ld", alert_pipe_name, C.alert_pipe);

	/* Open the u:/dev/console device to get keyboard input */
	C.KBD_dev = f_open(KBD_dev_name, O_DENYRW|O_RDONLY);
	if (C.KBD_dev < 0)
	{
		fdisplay(log, "XaAES ERROR: Can't open '%s' :: %ld",
			 KBD_dev_name, C.KBD_dev);
		goto leave;
	}
	fdisplay(log, "Open '%s' to %ld", KBD_dev_name, C.KBD_dev);

	/* initialize mouse */
	if (!init_moose())
	{
		fdisplay(log, "XaAES ERROR: init_moose failed");
		goto leave;
	}

	DIAGS(("Handles: KBD %ld, ALERT %ld", C.KBD_dev, C.alert_pipe));


	/*
	 * Load Accessories
	 */

	DIAGS(("loading accs"));
	load_accs();
	DIAGS(("loading accs done!"));


	/*
	 * startup shell and autorun
	 */

	DIAGS(("loading shell and autorun"));
	{
		enum locks lock = winlist|envstr|pending;
		Path parms;
		int i;

		parms[0] = sprintf(parms+1, sizeof(parms)-1, "%s", cfg.cnf_shell);

		C.DSKpid = launch(lock, 0, 0, 0, cfg.cnf_shell, parms, C.Aes);
		if (C.DSKpid > 0)
			strcpy(C.desk, cfg.cnf_shell);

		for (i = 0; i < sizeof(cfg.cnf_run)/sizeof(cfg.cnf_run[0]); i++)
		{
			if (cfg.cnf_run[i])
			{
				parms[0] = sprintf(parms+1, sizeof(parms)-1, "%s", cfg.cnf_run[i]);
				launch(lock, 0, 0, 0, cfg.cnf_run[i], parms, C.Aes);
			}
		}
	}
	DIAGS(("loading shell and autorun done!"));


	/*
	 * Main kernel loop
	 */

	do {
		/* The root of all locking under AES pid. */
		/* how about this? It means that these
	         * semaphores are not needed and are effectively skipped.
		 */
		enum locks lock = winlist|envstr|pending;

		input_channels = 1UL << C.KBD_dev;	/* We are waiting on all these channels */
		input_channels |= 1UL << C.alert_pipe;	/* Monitor the system alert pipe */

		/* DIAG((D_kern, NULL, "Fselect mask: 0x%08lx", input_channels)); */

		/* The pivoting point of XaAES!
		 * Wait via Fselect() for keyboard and alerts.
		 */
		fs_rtn = f_select(C.active_timeout.timeout, (long *) &input_channels, 0L, 0L);	

		DIAG((D_kern, NULL,">>Fselect(t%d) :: %d, channels: 0x%08lx, update_lock %d, mouse_lock %d",
			C.active_timeout.timeout, fs_rtn, input_channels,
			update_locked() ? update_locked()->p->pid : 0,
			mouse_locked() ? mouse_locked()->p->pid : 0));

		if (C.shutdown & QUIT_NOW)
			break;

		if (evnt_count == 5000)
		{
			/* Housekeeping */
			evnt_count = 0;
		}

		if (fs_rtn > 0)
		{
			/* HR Fselect returns no of bits set in input_channels */
			evnt_count++;

			if (input_channels & (1UL << C.alert_pipe))
			{
				alert_input(lock);
			}
			else
			{
				if (!update_locked() == 0 && alert_pending)
				{
					OBJECT *form = ResourceTree(C.Aes_rsc, SYS_ERROR);
					char *buf;

					do {
						buf = pendig_alerts(form, SYSALERT_LIST);
						if (buf)
						{
							/* if a app left the mouse off */
							forcem();
							/* Bring up an alert */
							do_form_alert(lock, C.Aes, 1, buf);
						}
					}
					while (buf);

					alert_pending = 0;
				}
			}

			if (input_channels & (1UL << C.KBD_dev))
				keyboard_input(lock);

		}
#if GENERATE_DIAGS
		else if (fs_rtn < 0)
		{
			DIAG((D_kern, NULL, ">>Fselect :: %d",fs_rtn));
		}
#endif
		else
		{
			/* Call the active function if we need to */
			if (C.active_timeout.task)
			{
				if (C.active_timeout.timeout != 0
				     && (fs_rtn == 0 || (evnt_count & 0xff) == 1))
					(*C.active_timeout.task)(&C.active_timeout);
			}
		}

		/* execute delayed delete_window */
		if (S.deleted_windows.first)
			do_delayed_delete_window(lock);
	}
	while (!(C.shutdown & QUIT_NOW));

	DIAGS(("**** Leave kernel for shutdown"));

leave:
	k_exit();
}

static void
k_exit(void)
{
	/* don't reenter on fatal signals */
	p_signal(SIGILL,   SIG_DFL);
	p_signal(SIGTRAP,  SIG_DFL);
	p_signal(SIGTRAP,  SIG_DFL);
	p_signal(SIGABRT,  SIG_DFL);
	p_signal(SIGFPE,   SIG_DFL);
	p_signal(SIGBUS,   SIG_DFL);
	p_signal(SIGSEGV,  SIG_DFL);

	if (svmotv)
	{
		void *m, *b, *h;

		vex_motv(C.P_handle, svmotv, &m);
		vex_butv(C.P_handle, svbutv, &b);

		if (svwhlv)
			vex_wheelv(C.P_handle, svwhlv, &h);
	}

	k_shutdown();

	/*
	 * deinstall trap #2 handler
	 */
	register_trap2(XA_handler, 1, 0, 0);

	/*
	 * close input devices
	 */
	if (C.KBD_dev > 0)
		f_close(C.KBD_dev);

	if (C.adi_mouse)
		adi_close(C.adi_mouse);
		
	if (C.alert_pipe > 0)
		f_close(C.alert_pipe);

	if (loader_pid > 0)
		ikill(loader_pid, SIGCONT);


	if (C.shutdown & HALT_SYSTEM)
		s_hutdown(0);  /* halt */
	else if (C.shutdown & REBOOT_SYSTEM)
		s_hutdown(1);  /* warm start */


	/* XXX todo -> module_exit */
	kthread_exit(0);
}

/* 
 * Set the "active" function  
 */
/* HR: extended & generalized */
/* preliminary versions (only 1 such task at a time),
   but it is easy expandable. */
Tab *
new_task(Tab *new)
{
	bzero(new, sizeof(*new));
	return new;
}

void
free_task(Tab *t, int *nester)
{
	if (t)
	{
		if (t->nest && *nester)
		{
			nester--;
			free_task(t->nest, nester);
		}

		t->nest = 0;
		t->ty = NO_TASK;
		t->pr = 0;
		t->nx = 0;
		t->task = 0;
		t->timeout = 0;
	}
}
