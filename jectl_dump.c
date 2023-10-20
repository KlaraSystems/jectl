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
#include <libgen.h>

#include "jectl.h"


static void
print_je(zfs_handle_t *je)
{
	char *name;
	const char *value;
	char buffer[ZFS_MAXPROPLEN];

	name = strdup(zfs_get_name(je));

	if (get_property(je, "je:active", &value) == 0 &&
	    strcmp(value, zfs_get_name(je)) == 0) {
		snprintf(buffer, sizeof(buffer), "%s (ACTIVE)", basename(name));
	} else
		snprintf(buffer, sizeof(buffer), "%s", basename(name));

	printf("  Name:              %s\n", buffer);

	if (get_property(je, "je:version", &value) == 0)
		printf("    branch:            %s\n", value);
	if (get_property(je, "je:poudriere:freebsd_version", &value) == 0)
		printf("    version:           %s\n", value);
	if (get_property(je, "je:poudriere:jailname", &value) == 0)
		printf("    poudriere-jail:    %s\n", value);
	if (get_property(je, "je:poudriere:overlaydir", &value) == 0)
		printf("    overlay:           %s\n", value);
	if (get_property(je, "je:poudriere:packagelist", &value) == 0)
		printf("    packagelist:       %s\n", value);

	free(name);
	return;
}

static int
print_jail_cb(zfs_handle_t *zhp, void *arg __unused)
{
	int *count = arg;

	printf("%d.", (*count)++);
	print_je(zhp);
	zfs_close(zhp);
	return (0);
}


static int
print_jail(zfs_handle_t *jds, void *arg __unused)
{
	int count;
	zfs_handle_t *je;

	if ((je = get_active_je(jds)) == NULL)
		return (0);

	char *name;
	name = strdup(zfs_get_name(jds));
	printf("Jail name: %s\n", basename(name));
	printf("Environments:\n");

	count = 1;
	zfs_iter_filesystems(jds, print_jail_cb, &count);
	printf("\n");

	return (0);
}

static int
print_all(void)
{
	int count;
	zfs_handle_t *zhp;

	count = 1;

	/* print jails */
	zhp = zfs_open(lzh, jeroot, ZFS_TYPE_FILESYSTEM);
	zfs_iter_filesystems(zhp, print_jail, NULL);
	zfs_close(zhp);

	printf("Available jail environments:\n");
	zhp = zfs_open(lzh, jepool, ZFS_TYPE_FILESYSTEM);
	zfs_iter_filesystems(zhp, print_jail_cb, &count);
	zfs_close(zhp);

	return (0);
}

static int
jectl_dump(int argc, char **argv)
{
	zfs_handle_t *jds;

	if (argc < 1 || argc > 2) {
		fprintf(stderr, "usage: jectl dump [jailname]\n");
		exit(1);
	}

	if (argc == 1) {
		print_all();
		return (0);
	}

	if ((jds = get_jail_dataset(argv[1])) == NULL)
		return (1);

	print_jail(jds, NULL);

	zfs_close(jds);

	return (0);
}
JE_COMMAND(jectl, dump, jectl_dump);

