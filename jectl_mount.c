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

static int
gather_cb(zfs_handle_t *zhp, void *arg __unused)
{
	get_all_cb_t *cb = arg;

	if (zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) {
		libzfs_add_handle(cb, zhp);
		zfs_iter_filesystems(zhp, gather_cb, cb);
	} else
		zfs_close(zhp);

	return (0);
}

static int
mount_one(zfs_handle_t *zhp, void *arg __unused)
{
	int error;

	error = zfs_mount(zhp, NULL, 0);

	zfs_close(zhp);
	return (error);
}

/* mount jail at given mountpoint */
int
je_mount(zfs_handle_t *jds, const char *mountpoint)
{
	zfs_handle_t *je;
	get_all_cb_t cb = { 0 };

	if ((je = get_active_je(jds)) == NULL) {
		fprintf(stderr,
		    "je_mount: cannot find active jail environment for '%s'\n",
		    zfs_get_name(jds));
		return (1);
	}

	/*
	 * XXX: work-around dying jails
	 *
	 * A dying jail can prevent the backing dataset from being unmounted.
	 * Do a forced unmount until dying jails can be cleaned properly.
	 */
	if (je_unmount(je, MNT_FORCE) != 0) {
		char mp[ZFS_MAXPROPLEN];

		zfs_prop_get(je, ZFS_PROP_MOUNTPOINT, mp, sizeof(mp),
		    NULL, NULL, 0, B_FALSE);
		fprintf(stderr,
		    "je_mount: cannot unmount '%s' from '%s'\n",
		    zfs_get_name(je), mp);
		return (1);
	}

	if (zfs_prop_set(je, "mountpoint", mountpoint) != 0) {
		fprintf(stderr,
		    "je_mount: cannot set mountpoint for '%s' at '%s'\n",
		    zfs_get_name(je), mountpoint);
		return (1);
	}

	libzfs_add_handle(&cb, je);
	zfs_iter_filesystems(je, gather_cb, &cb);
	zfs_foreach_mountpoint(lzh, cb.cb_handles, cb.cb_used,
	    mount_one, NULL, B_FALSE);

	return (0);
}

static int
jectl_mount(int argc, char **argv)
{
	int error;
	zfs_handle_t *jds;

	if (argc != 3) {
		fprintf(stderr, "usage: jectl mount <jailname> <mountpoint>\n");
		exit(1);
	}

	if ((jds = get_jail_dataset(argv[1])) == NULL)
		return (1);

	error = je_mount(jds, argv[2]);

	zfs_close(jds);

	return (error);
}
JE_COMMAND(jectl, mount, jectl_mount);
