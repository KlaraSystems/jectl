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
#include <stdbool.h>
#include <libzfs_impl.h>

#include "jectl.h"

struct compare_info {
	zfs_handle_t *je;
	zfs_handle_t *result;
};

/* compare a property between two datasets */
static bool
is_equal(zfs_handle_t *a, zfs_handle_t *b, const char *property)
{
	const char *astr, *bstr;
	int error1, error2;

	error1 = get_property(a, property, &astr);
	error2 = get_property(b, property, &bstr);

	/* neither dataset has this property */
	if (error1 && error2)
		return (true);
	/* one of the datasets has this property */
	else if (error1 || error2)
		return (false);

	return (strcmp(astr, bstr) == 0);
}

static int
compare_je(zfs_handle_t *zhp, void *arg)
{
	struct compare_info *ci __unused;
	zfs_handle_t *je;
	const char *v1, *v2;

	ci = arg;

	if (ci->result != NULL)
		je = ci->result;
	else
		je = ci->je;

	/* dont close our handle */
	if (je == zhp)
		return (0);

	if (!is_equal(je, zhp, "je:poudriere:jailname"))
		goto done;
	if (!is_equal(je, zhp, "je:poudriere:overlaydir"))
		goto done;
	if (!is_equal(je, zhp, "je:poudriere:packagelist"))
		goto done;

	if (get_property(je, "je:poudriere:freebsd_version", &v1) != 0)
		goto done;
	if (get_property(zhp, "je:poudriere:freebsd_version", &v2) != 0)
		goto done;

	if (strtoul(v1, NULL, 10) < strtoul(v2, NULL, 10)) {
		if (ci->result != NULL)
			zfs_close(ci->result);
		ci->result = zhp;
		return (0);
	}

done:
	zfs_close(zhp);
	return (0);
}

/*
 * only handles when FreeBSD_version is bumped
 * needs a more sophisticated update mechanism.
 */
static zfs_handle_t *
je_next(zfs_handle_t *jds)
{
	struct compare_info ci;
	zfs_handle_t *root, *je;

	if ((je = get_active_je(jds)) == NULL) {
		fprintf(stderr, "cannot find active jail environment: %s\n", zfs_get_name(jds));
		return (NULL);
	}

	root = zfs_open(lzh, jepool, ZFS_TYPE_FILESYSTEM);

	ci.je = je;
	ci.result = NULL;
	zfs_iter_filesystems(root, compare_je, &ci);

	zfs_close(je);
	zfs_close(root);

	if (ci.result == NULL)
		return (NULL);

	return (je_copy(ci.result, jds));
}

static int
je_update(zfs_handle_t *jds)
{
	int error;
	zfs_handle_t *next;

	/* no update found, return with no error */
	if ((next = je_next(jds)) == NULL)
		return (0);

	error = je_swapin(jds, next);

	zfs_close(next);

	return (error);
}

static int
jectl_update(int argc, char **argv)
{
	int error;
	zfs_handle_t *jds;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: jectl update <jailname> [mountpoint]\n");
		exit(1);
	}

	if ((jds = get_jail_dataset(argv[1])) == NULL)
		return (1);

	if (je_update(jds) != 0)
		fprintf(stderr, "cannot update '%s'\n", zfs_get_name(jds));

	if (argc == 3) {
		error = je_mount(jds, argv[2]);
	} else
		error = 0;

	zfs_close(jds);

	return (error);
}
JE_COMMAND(jectl, update, jectl_update);
