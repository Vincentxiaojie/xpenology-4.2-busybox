/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

#include "libbb.h"
#include <mntent.h>

#ifdef MY_ABC_HERE
#include "synobusybox.h"
#include <glob.h>
#include <syslog.h>
#include <limits.h>
#define  SYNO_PROC_PREFIX        "/proc"
#if SYNO_HAVE_KERNEL_VERSION(2,6,38)
#define  SYNO_PROC_PS_NAME       "comm"
#else
#define  SYNO_PROC_PS_NAME       "cmdline"
#endif
#define  SYNO_PROC_CWD_PATH      SYNO_PROC_PREFIX"/*/cwd"
#define  SYNO_PROC_EXE_PATH      SYNO_PROC_PREFIX"/*/exe"
#define  SYNO_PROC_FD_PATH       SYNO_PROC_PREFIX"/*/fd/*"
#define SYNO_UNMOUNT_RETRY_KILL_PROCESS	 	30
#define SYNO_UNMOUNT_RETRY_KILL_ALL		45
#define SYNO_UNMOUNT_RETRY_MAX			60
#endif

/*
 * Given a block device, find the mount table entry if that block device
 * is mounted.
 *
 * Given any other file (or directory), find the mount table entry for its
 * filesystem.
 */
struct mntent* FAST_FUNC find_mount_point(const char *name, int subdir_too)
{
	struct stat s;
	FILE *mtab_fp;
	struct mntent *mountEntry;
	dev_t devno_of_name;
	bool block_dev;

	if (stat(name, &s) != 0)
		return NULL;

	devno_of_name = s.st_dev;
	block_dev = 0;
	if (S_ISBLK(s.st_mode)) {
		devno_of_name = s.st_rdev;
		block_dev = 1;
	}

	mtab_fp = setmntent(bb_path_mtab_file, "r");
	if (!mtab_fp)
		return NULL;

	while ((mountEntry = getmntent(mtab_fp)) != NULL) {
		/* rootfs mount in Linux 2.6 exists always,
		 * and it makes sense to always ignore it.
		 * Otherwise people can't reference their "real" root! */
		if (strcmp(mountEntry->mnt_fsname, "rootfs") == 0)
			continue;

		if (strcmp(name, mountEntry->mnt_dir) == 0
		 || strcmp(name, mountEntry->mnt_fsname) == 0
		) { /* String match. */
			break;
		}

		if (!(subdir_too || block_dev))
			continue;

		/* Is device's dev_t == name's dev_t? */
		if (stat(mountEntry->mnt_fsname, &s) == 0 && s.st_rdev == devno_of_name)
			break;
		/* Match the directory's mount point. */
		if (stat(mountEntry->mnt_dir, &s) == 0 && s.st_dev == devno_of_name)
			break;
	}
	endmntent(mtab_fp);

	return mountEntry;
}

#ifdef MY_ABC_HERE
static int  get_process_name(const int pid, char *szProcessName, const int processLen)
{
	int            ret = -1;
	size_t         cbBuf = 0;
	char          *szBuf = NULL;
	char           szCommPath[PATH_MAX];
	FILE          *pfSrc = NULL;

	snprintf(szCommPath, sizeof(szCommPath), "%s/%d/%s", SYNO_PROC_PREFIX, pid, SYNO_PROC_PS_NAME);
	if (NULL == (pfSrc = fopen(szCommPath, "r"))) {
		goto ErrOut;
	}
	if (-1 == getline(&szBuf, &cbBuf, pfSrc)) {
		goto ErrOut;
	}
#if SYNO_HAVE_KERNEL_VERSION(2,6,38)
	szBuf[strlen(szBuf)-1] = '\0';  // trim the string.
#endif
	snprintf(szProcessName, processLen, "%s", szBuf);
	ret = 0;

ErrOut:
	if (szBuf) {
		free(szBuf);
	}
	if (pfSrc) {
		fclose(pfSrc);
	}
	return ret;
}

static void do_killall(const char *szProcessName, int blSigSendKill)
{
	pid_t *pl;
	pid_t* pidList;

	pidList = find_pid_by_name(szProcessName);
	for (pl = pidList; *pl; pl++) {
		if (0 == *pl) {
			break;
		}
		kill(*pl, blSigSendKill?SIGKILL:SIGTERM);
	}
	free(pidList);
}

static int do_kill_process(const char *szDir, const char *szGlobPath, int blKillAll, int blSigSendKill)
{
	int           ret = 0;
	int           killPid = 0;
	size_t        i = 0;
	int           dirLen = 0;
	char          szFilePath[PATH_MAX];
	char          szProcessName[PATH_MAX];
	glob_t        globBuff;

	memset(&globBuff, 0, sizeof(globBuff));
	if (0 != glob(szGlobPath, GLOB_DOOFFS, NULL, &globBuff)) {
		return ret;
	}
	dirLen = strlen(szDir);
	for (i = 0; i < globBuff.gl_pathc; i++) {
		memset(&szFilePath, 0, sizeof(szFilePath));
		if (-1 == readlink(globBuff.gl_pathv[i], szFilePath, sizeof(szFilePath))) {
			continue;
		}
		if (strncmp(szDir, szFilePath, dirLen)) {
			continue;
		}
		if (('\0' != szFilePath[dirLen]) && ('/' != szFilePath[dirLen])) {
			continue;
		}
		if (1 != sscanf(globBuff.gl_pathv[i], "/proc/%d/", &killPid)) {
			continue;
		}
		if (0 > get_process_name(killPid, szProcessName, sizeof(szProcessName))) {
			continue;
		}
		if (0 != kill((pid_t)killPid, blSigSendKill?SIGKILL:SIGTERM)) {
			syslog(LOG_ERR, "Failed to kill the process \"%s\" with %s failed.", szProcessName, szFilePath);
		} else {
			syslog(LOG_ERR, "Kill the process \"%s\" with %s.", szProcessName, szFilePath);
		}
		if (blKillAll) {
			do_killall(szProcessName, blSigSendKill);
		}
		ret = 1;
	}

	globfree(&globBuff);
	return ret;
}

/**
 * Kill all the processes which get the files
 * on the target volume.
 * It will retry if the volume is still not clean. (max 
 * SYNO_UNMOUNT_MAX_RETRY) 
 * 
 * @param szDir  target volume path
 * 
 * @return 
 */
void FAST_FUNC kill_process_for_umount(const char *szDir)
{
	int cRetry = 0;
	int blSendKill = 0, blKillall = 0;
	int dirLen = 0;
	int ret_exe = 0, ret_cwd = 0, ret_fd = 0;
	char szVolumePrefix[PATH_MAX];

	if (!szDir) {
		return;
	}
	dirLen = strlen(szDir);
	snprintf(szVolumePrefix, sizeof(szVolumePrefix), "%s", szDir);
	if ('/' == szVolumePrefix[dirLen-1]) {
		szVolumePrefix[dirLen-1] = '\0';
	}
	while (cRetry <= SYNO_UNMOUNT_RETRY_MAX) {
		if (cRetry == SYNO_UNMOUNT_RETRY_KILL_PROCESS) {
			syslog(LOG_ERR, "umount %s start to send kill process.", szDir);
			blSendKill = 1;
		}
		if (cRetry == SYNO_UNMOUNT_RETRY_KILL_ALL) {
			syslog(LOG_ERR, "umount %s start to send kill all processes.", szDir);
			blKillall = 1;
		}
		ret_exe = do_kill_process(szVolumePrefix, SYNO_PROC_EXE_PATH, blKillall, blSendKill);
		ret_cwd = do_kill_process(szVolumePrefix, SYNO_PROC_CWD_PATH, blKillall, blSendKill);
		ret_fd = do_kill_process(szVolumePrefix, SYNO_PROC_FD_PATH, blKillall, blSendKill);
		if (!(ret_exe || ret_cwd || ret_fd)) {
			break;
		}
		sleep(1);
		cRetry++;
	}
}
#endif

