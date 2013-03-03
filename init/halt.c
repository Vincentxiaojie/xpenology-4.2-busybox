/* vi: set sw=4 ts=4: */
/*
 * Poweroff reboot and halt, oh my.
 *
 * Copyright 2006 by Rob Landley <rob@landley.net>
 *
 * Licensed under GPL version 2, see file LICENSE in this tarball for details.
 */

#include <sys/syscall.h>
#include <asm/unistd.h>
#include "libbb.h"
#include <sys/reboot.h>
#include "synobusybox.h"

#if defined(MY_ABC_HERE)
#include <syslog.h>
#endif

#if ENABLE_FEATURE_WTMP
#include <sys/utsname.h>
#include <utmp.h>

static void write_wtmp(void)
{
	struct utmp utmp;
	struct utsname uts;
	if (access(bb_path_wtmp_file, R_OK|W_OK) == -1) {
		close(creat(bb_path_wtmp_file, 0664));
	}
	memset(&utmp, 0, sizeof(utmp));
	utmp.ut_tv.tv_sec = time(NULL);
	safe_strncpy(utmp.ut_user, "shutdown", UT_NAMESIZE);
	utmp.ut_type = RUN_LVL;
	safe_strncpy(utmp.ut_id, "~~", sizeof(utmp.ut_id));
	safe_strncpy(utmp.ut_line, "~~", UT_LINESIZE);
	if (uname(&uts) == 0)
		safe_strncpy(utmp.ut_host, uts.release, sizeof(utmp.ut_host));
	updwtmp(bb_path_wtmp_file, &utmp);

}
#else
#define write_wtmp() ((void)0)
#endif

#ifndef RB_HALT_SYSTEM
#define RB_HALT_SYSTEM RB_HALT
#endif

#ifndef RB_POWERDOWN
/* Stop system and switch power off if possible.  */
# define RB_POWERDOWN   0x4321fedc
#endif
#ifndef RB_POWER_OFF
# define RB_POWER_OFF RB_POWERDOWN
#endif
int halt_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int halt_main(int argc UNUSED_PARAM, char **argv)
{
	static const int magic[] = {
		RB_HALT_SYSTEM,
		RB_POWER_OFF,
		RB_AUTOBOOT
	};
	static const smallint signals[] = { SIGUSR1, SIGUSR2, SIGTERM };

	int delay = 0;
	int which, flags, rc;

#ifdef MY_ABC_HERE
	if (0 != setpriority(PRIO_PROCESS, 0, -20)) {
		bb_perror_msg("Failed to setpriority. (%m)");
	}

#ifndef SYNO_POWER_PC
#define IOPRIO_WHO_PROCESS (1)
#define IOPRIO_CLASS_RT (1)
#define IOPRIO_CLASS_SHIFT (13)
#define IOPRIO_PRIO_VALUE(class, data)	(((class) << IOPRIO_CLASS_SHIFT) | data)

	if (0 != syscall(__NR_ioprio_set, IOPRIO_WHO_PROCESS, getpid(), IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, 0))) {
		bb_perror_msg("Failed to ioprio_set. (%m)");
	}
#endif
#endif

	/* Figure out which applet we're running */
	for (which = 0; "hpr"[which] != applet_name[0]; which++)
		continue;

	/* Parse and handle arguments */
	opt_complementary = "d+"; /* -d N */
	/* We support -w even if !ENABLE_FEATURE_WTMP,
	 * in order to not break scripts.
	 * -i (shut down network interfaces) is ignored.
	 */
	flags = getopt32(argv, "d:nfwi", &delay);

	sleep(delay);

	write_wtmp();

	if (flags & 8) /* -w */
		return EXIT_SUCCESS;

	if (!(flags & 2)) /* no -n */
		sync();

#ifdef MY_ABC_HERE
	/*  which == 1, PowerOff
	 *  which == 2, Reboot
	 */
	if (1 == which) {
			syslog(LOG_ERR, "%s (%d) syno_poweroff_task start\n", __FILE__, __LINE__);
			system("/usr/syno/bin/syno_poweroff_task -p > /dev/null 2>&1");
	} else if (2 == which) {
			syslog(LOG_ERR, "%s (%d) syno_poweroff_task start\n", __FILE__, __LINE__);
			system("/usr/syno/bin/syno_poweroff_task -r > /dev/null 2>&1");
	}
#endif
	/* Perform action. */
	rc = 1;
	if (!(flags & 4)) { /* no -f */
//TODO: I tend to think that signalling linuxrc is wrong
// pity original author didn't comment on it...
		if (ENABLE_FEATURE_INITRD) {
			/* talk to linuxrc */
			/* bbox init/linuxrc assumed */
			pid_t *pidlist = find_pid_by_name("linuxrc");
			if (pidlist[0] > 0)
				rc = kill(pidlist[0], signals[which]);
			if (ENABLE_FEATURE_CLEAN_UP)
				free(pidlist);
		}
		if (rc) {
			/* talk to init */
			if (!ENABLE_FEATURE_CALL_TELINIT) {
				/* bbox init assumed */
				rc = kill(1, signals[which]);
			} else {
				/* SysV style init assumed */
				/* runlevels:
				 * 0 == shutdown
				 * 6 == reboot */
				rc = execlp(CONFIG_TELINIT_PATH,
						CONFIG_TELINIT_PATH,
						which == 2 ? "6" : "0",
						(char *)NULL
				);
			}
		}
	} else {
		rc = reboot(magic[which]);
	}

	if (rc)
		bb_perror_nomsg_and_die();
	return rc;
}
