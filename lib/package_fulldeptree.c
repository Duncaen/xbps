/*-
 * Copyright (c) 2014-2019 Juan Romero Pardines.
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

#include "xbps_api_impl.h"

#define FLAG_DONE  0x0001
#define FLAG_CYCLE 0x0010

struct item;

struct item {
	struct item *hnext;
	struct item **deps;
	size_t ndeps;
	const char *pkgver;
	size_t namelen;
	xbps_array_t rdeps;
	int flags;
};

#define ITHSIZE	1024
#define ITHMASK	(ITHSIZE - 1)

static struct item *ItemHash[ITHSIZE];

static int
itemhash(const char *key, size_t len)
{
	int hv = 0xA1B5F342;
	unsigned int i;

	assert(key);
	assert(len > 0);

	for (i = 0; i < len; ++i)
		hv = (hv << 5) ^ (hv >> 23) ^ key[i];

	return hv & ITHMASK;
}

static struct item *
lookupItem(const char *key, size_t len)
{
	struct item *item;

	assert(key);
	assert(len > 0);

	for (item = ItemHash[itemhash(key, len)]; item; item = item->hnext) {
		if (strncmp(key, item->pkgver, len) == 0)
			return item;
	}
	return NULL;
}

static struct item *
addItem(const char *pkgver, size_t namelen)
{
	struct item **itemp;
	struct item *item = calloc(sizeof(*item), 1);

	if (item == NULL)
		return NULL;

	assert(pkgver);
	assert(namelen > 0);

	itemp = &ItemHash[itemhash(pkgver, namelen)];
	item->hnext = *itemp;
	item->pkgver = pkgver;
	item->namelen = namelen;
	*itemp = item;

	return item;
}

/*
 * Recursively add all dependencies to the hash table.
 */
static struct item *
collect_depends(struct xbps_handle *xhp, xbps_dictionary_t pkgd, bool rpool)
{
	char pkgn[XBPS_MAXPKGNAME];
	xbps_array_t rdeps, provides;
	struct item *item, *xitem;
	const char *pkgver = NULL;
	size_t pkgnlen;

	assert(xhp);
	assert(pkgd);

	xbps_dictionary_get_cstring_nocopy(pkgd, "pkgver", &pkgver);

	pkgnlen = xbps_pkgname_len(pkgver);
	
	if ((item = lookupItem(pkgver, pkgnlen)) ||
	    !(item = addItem(pkgver, pkgnlen))) {
		return item;
	}

	item->flags |= FLAG_CYCLE;

	rdeps = xbps_dictionary_get(pkgd, "run_depends");
	item->ndeps = xbps_array_count(rdeps);
	if (item->ndeps) {
		item->deps = calloc(item->ndeps, sizeof (struct item *));
		if (item->deps == NULL)
			return NULL;
	} else {
		item->deps = NULL;
	}

	for (unsigned int i = 0; i < item->ndeps; i++) {
		xbps_dictionary_t curpkgd;
		const char *curdep = NULL;

		xbps_array_get_cstring_nocopy(rdeps, i, &curdep);
		if (rpool) {
			if ((curpkgd = xbps_rpool_get_pkg(xhp, curdep)) == NULL)
				curpkgd = xbps_rpool_get_virtualpkg(xhp, curdep);
		} else {
			if ((curpkgd = xbps_pkgdb_get_pkg(xhp, curdep)) == NULL)
				curpkgd = xbps_pkgdb_get_virtualpkg(xhp, curdep);
			/* Ignore missing local runtime dependencies, because ignorepkg */
			if (curpkgd == NULL)
				continue;
		}
		if (curpkgd == NULL) {
			/* package depends on missing dependencies */
			xbps_dbg_printf(xhp, "%s: missing dependency '%s'\n", pkgver, curdep);
			errno = ENODEV;
			return NULL;
		}

		pkgnlen = xbps_pkgname_cpy(pkgn, curdep, sizeof pkgn);
		if (pkgnlen >= sizeof pkgn) {
			errno = ENOMEM;
			return NULL;
		}
		
		if ((provides = xbps_dictionary_get(pkgd, "provides")) &&
		    xbps_match_pkgname_in_array(provides, pkgn)) {
			xbps_dbg_printf(xhp, "%s: ignoring dependency %s "
			    "already in provides\n", pkgver, curdep);
			continue;
		}
		xitem = lookupItem(curdep, pkgnlen);
		if (xitem) {
			item->deps[i] = xitem;
			continue;
		}
		xitem = collect_depends(xhp, curpkgd, rpool);
		if (xitem == NULL) {
			/* package depends on missing dependencies */
			xbps_dbg_printf(xhp, "%s: missing dependency '%s'\n", pkgver, curdep);
			errno = ENODEV;
			return NULL;
		}
		item->deps[i] = xitem;
	}
	item->flags |= ~FLAG_CYCLE;
	return item;
}

static bool
sort_recursive(struct xbps_handle *xhp UNUSED, struct item *item, xbps_array_t result)
{
	size_t done = 0;
	bool cycle = item->flags & FLAG_CYCLE;

	fprintf(stderr, ">>>> sort recursive: %s ndeps=%zu\n", item->pkgver,
	    item->ndeps);
	if (cycle) {
		fprintf(stderr, ">>>> breaking cycle: %s\n", item->pkgver);
		/* xbps_array_add_cstring_nocopy(result, item->pkgver); */
		/* item->flags |= FLAG_DONE; */
		/* return true; */
		return false;
	}

	item->flags |= FLAG_CYCLE;
	for (size_t i = 0; i < item->ndeps; i++) {
		struct item *xitem = item->deps[i];
		if (xitem == NULL || xitem->flags & FLAG_DONE ||
		    sort_recursive(xhp, xitem, result))
			done++;
	}
	item->flags |= ~FLAG_CYCLE;

	fprintf(stderr, ">>>> %s: done=%zu ndeps=%zu\n", item->pkgver, done, 
	    item->ndeps);

	if (done == item->ndeps) {
		fprintf(stderr, ">>>> done: %s\n", item->pkgver);
		xbps_array_add_cstring_nocopy(result, item->pkgver);
		item->flags |= FLAG_DONE;
		return true;
	}
	/* if (cycle && done+1 == rdeps) { */
	/* 	fprintf(stderr, ">>>> breaking cycle\n"); */
	/* 	xbps_array_add_cstring_nocopy(result, item->pkgver); */
	/* 	item->flags |= FLAG_DONE; */
	/* 	return true; */
	/* } */
	return false;
}

/*
 * Recursively add dependencies to the todo list,
 * and add dependencies without further dependencies
 * to the result.
 */
static bool
collect_todo(struct xbps_handle *xhp UNUSED, struct item *item, xbps_array_t todo, xbps_array_t result)
{
	if (item->flags & FLAG_CYCLE)
		return true;

	item->flags &= FLAG_CYCLE;
	for (size_t i = 0; i < item->ndeps; i++) {
		struct item *xitem = item->deps[i];
		if (xitem == NULL)
			continue;
		if (xbps_match_string_in_array(result, xitem->pkgver) ||
		    xbps_match_string_in_array(todo, xitem->pkgver)) {
			fprintf(stderr, ">>> skipping: %s\n", xitem->pkgver);
			continue;
		}
		if (xitem->ndeps == 0) {
			if (!xbps_array_add_cstring_nocopy(result, xitem->pkgver))
				return false;
			fprintf(stderr, ">>> added result: %s\n", xitem->pkgver);
			xitem->flags = FLAG_DONE;
		} else {
			if (!collect_todo(xhp, xitem, todo, result))
				return false;
			if (!xbps_array_add_cstring_nocopy(todo, xitem->pkgver))
				return false;
			fprintf(stderr, ">>> added todo: %s\n", xitem->pkgver);
			xitem->flags = 0;
		}
	}
	item->flags |= FLAG_CYCLE;
	return true;
}

static xbps_array_t
ordered_depends(struct xbps_handle *xhp UNUSED, struct item *item)
{
	struct item *xitem;
	xbps_array_t result, todo;

	assert(item);
	/* head is already done */
	item->flags = FLAG_DONE;

	result = xbps_array_create();
	assert(result);
	todo = xbps_array_create();
	assert(todo);

	if (!collect_todo(xhp, item, todo, result))
		return NULL;

	while (xbps_array_count(todo) > 0) {
		for (unsigned int i = 0; i < xbps_array_count(todo); i++) {
			const char *pkgn;

			xbps_array_get_cstring_nocopy(todo, i, &pkgn);
			xitem = lookupItem(pkgn, strlen(pkgn));
			assert(xitem);
			fprintf(stderr, ">>> TODO: %s\n", pkgn);

			if (xitem->flags & FLAG_DONE || sort_recursive(xhp, xitem, result)) {
				xbps_array_remove(todo, i);
				break;
			}
		}
	}
	
	for (unsigned int i = 0; i < xbps_array_count(result); i++) {
		const char *pkgver;
		xbps_array_get_cstring_nocopy(result, i, &pkgver);
		fprintf(stderr, ">>>>> %s\n", pkgver);
	}
	return result;
}

xbps_array_t HIDDEN
xbps_get_pkg_fulldeptree(struct xbps_handle *xhp, const char *pkg, bool rpool)
{
	xbps_dictionary_t pkgd;
	struct item *item;

	if (rpool) {
		if (((pkgd = xbps_rpool_get_pkg(xhp, pkg)) == NULL) &&
		    ((pkgd = xbps_rpool_get_virtualpkg(xhp, pkg)) == NULL))
			return NULL;
	} else {
		if (((pkgd = xbps_pkgdb_get_pkg(xhp, pkg)) == NULL) &&
		    ((pkgd = xbps_pkgdb_get_virtualpkg(xhp, pkg)) == NULL))
			return NULL;
	}
	if ((item = collect_depends(xhp, pkgd, rpool)) == NULL)
		return NULL;

	return ordered_depends(xhp, item);
}
