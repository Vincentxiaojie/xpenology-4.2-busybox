/* vi: set sw=4 ts=4: */
/*
 * Small xz deflate implementation.
 * Copyright (C) 2006  Aurelien Jacobs <aurel@gnuage.org>
 *
 * Licensed under GPL v2, see file LICENSE in this tarball for details.
 */

#include "libbb.h"
#include "unarchive.h"

char FAST_FUNC get_header_tar_xz(archive_handle_t *archive_handle)
{
#if BB_MMU
	union {
		uint8_t b[4];
		uint16_t b16[2];
		uint32_t b32[1];
	} magic;
	bool blFound = false;
#endif
	/* Can't lseek over pipes */
	archive_handle->seek = seek_by_read;

	/* Check gzip magic only if open_transformer will invoke unpack_gz_stream (MMU case).
	 * Otherwise, it will invoke an external helper "gunzip -cf" (NOMMU case) which will
	 * need the header. */
#if BB_MMU
	xread(archive_handle->src_fd, &magic, 2);
	/* Can skip this check, but error message will be less clear */
	if (magic.b16[0] == XZ_MAGIC1) {
		xread(archive_handle->src_fd, magic.b32, sizeof(magic.b32[0]));
		if (magic.b32[0] == XZ_MAGIC2) {
			blFound = true;
		}
	}
	if (!blFound) {
		bb_error_msg_and_die("invalid xz magic");
	}
#endif

	open_transformer(archive_handle->src_fd, unpack_xz_stream, "unxz");
	archive_handle->offset = 0;
	while (get_header_tar(archive_handle) == EXIT_SUCCESS)
		continue;

	/* Can only do one file at a time */
	return EXIT_FAILURE;
}
