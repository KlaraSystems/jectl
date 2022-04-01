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

/*
 * unmount zhp and child datasets inheriting zhp's mountpoint
 */
int
je_unmount(zfs_handle_t *zhp)
{
	if (!zfs_is_mounted(zhp, NULL))
		return (0);

	return (zfs_unmountall(zhp, 0));
}

static int
jectl_unmount(int argc, char **argv)
{
	int error;
	zfs_handle_t *je, *jds;

	if (argc != 2) {
		fprintf(stderr, "usage: jectl umount <jailname>\n");
		return (1);
	}

	if ((jds = get_jail_dataset(argv[1])) == NULL)
		return (1);

	if ((je = get_active_je(jds)) == NULL) {
		zfs_close(jds);
		return (1);
	}

	error = je_unmount(je);

	zfs_close(je);
	zfs_close(jds);

	return (error);
}
JE_COMMAND(jectl, umount, jectl_unmount);
