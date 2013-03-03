#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

extern int GetKeyValue(const char *szFile, const char *szKey, char *szValue, int size);

static void usage(void)
{
	printf("Usages: get_key_value file key\n");
}

int get_key_value_main(int argc, char **argv);
int get_key_value_main(int argc, char **argv)
{
	char szBuf[4096] = {'\0'};
	int error = 0;

	if (argc != 3) {
		usage();
		exit(1);
	}

	error = GetKeyValue(argv[1], argv[2], szBuf, sizeof(szBuf));
	if (*szBuf != 0) {
		printf("%s\n", szBuf);
	}

	exit(error);
}
