// Copyright (c) 2008-2008 Synology Inc. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define COUNTS_TO_CHECK 100 //check device every 200MB.
#define BOOL int
#define FALSE 0
#define TRUE 1

#define BS 1048576 // 1M
char gszBuf[BS];

static void usage(void)
{
	printf("Copyright (c) 2008-2012 Synology Inc. All rights reserved.\n");
	printf("Usage: synodd [-r] [-w] device_list\n");
	printf("   -r: dd read\n");
	printf("   -w: dd write, which is default\n");
	printf("\n");
	printf("Example 1: synodd /dev/hda\n");
	printf("Example 2: synodd /dev/sda1 /dev/sdb2 ...\n");
	printf("Example 3: synodd -r /dev/md2 ...\n");
	printf("Example 4: synodd -w /dev/md2 ...\n");
	printf("\n");
}

static int OpenDevice(const char *szDevPath, BOOL blRead, off_t offset)
{
	int fd = -1;
	int err = -1;
	int flags = O_WRONLY;
	
	if (!szDevPath) {
		return -1;
	}

	if (blRead) {
		flags = O_RDONLY;
	} else {
		flags = O_WRONLY;
	}

	if (0 > (fd = open(szDevPath, flags, 0666))) {
		syslog(LOG_ERR, "%s(%d) open [%s](off:%llu) failed, errno=%d(%s)", __FILE__, __LINE__,
			szDevPath, (unsigned long long)offset, errno, strerror(errno));
		goto Err;
	}

	if ((off_t)-1 == lseek(fd, offset, SEEK_SET)) {
		syslog(LOG_ERR, "%s(%d) lseek %llu failed, errno=%s", __FILE__, __LINE__, (unsigned long long)offset, strerror(errno));
		goto Err;
	}

	err = 0;
Err:
	if (err) {
		if (fd >= 0) {
			close(fd);
			fd = -1;
		}
	}
	return fd;
}

static FILE * OpenProgress(const char *szDev)
{
	FILE *fp = NULL;
	const char *pch = NULL;
	char szFile[64];

	if (!szDev) {
		return NULL;
	}

	if (NULL == (pch = strrchr(szDev, '/'))) {
		pch = szDev;
	} else {
		pch++;
	}
	snprintf(szFile, sizeof(szFile), "/tmp/synodd.%s.%u", pch, getpid());
	if (NULL == (fp = fopen(szFile, "w"))) {
		syslog(LOG_ERR, "%s(%d) fopen [%s] failed, errno=%d(%s)", __FILE__, __LINE__,
			szFile, errno, strerror(errno));
		goto Err;
	}

Err:
	return fp;
}

static void WriteProgress(FILE *fp, unsigned long long ullWritedBytes, unsigned long long ullTotalBytes)
{
	rewind(fp);
	fprintf(fp, "%llu/%llu\n", ullWritedBytes, ullTotalBytes);
}

static int CheckDevice(int fd, unsigned long long *pUllTotalBytes)
{
	int err = -1;
	struct stat statbuf;
	unsigned long long ullTotalBytes = 0;

	if (0 > fd || !pUllTotalBytes) {
		return -1;
	}

	if (0 > fstat(fd, &statbuf)) {
		syslog(LOG_ERR, "%s(%d) fstat failed, errno=%d(%s)", __FILE__, __LINE__,
			errno, strerror(errno));
		err = errno;
		goto Err;
	}
	if (!S_ISBLK(statbuf.st_mode)) {
		syslog(LOG_ERR, "%s(%d) is not a block device.", __FILE__, __LINE__);
		goto Err;
	}

	ullTotalBytes = lseek(fd, 0, SEEK_END);
	if ((off_t)-1 == ullTotalBytes) {
		syslog(LOG_ERR, "%s(%d) lseek end failed, errno=%s", __FILE__, __LINE__, strerror(errno));
		err = errno;
		goto Err;
	}
	if ((off_t)-1 == lseek(fd, 0, SEEK_SET)) {
		syslog(LOG_ERR, "%s(%d) lseek 0 failed, errno=%s", __FILE__, __LINE__, strerror(errno));
		goto Err;
	}

	*pUllTotalBytes = ullTotalBytes;

	err = 0;
Err:
	return err;	
}

static int SynoddOne(char *szDev, BOOL blRead)
{
	int err = -1;
	int fd = -1;
	int counts = 0;
	ssize_t lProgressBytes = 0;
	unsigned long long ullProgTotalBytes = 0;
	unsigned long long ullTotalBytes = 0;
	FILE *fp = NULL;

	if (!szDev) {
		return -1;
	}
	
	if (0 > (fd = OpenDevice(szDev, blRead, 0))){
		err = errno;
		goto END;
	}

	if (0 > CheckDevice(fd, &ullTotalBytes)){
		err = errno;
		syslog(LOG_ERR, "%s(%d) failed to check [%s].", __FILE__, __LINE__, szDev);
		goto END;
	}

	//Open Progress File
	if (NULL == (fp = OpenProgress(szDev))) {
		goto END;
	}

	err = 0;
	while(1) {
		if (COUNTS_TO_CHECK == counts) {
			counts = 0;
			
			if (fd >= 0) {
				close(fd);
			}
			if (0 > (fd = OpenDevice(szDev, blRead, ullProgTotalBytes))){
				err = errno;
				goto END;
			}
		}
		if (blRead) {
			lProgressBytes = read(fd, gszBuf, sizeof(gszBuf));
		} else {
			lProgressBytes = write(fd, gszBuf, sizeof(gszBuf));
		}
		if (0 > lProgressBytes) {
			if (ENOSPC != errno) {
				syslog(LOG_ERR, "(%s/%d) %s [%s] failed, errno=%m"
					   ,__FILE__ ,__LINE__ , blRead?"read":"write", szDev);
				err = errno;
			}
			goto END;
		}
		ullProgTotalBytes += lProgressBytes;

		WriteProgress(fp, ullProgTotalBytes, ullTotalBytes);
		if (ullTotalBytes <= ullProgTotalBytes) {
			break;
		}

		counts++;
	}
END:
	if (0 <= fd) close(fd);
	if (NULL != fp) fclose(fp);
	return err;
}

int synodd_main(int argc, char **argv);
int synodd_main(int argc, char **argv)
{
	int err = -1;
	int i, *status = NULL;
	int devShift = 1; //skip binary
	int devCount = 0;
	pid_t *pid = NULL;
	BOOL blRead = FALSE;

	if (argc < 2 || !strcmp(argv[1], "-h")) {
		usage();
		exit(1);
	}

	if (!strcmp(argv[1], "-r")) {
		blRead = TRUE;
		devShift++;
	} else if (!strcmp(argv[1], "-w")) {
		blRead = FALSE;
		devShift++;
	}

	setpriority(PRIO_PROCESS, 0, -1);
	bzero(&gszBuf, sizeof(gszBuf));
	devCount = argc - devShift;
	pid = calloc(devCount, sizeof(int));
	status = calloc(devCount, sizeof(int));

	for (i = 0; i < devCount; i++) {
		if (0 > (pid[i] = fork())) {
			syslog(LOG_ERR, "(%s/%d) fork failed! errno=%m", __FILE__, __LINE__);
			goto END;
		}
		if (0 == pid[i]) { // child
			err = SynoddOne(argv[i + devShift], blRead);
			goto END;
		}
	}

	err = 0;
	for (i = 0; i < devCount; i++) {
		printf("waitpid: %d, synodd[%s]:%s\n"
			   , pid[i], argv[i + devShift], strerror(WEXITSTATUS(status[i])));
		waitpid(pid[i], &status[i], 0);
		if (0 != WEXITSTATUS(status[i])) {
			err = -1;
		}
	}

END:
	if (pid) free(pid);
	if (status) free(status);
	exit(err);
}

