/*
 * $Id$
 *
 * XaAES - XaAES Ain't the AES (c) 1992 - 1998 C.Graham
 *                                 1999 - 2003 H.Robbers
 *                                        2004 F.Naumann
 *
 * A multitasking AES replacement for MiNT
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
#include "xalloc.h"

#include "xa_form.h"
#include "xa_rsrc.h"
#include "xa_shel.h"

#include "mint/dcntl.h"
#include "mint/fcntl.h"
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
Block(struct xa_client *client, int which)
{
        DIAG((D_kern, client, "[%d]Blocked %s", which, c_owner(client)));
	sleep(IO_Q, (long)client);
}

void
Unblock(struct xa_client *client, unsigned long value, int which)
{
	wake(IO_Q, (long)client);

	/* HR 041201: the following served as a excellent safeguard on the
	 *            internal consistency of the event handling mechanisms.
	 */
	if (value == XA_OK)
	{
		DIAG((D_kern,client,"[%d]Unblocked %s 0x%lx", which, c_owner(client), value));
		cancel_evnt_multi(client,1);
	}
}

static const char alert_pipe_name[] = "u:\\pipe\\alert";
static const char KBD_dev_name[] = "u:\\dev\\console";
static const char moose_name[] = "u:\\dev\\moose";

static vdi_vec *svmotv = NULL;
static vdi_vec *svbutv = NULL;
static vdi_vec *svwhlv = NULL;
/* static vdi_vec *svtimv = NULL; */

/*
 * (Re)initialise the mouse device /dev/moose
 */
static bool
init_moose(void)
{
	struct fs_info info;
	long major, minor;
	struct moose_vecsbuf vecs;
	unsigned short dclick_time;

	if (!C.MOUSE_dev)
	{
		C.MOUSE_dev = f_open(moose_name, O_RDWR);
		if (C.MOUSE_dev < 0)
		{
			fdisplay(log, "Can't open %s", moose_name);
			return false;
		}
	}

	/* first check the xdd version */
	if (f_cntl(C.MOUSE_dev, (long)(&info), FS_INFO) != 0)
	{
		fdisplay(log, "Fcntl(FS_INFO) failed, please use the right moose.xdd?");
		return false;
	}

	major = info.version >> 16;
	minor = info.version & 0xffffL;
	if (major != 0 || minor < 4)
	{
		fdisplay(log, ", do you use the right xdd?");
		return false;
	}

	if (f_cntl(C.MOUSE_dev, (long)(&vecs), MOOSE_READVECS) != 0)
	{
		fdisplay(log, "Moose set dclick time failed");
		return false;
	}

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

	dclick_time = lcfg.double_click_time;
	if (f_cntl(C.MOUSE_dev, (long)(&dclick_time), MOOSE_DCLICK) != 0)
		fdisplay(log, "Moose set dclick time failed");

	return true;
}

/* HR 050601: The active widgets are intimately connected to the mouse.
 *            There is only 1 mouse, so there is only need for 1 global structure.
 */
XA_PENDING_WIDGET widget_active = { NULL }; /* Pending active widget (if any) */



/* HR 180601: Now use the global (once) set values in the button structure */
void
multi_intout(struct xa_client *client, short *o, int evnt)
{
	//get_mouse(1);
	exclusive_mouse_input(client, 1, 0,0,0);

	*o++ = evnt;
	*o++ = mu_button.x;
	*o++ = mu_button.y;
	*o++ = mu_button.cb;	/*button.b;*/
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
		buf = xmalloc(sizeof(char) * n + 4, 2);
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
	C.shutdown |= QUIT_NOW;
}

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

	p_signal (SIGALRM, (long) ignore);
	p_signal (SIGTERM, (long) sigterm);
	p_signal (SIGQUIT, (long) ignore);
	p_signal (SIGHUP,  (long) ignore);
	p_signal (SIGTSTP, (long) ignore);
	p_signal (SIGINT,  (long) ignore);
	p_signal (SIGABRT, (long) ignore);
	p_signal (SIGUSR1, (long) ignore);
	p_signal (SIGUSR2, (long) ignore);


	/*
	 * register trap#2 handler
	 */

	if (register_trap2(XA_handler, 0, 0))
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
	mu_button.got = true;

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

	/* Open /dev/moose (040201: after xa_setup.scl for mouse configuration) */
	if (!init_moose())
	{
		goto leave;
	}

	DIAGS(("Handles: MOUSE %ld, KBD %ld, ALERT %ld",
		C.MOUSE_dev, C.KBD_dev, C.alert_pipe));


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
		/* HR: The root of all locking under AES pid. */
		/* HR: how about this? It means that these
	         *     semaphores are not needed and are effectively skipped.
		 */
		enum locks lock = winlist|envstr|pending;
#if _xxx_GENERATE_DIAGS
		/* For if no keys can be received anymore. */
		{
			short sta;

			vq_key_s(C.vh, &sta);
			if ((sta & 14) == 14)	/* CTRL|ALT|LSHF (break_in) */
			{
				DIAGS(("shutdown by CtlAltShift"));
				shutdown(lock);
				break;
			}
		}
#endif
		preprocess_mouse(lock); 

		input_channels  = 1UL << C.MOUSE_dev;
		input_channels |= 1UL << C.KBD_dev;	/* We are waiting on all these channels */
		input_channels |= 1UL << C.alert_pipe;	/* Monitor the system alert pipe */

		DIAG((D_kern, NULL, "Fselect mask: 0x%08lx", input_channels));

		/* The pivoting point of XaAES!
		 * Wait via Fselect() for keyboard, mouse and the AES command pipe(s).
		 */
		fs_rtn = f_select(C.active_timeout.timeout, (long *) &input_channels, 0L, 0L);	

		DIAG((D_kern, NULL,">>Fselect(t%d) :: %d, channels: 0x%08lx, U%dM%d",
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

			if (input_channels & (1UL << C.MOUSE_dev))
				mouse_input(lock, false);

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
	 * deinstall trap#2 handler
	 */
	register_trap2(XA_handler, 1, 0);

	/*
	 * close input devices
	 */
	if (C.MOUSE_dev > 0)
		f_close(C.MOUSE_dev);

	if (C.KBD_dev > 0)
		f_close(C.KBD_dev);

	if (C.alert_pipe > 0)
		f_close(C.alert_pipe);

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
