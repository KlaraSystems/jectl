/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <libzfs_impl.h>

static libzfs_handle_t *lzh;

static const char *jepool = "zroot/JE";
static const char *jeroot = "zroot/JAIL";

static void
usage(void)
{
	printf(
	    "usage: jectl\n"
	    "       [--jail=<jailname>]\n"
	    "       [--import=<jailname|jailenv>]\n"
	    "       [--info | --info=<jailenv>]\n"
	    "       [--set-je=<jailenv>]\n"
	    "       [--mount=<mountpoint>]\n"
	    "       [--umount]\n"
	    "       [--update]\n");
	exit(1);
}

static int
get_user_prop(zfs_handle_t *zhp, const char *property, char **val)
{
	int error;
	nvlist_t *nvl, *propval;

	if ((nvl = zfs_get_user_props(zhp)) == NULL)
		return (-1);

	if ((error = nvlist_lookup_nvlist(nvl, property, &propval)) != 0)
		return (error);

	if ((error = nvlist_lookup_string(propval, ZPROP_VALUE, val)) != 0)
		return (error);

	/* user property has been "unset" */
	if (strcmp(*val, "") == 0)
		return (-1);

	return (0);
}

static int
set_property(zfs_handle_t *zhp, const char *property, const char *val)
{
	nvlist_t *nvl;

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
		return (ENOMEM);

	nvlist_add_string(nvl, property, val);

	zfs_prop_set_list(zhp, nvl);

	free(nvl);

	return (0);
}

/* compare a property between two datasets */
static bool
is_equal(zfs_handle_t *a, zfs_handle_t *b, const char *property)
{
	char *astr, *bstr;
	int error1, error2;

	error1 = get_user_prop(a, property, &astr);
	error2 = get_user_prop(b, property, &bstr);

	/* neither dataset has this property */
	if (error1 && error2)
		return (true);
	/* one of the datasets has this property */
	else if (error1 || error2)
		return (false);

	return (strcmp(astr, bstr) == 0);
}

static zfs_handle_t *
search_jepool(const char *target)
{
	char name[ZFS_MAXPROPLEN];

	snprintf(name, sizeof(name), "%s/%s", jepool, target);

	if (!zfs_dataset_exists(lzh, name, ZFS_TYPE_FILESYSTEM))
		return (NULL);

	return (zfs_open(lzh, name, ZFS_TYPE_FILESYSTEM));
}

/*
 * unmount zhp and child datasets inheriting zhp's mountpoint
 */
static int
do_unmountall(zfs_handle_t *zhp)
{
	if (!zfs_is_mounted(zhp, NULL))
		return (0);

	return (zfs_unmountall(zhp, 0));
}

static zfs_handle_t *
get_jail_dataset(const char *jailname)
{
	char jds_name[ZFS_MAXPROPLEN];

	/* path to jail dataset, zroot/JAIL/$jailname */
	snprintf(jds_name, sizeof(jds_name), "%s/%s", jeroot, jailname);

	return (zfs_open(lzh, jds_name, ZFS_TYPE_FILESYSTEM));
}

/* get active jail environment */
static zfs_handle_t *
get_active_je(zfs_handle_t *jds)
{
	char *je_name;

	if (get_user_prop(jds, "je:active", &je_name) != 0)
		return (NULL);

	return (zfs_open(lzh, je_name, ZFS_TYPE_FILESYSTEM));
}

static void
print_je(zfs_handle_t *je)
{
	char *value;

	if (get_user_prop(je, "je:active", &value) == 0 &&
	    strcmp(value, zfs_get_name(je)) == 0)
		printf("je:active=%s\n", value);
	if (get_user_prop(je, "je:version", &value) == 0)
		printf("je:version=%s\n", value);
	if (get_user_prop(je, "je:poudriere:freebsd_version", &value) == 0)
		printf("je:poudriere:freebsd_version=%s\n", value);
	if (get_user_prop(je, "je:poudriere:jailname", &value) == 0)
		printf("je:poudriere:jailname=%s\n", value);
	if (get_user_prop(je, "je:poudriere:overlaydir", &value) == 0)
		printf("je:poudriere:overlaydir=%s\n", value);
	if (get_user_prop(je, "je:poudriere:packagelist", &value) == 0)
		printf("je:poudriere:packagelist=%s\n", value);

	return;
}

static void
print_jds(zfs_handle_t *jds)
{
	zfs_handle_t *je;

	if ((je = get_active_je(jds)) == NULL)
		return;

	print_je(je);
	zfs_close(je);
}

static void
je_print(zfs_handle_t *jds, const char *jename)
{
	zfs_handle_t *zhp;
	char name[ZFS_MAX_DATASET_NAME_LEN];

	if (jds == NULL && jename == NULL) {
		printf("jectl: cannot print: specify --jail flag or argument to --info=<je>\n");
		return;
	}

	if (jds != NULL && jename == NULL) {
		print_jds(jds);
		return;
	} else if (jds != NULL && jename != NULL) {
		snprintf(name, sizeof(name), "%s/%s", zfs_get_name(jds), jename);
	} else if (jds == NULL && jename != NULL) {
		snprintf(name, sizeof(name), "%s/%s", jepool, jename);
	}

	if (!zfs_dataset_exists(lzh, name, ZFS_TYPE_FILESYSTEM)) {
		printf("jectl: cannot print '%s': does not exist\n", name);
		return;
	}

	if ((zhp = zfs_open(lzh, name, ZFS_TYPE_FILESYSTEM)) == NULL)
		return;
	print_je(zhp);
	zfs_close(zhp);
}

static int
destroy_cb(zfs_handle_t *zhp, void *arg __unused)
{
	zfs_destroy(zhp, false);
	return (0);
}

static void
je_destroy(zfs_handle_t *zhp)
{
	zfs_iter_dependents(zhp, B_TRUE, destroy_cb, NULL);
	zfs_destroy(zhp, false);
}

static int
gather_cb(zfs_handle_t *zhp, void *arg __unused)
{
	get_all_cb_t *cb = arg;

	if (zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM)
		libzfs_add_handle(cb, zhp);
	else
		zfs_close(zhp);

	return (0);
}

static int
mount_one(zfs_handle_t *zhp, void *arg __unused)
{
	int error;

	if ((error = zfs_mount(zhp, NULL, 0)) != 0)
		zfs_close(zhp);

	return (error);
}

/* mount jail environment */
static int
je_mount(zfs_handle_t *jds, const char *mountpoint)
{
	zfs_handle_t *je;

	if ((je = get_active_je(jds)) == NULL) {
		printf("cannot find active jail environment: %s\n", zfs_get_name(jds));
		return (-1);
	}

	if (do_unmountall(je) != 0) {
		zfs_close(je);
		return (-1);
	}

	if (zfs_prop_set(je, "mountpoint", mountpoint) != 0) {
		zfs_close(je);
		return (-1);
	}

	get_all_cb_t cb = { 0 };
	libzfs_add_handle(&cb, je);
	zfs_iter_dependents(je, B_TRUE, gather_cb, &cb);
	zfs_foreach_mountpoint(lzh, cb.cb_handles, cb.cb_used,
	    mount_one, NULL, B_TRUE);

	zfs_close(je);
	return (0);
}

static int
do_rename(zfs_handle_t *zhp, void *arg)
{
	struct renameflags flags = { 0 };
	char dest[ZFS_MAX_DATASET_NAME_LEN];
	nvlist_t *nvl;
	char *src, *target, *renaming, *tofree;

	nvl = arg;

	if (nvlist_lookup_string(nvl, "src", &src) != 0)
		goto next;

	if (nvlist_lookup_string(nvl, "target", &target) != 0)
		goto next;

	if ((renaming = strdup(zfs_get_name(zhp))) == NULL)
		goto next;

	tofree = renaming;
	renaming += strlen(src) + 1;

	snprintf(dest, sizeof(dest), "%s/%s", target, renaming);

	zfs_rename(zhp, dest, flags);

	free(tofree);
	return (0);
next:
	zfs_close(zhp);
	return (0);
}

/*
 * move (rename) all child datasets from src to target
 */
static int
je_rename(zfs_handle_t *src, zfs_handle_t *target)
{
	nvlist_t *nvl;

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
		return (ENOMEM);

	nvlist_add_string(nvl, "src", zfs_get_name(src));
	nvlist_add_string(nvl, "target", zfs_get_name(target));

	zfs_iter_filesystems(src, do_rename, nvl);

	nvlist_free(nvl);
	return (0);
}

/* clone target dataset to jds */
static zfs_handle_t *
je_copy(zfs_handle_t *jds, zfs_handle_t *target)
{
	char clone_name[ZFS_MAX_DATASET_NAME_LEN];
	char snapshot_name[ZFS_MAX_DATASET_NAME_LEN];
	zfs_handle_t *je, *snapshot;
	char *jename, *tofree, *version;
	const char *root_dataset;
	int error;

	je = NULL;

	if (strnstr(zfs_get_name(target), jepool, strlen(jepool)))
		root_dataset = jepool;
	else
		root_dataset = jeroot;

	/* then clone it into the jail dataset */
	jename = tofree = strdup(zfs_get_name(target));
	jename += strlen(root_dataset) + 1;

	snprintf(clone_name, sizeof(clone_name), "%s/%s", zfs_get_name(jds), jename);

	if (!zfs_dataset_exists(lzh, clone_name, ZFS_TYPE_FILESYSTEM)) {
		/* create a snapshot of provided jail environment */
		snprintf(snapshot_name, sizeof(snapshot_name), "%s@%s", zfs_get_name(target), "jectl");
		if (!zfs_dataset_exists(lzh, snapshot_name, ZFS_TYPE_SNAPSHOT) &&
		    zfs_snapshot(lzh, snapshot_name, B_FALSE, NULL) != 0)
			return (NULL);

		snapshot = zfs_open(lzh, snapshot_name, ZFS_TYPE_SNAPSHOT);
		if (snapshot == NULL)
			return (NULL);
		error = zfs_clone(snapshot, clone_name, NULL);
		zfs_close(snapshot);
		if (error)
			goto done;
	}

	if ((je = zfs_open(lzh, clone_name, ZFS_TYPE_FILESYSTEM)) == NULL)
		goto done;

	/* copy properties over */
	zfs_prop_set(je, "canmount", "noauto");
	if (get_user_prop(target, "je:version", &version) == 0)
		set_property(je, "je:version", version);
	if (get_user_prop(target, "je:poudriere:freebsd_version", &version) == 0)
		set_property(je, "je:poudriere:freebsd_version", version);
	if (get_user_prop(target, "je:poudriere:jailname", &version) == 0)
		set_property(je, "je:poudriere:jailname", version);
	if (get_user_prop(target, "je:poudriere:overlaydir", &version) == 0)
		set_property(je, "je:poudriere:overlaydir", version);
	if (get_user_prop(target, "je:poudriere:packagelist", &version) == 0)
		set_property(je, "je:poudriere:packagelist", version);

done:
	zfs_close(target);
	free(tofree);
	return (je);
}

struct compare_info {
	zfs_handle_t *je;
	zfs_handle_t *result;
};

static int
compare_je(zfs_handle_t *zhp, void *arg)
{
	struct compare_info *ci __unused;
	zfs_handle_t *je;
	char *v1, *v2;

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

	if (get_user_prop(je, "je:poudriere:freebsd_version", &v1) != 0)
		goto done;
	if (get_user_prop(zhp, "je:poudriere:freebsd_version", &v2) != 0)
		goto done;

	if (strtoul(v1, NULL, 10) < strtoul(v2, NULL, 10)) {
		printf("jectl: updating from %s to %s\n", zfs_get_name(je), zfs_get_name(zhp));
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
		printf("cannot find active jail environment: %s\n", zfs_get_name(jds));
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

	return (je_copy(jds, ci.result));
}

static int
je_swapin(zfs_handle_t *jds, zfs_handle_t *target)
{
	zfs_handle_t *src;

	if ((src = get_active_je(jds)) == NULL) {
		set_property(jds, "je:active", zfs_get_name(target));
		return (0);
	}

	/* already the active dataset */
	if (strcmp(zfs_get_name(src), zfs_get_name(target)) == 0) {
		zfs_close(src);
		return (0);
	}

	if (do_unmountall(src) != 0) {
		zfs_close(src);
		return (-1);
	}

	if (do_unmountall(target) != 0) {
		zfs_close(src);
		return (-1);
	}

	/* move child datasets from src to target */
	je_rename(src, target);

	/* set new jail environment */
	set_property(jds, "je:active", zfs_get_name(target));

	zfs_close(src);
	return (0);
}

static int
je_update(zfs_handle_t *jds __unused)
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
je_set(zfs_handle_t *jds, const char *target)
{
	int error;
	char name[ZFS_MAXPROPLEN];
	zfs_handle_t *next, *zhp;

	snprintf(name, sizeof(name), "%s/%s", zfs_get_name(jds), target);

	if (!zfs_dataset_exists(lzh, name, ZFS_TYPE_FILESYSTEM)) {
		if ((zhp = search_jepool(target)) == NULL)
			return (-1);
		next = je_copy(jds, zhp);
	} else {
		next = zfs_open(lzh, name, ZFS_TYPE_FILESYSTEM);
	}

	if (next == NULL)
		return (-1);

	error = je_swapin(jds, next);

	zfs_close(next);

	return (error);
}

static int
je_unmount(zfs_handle_t *jds)
{
	int error;
	zfs_handle_t *je;

	if ((je = get_active_je(jds)) == NULL) {
		printf("jectl: cannot find active jail environment: %s\n", zfs_get_name(jds));
		return (-1);
	}

	error = do_unmountall(je);

	zfs_close(je);

	return (error);
}

static int
je_import(const char *import_name)
{
	nvlist_t *props;
	zfs_handle_t *zfs;
	recvflags_t flags = { .nomount = 1 };
	char name[ZFS_MAXPROPLEN];
	char *default_je;
	struct renameflags rflags = { 0 };

	snprintf(name, sizeof(name), "%s/jectl.XXXXXX", jeroot);
	if (mktemp(name) == NULL)
		return (1);

	if (zfs_receive(lzh, name, NULL, &flags, STDIN_FILENO, NULL) != 0)
		return (-1);

	if ((zfs = zfs_open(lzh, name, ZFS_TYPE_FILESYSTEM)) == NULL)
		return (-1);

	if (get_user_prop(zfs, "je:poudriere:create", &default_je) == 0)
		snprintf(name, sizeof(name), "%s/%s", jeroot, import_name);
	else
		snprintf(name, sizeof(name), "%s/%s", jepool, import_name);

	if (zfs_dataset_exists(lzh, name, ZFS_TYPE_FILESYSTEM)) {
		printf("jectl: cannot import '%s': jail dataset already exists\n", import_name);
		je_destroy(zfs);
		return (-1);
	}

	if (zfs_rename(zfs, name, rflags) != 0) {
		je_destroy(zfs);
		return (-1);
	}

	zfs_close(zfs);
	zfs = zfs_open(lzh, name, ZFS_TYPE_FILESYSTEM);

	nvlist_alloc(&props, NV_UNIQUE_NAME, KM_SLEEP);
	nvlist_add_string(props, "canmount", "noauto");
	nvlist_add_string(props, "mountpoint", "none");
	zfs_prop_set_list(zfs, props);
	nvlist_free(props);

	if (get_user_prop(zfs, "je:poudriere:create", &default_je) == 0) {
		je_set(zfs, default_je);
		set_property(zfs, "je:poudriere:create", "");
	}

	zfs_close(zfs);

	return (0);
}

static zfs_handle_t *
create_jail_dataset(const char *dataset)
{
	int error;
	nvlist_t *nvl;

	nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP);
	nvlist_add_string(nvl, "canmount", "off");
	nvlist_add_string(nvl, "mountpoint", "none");

	error = zfs_create(lzh, dataset, ZFS_TYPE_FILESYSTEM, nvl);

	nvlist_free(nvl);

	if (error != 0)
		return (NULL);

	return (zfs_open(lzh, dataset, ZFS_TYPE_FILESYSTEM));
}

static int
init_root(void)
{
	zfs_handle_t *zhp;

	if (!zfs_dataset_exists(lzh, jeroot, ZFS_TYPE_FILESYSTEM)) {
		if ((zhp = create_jail_dataset(jeroot)) == NULL) {
			printf("jectl: cannot create %s\n", jeroot);
			return (-1);
		}
		printf("create %s\n", jeroot);
		zfs_close(zhp);
	}

	if (!zfs_dataset_exists(lzh, jepool, ZFS_TYPE_FILESYSTEM)) {
		if ((zhp = create_jail_dataset(jepool)) == NULL) {
			printf("jectl: cannot create %s\n", jepool);
			return (-1);
		}
		printf("create %s\n", jepool);
		zfs_close(zhp);
	}

	return (0);
}

enum {
	HELP = 1,
	INFO,
	IMPORT,
	JAIL,
	SET_JE,
	MOUNT,
	UNMOUNT,
	UPDATE,
};

static const struct option prog_options[] = {
	{ "help", no_argument, NULL, HELP },
	{ "info", optional_argument, NULL, INFO },
	{ "import", required_argument, NULL, IMPORT },
	{ "jail", required_argument, NULL, JAIL },
	{ "set-je", required_argument, NULL, SET_JE },
	{ "mount", required_argument, NULL, MOUNT },
	{ "umount", no_argument, NULL, UNMOUNT },
	{ "unmount", no_argument, NULL, UNMOUNT },
	{ "update", no_argument, NULL, UPDATE },
};

int
main(int argc, char *argv[])
{
	int ch;
	zfs_handle_t *jds;
	bool set_info, import, set_je, mount, unmount, update;
	const char *jailname, *mountpoint, *setje, *import_name, *setinfo;

	jailname = mountpoint = import_name = setinfo = setje =  NULL;
	set_info = import = set_je = mount = unmount = update = false;

	while ((ch = getopt_long(argc, argv, "", prog_options, NULL)) != -1) {
		switch (ch) {
		case HELP:
			usage();
		case INFO:
			set_info = true;
			if (optarg == NULL)
				setinfo = NULL;
			else
				setinfo = optarg;
			break;
		case IMPORT:
			import = true;
			import_name = optarg;
			break;
		case JAIL:
			jailname = optarg;
			break;
		case SET_JE:
			set_je = true;
			setje = optarg;
			break;
		case MOUNT:
			mount = true;
			mountpoint = optarg;
			break;
		case UNMOUNT:
			unmount = true;
			break;
		case UPDATE:
			update = true;
			break;
		default:
			usage();
			break;
		}
	}

	if ((lzh = libzfs_init()) == NULL)
		return (-1);

	libzfs_print_on_error(lzh, B_TRUE);

	if (init_root() != 0)
		return (-1);

	if (import)
		return (je_import(import_name));

	if (set_info && jailname == NULL) {
		je_print(NULL, setinfo);
		return (0);
	}

	/* jailname required from here on */
	if (jailname == NULL) {
		printf("jectl: missing --jail flag\n");
		return (-1);
	}

	if ((jds = get_jail_dataset(jailname)) == NULL)
		return (-1);

	if (unmount)
		return (je_unmount(jds));

	if (set_je && je_set(jds, setje) != 0) {
		printf("jectl: failed to set jail environment: %s\n", setje);
		return (-1);
	}

	if (update && je_update(jds) != 0)
		printf("jectl: failed while updating jail: %s\n", zfs_get_name(jds));

	if (set_info)
		je_print(jds, setinfo);

	if (mount)
		return (je_mount(jds, mountpoint));

	return (0);
}
