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
#include <libgen.h>
#include <libzfs_impl.h>

#include "jectl.h"

/* get dataset for the given jail */
zfs_handle_t *
get_jail_dataset(const char *jailname)
{
	char jds_name[ZFS_MAXPROPLEN];

	/* path to jail dataset, zroot/JAIL/$jailname */
	snprintf(jds_name, sizeof(jds_name), "%s/%s", jeroot, jailname);

	return (zfs_open(lzh, jds_name, ZFS_TYPE_FILESYSTEM));
}

/* get active jail environment */
zfs_handle_t *
get_active_je(zfs_handle_t *jds)
{
	char *je_name;

	if (get_property(jds, "je:active", &je_name) != 0)
		return (NULL);

	return (zfs_open(lzh, je_name, ZFS_TYPE_FILESYSTEM));
}

int
get_property(zfs_handle_t *zhp, const char *property, char **val)
{
	int error;
	nvlist_t *nvl, *propval;

	if ((nvl = zfs_get_user_props(zhp)) == NULL)
		return (1);

	if ((error = nvlist_lookup_nvlist(nvl, property, &propval)) != 0)
		return (error);

	if ((error = nvlist_lookup_string(propval, ZPROP_VALUE, val)) != 0)
		return (error);

	/* user property has been "unset" */
	if (strcmp(*val, "") == 0)
		return (1);

	return (0);
}

/*
 * copy user properties from src to target
 */
static int
je_copy_user_props(zfs_handle_t *src, zfs_handle_t *target)
{
	struct nvpair *nvp;
	nvlist_t *nvl, *propval, *nnvl;
	char *value;

	if (nvlist_alloc(&nnvl, NV_UNIQUE_NAME, 0) != 0)
		return (ENOMEM);

	nvl = zfs_get_user_props(src);

	/*
	 * This seems like a hack.
	 * Trying to set the nvlist of src on target
	 * without going through a temporary nvlist
	 * doesn't work. I'm curious why that is.
	 */
	nvp = NULL;
	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		nvpair_value_nvlist(nvp, &propval);
		nvlist_lookup_string(propval, ZPROP_VALUE, &value);
		nvlist_add_string(nnvl, nvpair_name(nvp), value);
	}

	/* XXX: hmm..not sure if this should be set for every copy */
	nvlist_add_string(nnvl, "canmount", "noauto");

	zfs_prop_set_list(target, nnvl);

	nvlist_free(nnvl);
	return (0);
}


/*
 * Do the dirty work of copying a dataset:
 *  - take a snapshot of src
 *  - clone that snapshot to dest
 *  - return zfs handle to the clone (i.e., a new dataset)
 */
static zfs_handle_t *
je_copy_impl(zfs_handle_t *src, const char *dest)
{
	int error;
	zfs_handle_t *snapshot, *target;
	char snapshot_name[ZFS_MAX_DATASET_NAME_LEN];

	/* take a snapshot of target dataset */
	snprintf(snapshot_name, sizeof(snapshot_name), "%s@%s", zfs_get_name(src), "jectl");
	if (!zfs_dataset_exists(lzh, snapshot_name, ZFS_TYPE_SNAPSHOT) &&
	    zfs_snapshot(lzh, snapshot_name, B_FALSE, NULL) != 0)
			return (NULL);

	if ((snapshot = zfs_open(lzh, snapshot_name, ZFS_TYPE_SNAPSHOT)) == NULL)
		return (NULL);

	error = zfs_clone(snapshot, dest, NULL);

	zfs_close(snapshot);

	if (error != 0)
		return (NULL);

	if ((target = zfs_open(lzh, dest, ZFS_TYPE_FILESYSTEM)) != NULL)
		je_copy_user_props(src, target);

	return (target);
}

/*
 * copy src to target, the copied dataset becomes a child of target
 */
zfs_handle_t *
je_copy(zfs_handle_t *src, zfs_handle_t *target)
{
	char dest[ZFS_MAX_DATASET_NAME_LEN];
	zfs_handle_t *je;
	char *name;

	name = strdup(zfs_get_name(src));

	snprintf(dest, sizeof(dest), "%s/%s", zfs_get_name(target), basename(name));

	if (zfs_dataset_exists(lzh, dest, ZFS_TYPE_FILESYSTEM)) {
		je = zfs_open(lzh, dest, ZFS_TYPE_FILESYSTEM);
	} else {
		je = je_copy_impl(src, dest);
	}

	zfs_close(src);
	free(name);
	return (je);
}

static int
destroy_cb(zfs_handle_t *zhp, void *arg __unused)
{
	zfs_destroy(zhp, false);
	return (0);
}

int
je_destroy(zfs_handle_t *zhp)
{
	zfs_iter_dependents(zhp, B_TRUE, destroy_cb, NULL);
	return (zfs_destroy(zhp, false));
}

static int
rename_cb(zfs_handle_t *src, void *arg)
{
	zfs_handle_t *target;
	struct renameflags flags = { 0 };
	char *name;
	char dest[ZFS_MAX_DATASET_NAME_LEN];

	target = arg;

	name = strdup(zfs_get_name(src));

	snprintf(dest, sizeof(dest), "%s/%s", zfs_get_name(target), basename(name));

	zfs_rename(src, dest, flags);

	zfs_close(src);
	free(name);
	return (0);
}

/*
 * move (rename) all child datasets from src to target
 */
static int
je_rename(zfs_handle_t *src, zfs_handle_t *target)
{
	zfs_iter_filesystems(src, rename_cb, target);

	return (0);
}

/*
 * set target to be the active jail environment
 */
int
je_swapin(zfs_handle_t *jds, zfs_handle_t *target)
{
	zfs_handle_t *src;

	if ((src = get_active_je(jds)) == NULL) {
		zfs_prop_set(jds, "je:active", zfs_get_name(target));
		return (0);
	}

	/* already the active dataset */
	if (strcmp(zfs_get_name(src), zfs_get_name(target)) == 0) {
		zfs_close(src);
		return (0);
	}

	if (je_unmount(src) != 0 || je_unmount(target) != 0) {
		zfs_close(src);
		return (1);
	}

	/* move child datasets from src to target */
	je_rename(src, target);

	/* set new jail environment */
	zfs_prop_set(jds, "je:active", zfs_get_name(target));

	zfs_close(src);
	return (0);
}
