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

#include <sys/cdefs.h>
#include <sys/linker_set.h>

struct jectl_command {
	const char *name;
	int (*handler)(int argc, char **argv);
};

SET_DECLARE(jectl, struct jectl_command);

#define JE_COMMAND(set, name, function)				\
	static struct jectl_command name ## _jectl_command =	\
	{ #name, function };					\
	DATA_SET(set, name ## _jectl_command);

extern libzfs_handle_t *lzh;
extern const char *jepool;
extern const char *jeroot;

int get_property(zfs_handle_t *, const char *, char **);

zfs_handle_t * get_jail_dataset(const char *);
zfs_handle_t * get_active_je(zfs_handle_t *);

zfs_handle_t * je_copy(zfs_handle_t *, zfs_handle_t *);

int je_activate(zfs_handle_t *, const char *);
int je_destroy(zfs_handle_t *);
int je_mount(zfs_handle_t *, const char *);
int je_swapin(zfs_handle_t *, zfs_handle_t *);
int je_unmount(zfs_handle_t *);

