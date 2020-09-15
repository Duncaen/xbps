/*-
 * Copyright (c) 2015 Juan Romero Pardines.
 * Copyright (c) 2020 Duncan Overbruck <mail@duncano.de>.
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
#include <assert.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/param.h>

#include <xbps.h>
#include "defs.h"

/*
 * Checks package integrity of an installed package.
 * The following task is accomplished in this file:
 *
 * 	o Check alternative symlinks for set alternative groups.
 *
 * returns 0 if test ran successfully, 1 otherwise and -1 on error.
 */

static ssize_t
root_readlink(struct xbps_handle *xhp, const char *path, char *buf, size_t sz)
{
	char tmp[PATH_MAX];
	if (xbps_path_join(tmp, sizeof tmp, xhp->rootdir, path, (char *)NULL) == -1)
		return -1;
	return readlink(tmp, buf, sz);
}

static int
check_symlinks(struct xbps_handle *xhp UNUSED, const char *pkgname, xbps_array_t a,
	const char *grname)
{
	int rv = 0;
	ssize_t l;
	unsigned int i;
	char *alternative, *linkpath, *target;
	char path[PATH_MAX], buf[PATH_MAX], buf2[PATH_MAX];

	for (i = 0; i < xbps_array_count(a); i++) {
		alternative = xbps_string_cstring(xbps_array_get(a, i));

		/* split alternative into linkpath:target */
		linkpath = alternative;
		if ((target = strchr(alternative, ':')) == NULL) {
			xbps_error_printf("%s: invalid alternative %s.\n",
			    pkgname, alternative);
			free(alternative);
			continue;
		}
		*target++ = '\0';

		/* add target directory to relative linkpath */
		if (*linkpath != '/') {
			char *dir;
			char *tmp = strdup(target);
			if (tmp == NULL) {
				xbps_error_printf("%s: strdup: %s\n",
				    pkgname, strerror(errno));
				free(alternative);
				return -1;
			}
			dir = dirname(tmp);
			if (xbps_path_join(buf, sizeof buf, dir, linkpath,
			    (char *)NULL) == -1) {
				xbps_error_printf("%s: xbps_path_join: %s\n",
				    pkgname, strerror(errno));
				free(tmp);
				free(alternative);
				return -1;
			}
			free(tmp);
			linkpath = buf;
		}

		/* make target relative */
		if (*target == '/') {
			if (xbps_path_rel(buf2, sizeof buf2, linkpath, target) == -1) {
				xbps_error_printf("%s: xbps_path_rel: %s\n",
				    pkgname, strerror(errno));
				free(alternative);
				return -1;
			}
			target = buf2;
		}

		if ((l = root_readlink(xhp, linkpath, path, sizeof path)) == -1) {
			xbps_error_printf(
			    "%s: alternatives group %s symlink %s: %s\n",
			    pkgname, grname, linkpath, strerror(errno));
			rv = 1;
		} else if (strncmp(path, target, l) != 0) {
			xbps_error_printf("%s: alternatives group %s symlink %s has wrong target.\n",
			    pkgname, grname, linkpath);
			rv = 1;
		}
		free(alternative);
	}

	return rv;
}

int
check_pkg_alternatives(struct xbps_handle *xhp, const char *pkgname, void *arg)
{
	xbps_array_t allkeys, array;
	xbps_dictionary_t pkg_propsd = arg;
	xbps_dictionary_t alternatives, pkg_alternatives;
	int rv = 0;

	alternatives = xbps_dictionary_get(xhp->pkgdb, "_XBPS_ALTERNATIVES_");
	if (alternatives == NULL)
		return 0;

	pkg_alternatives = xbps_dictionary_get(pkg_propsd, "alternatives");
	if (!xbps_dictionary_count(pkg_alternatives))
		return 0;

	allkeys = xbps_dictionary_all_keys(pkg_alternatives);
	for (unsigned int i = 0; i < xbps_array_count(allkeys); i++) {
		xbps_object_t keysym;
		const char *first= NULL, *group;

		keysym = xbps_array_get(allkeys, i);
		group = xbps_dictionary_keysym_cstring_nocopy(keysym);

		array = xbps_dictionary_get(alternatives, group);
		if (array == NULL)
			continue;

		/* if pkgname is the first entry its set as alternative */
		xbps_array_get_cstring_nocopy(array, 0, &first);
		if (strcmp(pkgname, first) != 0)
			continue;

		array = xbps_dictionary_get(pkg_alternatives, group);
		if (check_symlinks(xhp, pkgname, array, group) != 0)
			rv = 1;
	}
	xbps_object_release(allkeys);

	return rv;
}
