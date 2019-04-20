/*-
 * Copyright (c) 2019 Duncan Overbruck <mail@duncano.de>.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef __linux__
#include <mntent.h>
#endif

#include "xbps_api_impl.h"

struct mountpoint {
	char *path;
	size_t len;
	uint64_t instsize, rmsize;
};

static const char *ignore[] = {
	"/dev",
	"/proc",
	"/run",
	"/sys",
	"/tmp",
};

static int
mountcmp(const void *a, const void *b)
{
	const struct mountpoint *ma = *(const struct mountpoint * const *)a;
	const struct mountpoint *mb = *(const struct mountpoint * const *)b;
	return -strcmp(ma->path, mb->path);
}

#ifdef __linux__
static size_t
get_mountpoints(struct xbps_handle *xhp, struct mountpoint ***pmounts)
{
	FILE *fp;
	struct mntent *ent;
	size_t nmounts = 0;
	struct mountpoint **mounts, *m;
	size_t allocmounts = 16;

	assert(xhp);

	fp = setmntent("/proc/mounts", "r");
	if (fp == NULL) {
		xbps_dbg_printf(xhp, "%s: setmntent failed: %s\n", __func__, strerror(errno));
		return 0;
	}

	mounts = calloc(allocmounts, sizeof (struct mountpoint *));
	assert(mounts);

	while ((ent = getmntent(fp)) != NULL) {
		bool found = false;
		for (unsigned int i = 0; !found && i < __arraycount(ignore); i++) {
			found = strncmp(ignore[i], ent->mnt_dir, strlen(ignore[i])) == 0;
		}
		if (found)
			continue;
		if (nmounts+1 >= allocmounts) {
			allocmounts *= 2;
			mounts = realloc(mounts, allocmounts * sizeof (struct mountpoint *));
			assert(mounts);
		}
		m = calloc(1, sizeof (struct mountpoint));
		assert(m);
		m->path = strdup(ent->mnt_dir);
		m->len = strlen(ent->mnt_dir);
		assert(m->path);
		mounts[nmounts++] = m;
	}
	endmntent(fp);
	*pmounts = mounts;
	return nmounts;
}
#else
get_mountpoints(struct xbps_handle *xhp)
{
	xbps_dictionary_t d;

	d = xbps_dictionary_create();
	assert(d);

	if (statvfs(ent->mnt_dir, &svfs) == -1) {
		xbps_dbg_printf(xhp, "%s: statvfs failed: %s\n", __func__, strerror(errno));
		goto out;
	}

	xbps_dictionary_set_uint64(d, "/", svfs.f_bfree * svfs.f_bsize);

out:
	return d;
}
#endif

#if 0
static struct mountpoint *
find_mountpoint(struct mountpoint **mnts, size_t nmnts, const char *dir)
{
	struct mountpoint *mnt;
	size_t len = strlen(dir);
	for (unsigned int i = 0; i < nmnts; i++) {
		if ((mnt = mnts[i]) == NULL)
			break;
		if (strncmp(mnt->path, dir, mnt->len) == 0) {
			if (len >= mnt->len && (dir[mnt->len-1] == '/' || dir[mnt->len-1] == '\0'))
				return mnt;
		}
	}
	return NULL;
}

static void
update_mountpoint_size(struct mountpoint **mounts, size_t nmounts, xbps_dictionary_t pkgd, bool add)
{
	xbps_object_t obj;
	xbps_dictionary_t dirs;
	xbps_object_iterator_t iter;
	struct mountpoint *mnt;
	uint64_t size;

	dirs = xbps_dictionary_get(pkgd, "dirs");
	if (dirs == NULL)
		goto fallback;

	iter = xbps_dictionary_iterator(dirs);
	if (iter == NULL)
		goto fallback;

	while ((obj = xbps_object_iterator_next(iter)) != NULL) {
		const char *file;
		xbps_dictionary_t dir = xbps_dictionary_get_keysym(dirs, obj);
		xbps_dictionary_get_cstring_nocopy(dir, "file", &file);
		xbps_dictionary_get_uint64(dir, "size", &size);
		mnt = find_mountpoint(mounts, nmounts, file);
		if (mnt == NULL)
			continue;
		if (add) {
			mnt->instsize += size;
		} else {
			mnt->rmsize -= size;
		}
	}
	return;

fallback:
	mnt = find_mountpoint(mounts, nmounts, "/");
	if (mnt == NULL)
		return;
	xbps_dictionary_get_uint64(pkgd, "installed_size", &size);
	if (add) {
		mnt->instsize += size;
	} else {
		mnt->rmsize -= size;
	}
}
#endif

static struct archive *
open_archive(const char *fname)
{
	struct archive *a;

	if ((a = archive_read_new()) == NULL)
		return NULL;

	archive_read_support_compression_gzip(a);
	archive_read_support_compression_bzip2(a);
	archive_read_support_compression_xz(a);
	archive_read_support_format_tar(a);

	if (archive_read_open_filename(a, fname, 32768)) {
		archive_read_finish(a);
		return NULL;
	}
	return a;
}

static int
update_install_sizes(struct xbps_handle *xhp, xbps_dictionary_t pkgd)
{
	struct archive *a;
	struct archive_entry *entry;
	char *binfile;
	int rv = 0;

	binfile = xbps_repository_pkg_path(xhp, pkgd);
	if (binfile == NULL)
		return ENOMEM;

	if ((a = open_archive(binfile)) == NULL) {
		free(binfile);
		return EINVAL;
	}
	free(binfile);

	while ((archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
		const char *bfile = archive_entry_pathname(entry);
		int64_t size = archive_entry_size(entry);
		(void) size;

		if (bfile[0] == '.')
			bfile++; /* skip first dot */

		archive_read_data_skip(a);
	}
	archive_read_finish(a);

	return rv;
}

static int
update_remove_sizes(struct xbps_handle *xhp, xbps_dictionary_t pkgd)
{
	struct archive *a;
	struct archive_entry *entry;
	char *binfile;
	int rv = 0;

	binfile = xbps_repository_pkg_path(xhp, pkgd);
	if (binfile == NULL)
		goto fallback;

	if ((a = open_archive(binfile)) == NULL) {
		free(binfile);
		return EINVAL;
	}
	free(binfile);

	while ((archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
		archive_read_data_skip(a);
	}
	archive_read_finish(a);

	return rv;

fallback:
	return 0;
}

int HIDDEN
xbps_transaction_diskspace(struct xbps_handle *xhp, xbps_object_iterator_t iter)
{
	xbps_object_t obj;
	xbps_dictionary_t pkg_metad;
	const char *pkgver, *tract;
	struct mountpoint **mnts;
	size_t nmnts;
	int rv = 0;
	bool preserve;

	nmnts = get_mountpoints(xhp, &mnts);
	if (nmnts == 0)
		return 0;
	qsort(mnts, nmnts, sizeof (struct mountpoint *), mountcmp);

#ifdef FULL_DEBUG
	xbps_dbg_printf(xhp, "%s: found mount points: ", __func__);
	for (unsigned int i = 0; i < nmnts; i++) {
		xbps_dbg_printf_append(xhp, i == 0 ? "`%s'" : ", `%s'",
		    mnts[i]->path);
	}
	xbps_dbg_printf_append(xhp, "\n");
#endif

	iter = xbps_array_iter_from_dict(xhp->transd, "packages");
	if (iter == NULL)
		return EINVAL;

	while ((obj = xbps_object_iterator_next(iter)) != NULL) {
		xbps_dictionary_get_cstring_nocopy(obj, "pkgver", &pkgver);
		xbps_dictionary_get_cstring_nocopy(obj, "transaction", &tract);
		xbps_dictionary_get_bool(obj, "preserve", &preserve);

		if (strcmp(tract, "install") == 0 ||
		    strcmp(tract, "update") == 0) {
			update_install_sizes(xhp, obj);
		}

		if ((strcmp(tract, "remove") == 0) ||
		    ((strcmp(tract, "update") == 0) && !preserve)) {
			char *pkgname = xbps_pkg_name(pkgver);
			assert(pkgname);
			pkg_metad = xbps_pkgdb_get_pkg(xhp, pkgname);
			free(pkgname);
			if (pkg_metad == NULL)
				continue;
			update_remove_sizes(xhp, pkg_metad);
		}

	}
	xbps_object_iterator_reset(iter);

	return ENOSPC;
	return rv;
}
