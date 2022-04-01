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
 * zfs recv into a temporary dataset to peek at the user properties.
 * If je:poudriere:create is set, the temporary dataset will be renamed
 * to $jeroot/$import_name; this is how a jail is created.
 * Otherwise, the temporary dataset is renamed to $jepool/$import_name
 * so that it can be consumed as a jail environment.
 */
static int
je_import(const char *import_name)
{
	nvlist_t *props;
	zfs_handle_t *zhp;
	recvflags_t flags = { .nomount = 1 };
	char name[ZFS_MAXPROPLEN];
	char *default_je;
	struct renameflags rflags = { 0 };

	snprintf(name, sizeof(name), "%s/jectl.XXXXXX", jeroot);
	if (mktemp(name) == NULL)
		return (1);

	if (zfs_receive(lzh, name, NULL, &flags, STDIN_FILENO, NULL) != 0)
		return (1);

	if ((zhp = zfs_open(lzh, name, ZFS_TYPE_FILESYSTEM)) == NULL)
		return (1);

	if (get_property(zhp, "je:poudriere:create", &default_je) == 0)
		snprintf(name, sizeof(name), "%s/%s", jeroot, import_name);
	else
		snprintf(name, sizeof(name), "%s/%s", jepool, import_name);

	if (zfs_dataset_exists(lzh, name, ZFS_TYPE_FILESYSTEM)) {
		fprintf(stderr, "jectl: cannot import '%s': jail dataset already exists\n", import_name);
		je_destroy(zhp);
		return (1);
	}

	if (zfs_rename(zhp, name, rflags) != 0) {
		je_destroy(zhp);
		return (1);
	}

	/*
	 * zhp goes stale after the rename, refresh it
	 */
	zfs_close(zhp);
	if ((zhp = zfs_open(lzh, name, ZFS_TYPE_FILESYSTEM)) == NULL) {
		fprintf(stderr, "cannot open imported dataset '%s'\n", name);
		return (1);
	}

	nvlist_alloc(&props, NV_UNIQUE_NAME, KM_SLEEP);

	if (get_property(zhp, "je:poudriere:create", &default_je) == 0) {
		/* XXX: should be set by nvlist_add_string */
		je_activate(zhp, default_je);

		nvlist_add_string(props, "canmount", "off");
		nvlist_add_string(props, "mountpoint", "none");
		nvlist_add_string(props, "je:poudriere:create", "");
	} else {
		nvlist_add_string(props, "canmount", "noauto");
		nvlist_add_string(props, "mountpoint", "none");
	}

	zfs_prop_set_list(zhp, props);
	nvlist_free(props);

	zfs_close(zhp);

	return (0);
}

static int
jectl_import(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: jectl import <jailname|jailenv>\n");
		exit(1);
	}

	return (je_import(argv[1]));
}
JE_COMMAND(jectl, import, jectl_import);

