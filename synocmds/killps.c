#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "libbb.h"

int killps_main(int argc, char* argv[]);
int killps_main(int argc, char* argv[])
{
	char *const path = xmalloc(PATH_MAX + 2); /* to save stack */
	
	if (2 != argc) {
		return EXIT_FAILURE;
	}
	
	realpath(argv[1], path);
	kill_process_for_umount(path);
	
	return EXIT_SUCCESS;
}
