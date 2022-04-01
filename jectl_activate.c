/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2022 Klara Inc.
 * Copyright (c) 2022 Rob Wing <rob.wing@klarasystems.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <libzfs_impl.h>

#include "jectl.h"

static zfs_handle_t *
search_jepool(const char *target)
{
	char name[ZFS_MAXPROPLEN];

	snprintf(name, sizeof(name), "%s/%s", jepool, target);

	if (!zfs_dataset_exists(lzh, name, ZFS_TYPE_FILESYSTEM))
		return (NULL);

	return (zfs_open(lzh, name, ZFS_TYPE_FILESYSTEM));
}

int
je_activate(zfs_handle_t *jds, const char *target)
{
	int error;
	char name[ZFS_MAXPROPLEN];
	zfs_handle_t *next, *zhp;

	snprintf(name, sizeof(name), "%s/%s", zfs_get_name(jds), target);

	if (zfs_dataset_exists(lzh, name, ZFS_TYPE_FILESYSTEM)) {
		next = zfs_open(lzh, name, ZFS_TYPE_FILESYSTEM);
	} else {
		if ((zhp = search_jepool(target)) == NULL)
			return (1);
		next = je_copy(zhp, jds);
	}

	if (next == NULL)
		return (1);

	error = je_swapin(jds, next);

	zfs_close(next);

	return (error);
}


static int
jectl_activate(int argc, char **argv)
{
	int error;
	zfs_handle_t *jds;

	if (argc != 3) {
		fprintf(stderr, "usage: jectl activate <jailname> <jailenv>\n");
		exit(1);
	}

	if ((jds = get_jail_dataset(argv[1])) == NULL)
		return (1);

	error = je_activate(jds, argv[2]);

	zfs_close(jds);

	return (error);
}
JE_COMMAND(jectl, activate, jectl_activate);

