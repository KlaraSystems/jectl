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
#include <getopt.h>
#include <libzfs_impl.h>

#include "jectl.h"

libzfs_handle_t *lzh;

const char *jepool = "zroot/JE";
const char *jeroot = "zroot/JAIL";

static void
usage(void)
{
	fprintf(stderr, "usage: jectl <command> ...\n\n");
	fprintf(stderr, "Commands:\n");
	fprintf(stderr, "    activate <jailname> <jailenv>	- activate jail environment\n");
	fprintf(stderr, "    dump [jailname]			- print detailed information\n");
	fprintf(stderr, "    import <jailname|jailenv>		- receive ZFS replication stream\n");
	fprintf(stderr, "    list [jailname]			- proxy to zfs list, no options accepted\n");
	fprintf(stderr, "    mount <jailname> <mountpoint>	- mount jail at given path\n");
	fprintf(stderr, "    umount <jailname>			- unmount jail\n");
	fprintf(stderr, "    update <jailname> [mountpoint]	- update jail and optionally mount\n");
	exit(1);
}

static int
init_root(void)
{
	nvlist_t *nvl;

	nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP);
	nvlist_add_string(nvl, "canmount", "off");
	nvlist_add_string(nvl, "mountpoint", "none");

	if (!zfs_dataset_exists(lzh, jeroot, ZFS_TYPE_FILESYSTEM)) {
		if (zfs_create(lzh, jeroot, ZFS_TYPE_FILESYSTEM, nvl) != 0) {
			fprintf(stderr, "jectl: cannot create %s\n", jeroot);
			return (1);
		}
		printf("create %s\n", jeroot);
	}

	if (!zfs_dataset_exists(lzh, jepool, ZFS_TYPE_FILESYSTEM)) {
		if (zfs_create(lzh, jepool, ZFS_TYPE_FILESYSTEM, nvl) != 0) {
			fprintf(stderr, "jectl: cannot create %s\n", jepool);
			return (1);
		}
		printf("create %s\n", jepool);
	}

	nvlist_free(nvl);
	return (0);
}

static int
jectl_list(int argc __unused, char **argv __unused)
{
	zfs_handle_t *jds;

	switch (argc) {
	case 1:
		execl("/sbin/zfs", "zfs", "list", "-r", jepool, jeroot, NULL);
		break;
	case 2:
		if ((jds = get_jail_dataset(argv[1])) == NULL)
			return (1);
		execl("/sbin/zfs", "zfs", "list", "-r", zfs_get_name(jds), NULL);
		zfs_close(jds);
		break;
	default:
		fprintf(stderr, "usage: jectl list [jailname]\n");
		exit(1);
	}

	return (1);
}
JE_COMMAND(jectl, list, jectl_list);

int
main(int argc, char *argv[])
{
	struct jectl_command **jc;

	if (argc < 2)
		usage();
	argv++;
	argc--;

	if ((lzh = libzfs_init()) == NULL)
		return (1);

	libzfs_print_on_error(lzh, B_TRUE);

	if (init_root() != 0)
		return (1);

	SET_FOREACH(jc, jectl) {
		if (strcmp((*jc)->name, argv[0]) == 0)
			return ((*jc)->handler(argc, argv));
	}

	fprintf(stderr, "jectl: sub-command not found: %s\n", argv[0]);
	return (1);
}
