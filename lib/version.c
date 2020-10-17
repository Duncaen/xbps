/*-
 * Copyright (c) 2002 Alistair G. Crooks.
 * Copyright (c) 2020 Duncan Overbruck <mail@duncano.de>
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
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * based on dewey.c:
 * $NetBSD: dewey.c,v 1.1.1.3 2009/03/08 14:51:37 joerg Exp $
 */

#include <ctype.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "xbps_api_impl.h"

#ifndef MIN
#define MIN(a,b)	(((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b)	(((a) > (b)) ? (a) : (b))
#endif

/*
 * This version comparision algorithm is based
 * on and compatible with "dewey" from NetBSD.
 * The main difference is that instead of parsing
 * the full version strings into an array of numbers,
 * this implementation compares each number as parses
 * them, therefore this implementation does not allocate
 * any memory and stops parsing as soon as the version
 * don't match.
 *
 * Versions are "split" into separate components,
 * each component produces a number, with the
 * exception of alpha characters, they produce
 * two numbers, a dot and the offset of the
 * character in the alphabet, "a" is equal to  ".1".
 *
 * keywords return a predefined number:
 * - "alpha" returns Alpha which is -3
 * - "beta" returns Beta which is -2
 * - "pre": returns RC which is -1
 * - "rc": returns RC which is -1
 * - "pl" returns Dot which is 0
 * 
 * The parsing follows:
 *   version   <- component*
 *   revision  <- '_' DIGIT+
 *   component <- DIGIT+ / '.' / revision / keyword / ALPHA
 *   keyword   <- "alpha" / "beta" / "pre" / "pl" / "rc"
 *
 * The previous implementation allowed revisions
 * anywhere and allowed revisions to appear multiple
 * times, this behaviour is preserved for compatibility.
 * A more correct approach would be to only allow
 * the revision once and use it to mark the end.
 *
 * Comparing versions is done by iterating over
 * the components of two version strings as long
 * as the components are equal, if one of the
 * versions has more components, then the component
 * is compared against 0 until both have no
 * components left.
 * If the two components are not equal then
 * we found which version is lower or greater
 * and can return the result without looking
 * at the rest of the version string.
 */

/* do not modify these values, or things will NOT work */
enum {
	Alpha = -3,
	Beta = -2,
	RC = -1,
	Dot = 0,
};

struct xbps_version_parser {
	const char *p;
	const char *e;
	enum {
		SDEF,
		SALPHA,
	} state;

	int rev;
};

static int
number(struct xbps_version_parser *p)
{
	int n;
	/* XXX: should this care about overflows? */
	n = (*p->p - '0');
	for (p->p++; (p->e ? p->p < p->e : 1) && isdigit((unsigned char)*p->p); p->p++)
		n = 10 * n + (*p->p - '0');
	return n;
}

static int
xbps_version_component(struct xbps_version_parser *p)
{
	if (p->p == p->e)
		return 0;

	if (p->state == SALPHA) {
		p->state = SDEF;
		return (int)(tolower(*p->p++) - 'a') + 1;
	}

	switch (*p->p) {
	case '\0':
		p->e = p->p;
		return 0;
	case '_':
		if (++p->p == p->e)
			return 0;
		if (isdigit((unsigned char)*p->p))
			p->rev = number(p);
		/* 
		 * XXX: the previous implementation allowed
		 * _$number multiple times and used the last
		 * one as revision. Do the same for now,
		 * the sane solution would be to stop
		 * parsing after the revision by setting
		 * the and to the current position:
		 * p->e = p->p;
		 */
		return 0;
	case '.':
		p->p++;
		return Dot;
	}

	if (isdigit((unsigned char)*p->p))
		return number(p);

	if (isalpha((unsigned char)*p->p)) {
		/*
		 * if end is known we can maybe skip some comparsions,
		 * otherwise just try all
		 */
		switch (p->e ? p->e - p->p : 5) {
		default:
			/* fallthrough */
		case 5:
			if (strncasecmp("alpha", p->p, 5) == 0) {
				p->p += 5;
				return Alpha;
			}
			/* fallthrough */
		case 4:
			if (strncasecmp("beta", p->p, 4) == 0) {
				p->p += 4;
				return Beta;
			}
			/* fallthrough */
		case 3:
			if (strncasecmp("pre", p->p, 3) == 0) {
				p->p += 3;
				return RC;
			}
			/* fallthrough */
		case 2:
			if (strncasecmp("rc", p->p, 2) == 0) {
				p->p += 2;
				return RC;
			}
			if (strncasecmp("pl", p->p, 2) == 0) {
				p->p += 2;
				return Dot;
			}
			/* fallthrough */
		case 1:
			p->state = SALPHA;
			return Dot;
		}
	}
	/* ignore everything else */
	p->p++;
	return 0;
}

int
xbps_version_cmpn(const char *s1, const char *e1, const char *s2, const char *e2)
{
	struct xbps_version_parser a = { .p = s1, .e = e1, .state = SDEF, .rev = 0 };
	struct xbps_version_parser b = { .p = s2, .e = e2, .state = SDEF, .rev = 0 };
	int cmp;

	/*
	 * Compare both components until either one reached the end.
	 */
	for (; a.p != a.e && b.p != b.e;) {
		if ((cmp = xbps_version_component(&a) - xbps_version_component(&b)) != 0)
			return cmp > 0 ? 1 : -1;
	}
	/*
	 * If one of them reached the end, compare against 0.
	 */
	if (a.p != a.e) {
		for (; a.p != a.e;) {
			if ((cmp = xbps_version_component(&a)) != 0)
				return cmp > 0 ? 1 : -1;
		}
	} else if (b.p != b.e) {
		for (; b.p != b.e;) {
			if ((cmp = xbps_version_component(&b)) != 0)
				return cmp < 0 ? 1 : -1;
		}
	}
	cmp = a.rev - b.rev;
	return cmp == 0 ? 0 : cmp > 0 ? 1 : -1;
}

int
xbps_version_cmp(const char *s1, const char *s2)
{
	return xbps_version_cmpn(s1, NULL, s2, NULL);
}

static bool
xbps_pkgpattern_parse(const char *pattern, size_t *namelen,
		const char **min, size_t *minlen, bool *mininc,
		const char **max, size_t *maxlen, bool *maxinc)
{
	const char *p;

	*min = *max = NULL;


	if ((p = strpbrk(pattern, "<>")) == NULL &&
	    (p = strrchr(pattern, '-')) == NULL) {
		return false;
	}

	*namelen = p-pattern;
	pattern = p;
	if (*pattern == '-')
		pattern++;

	*min = strchr(pattern, '>');
	if (*min) {
		*mininc = (*min)[1] == '=';
		*min += *mininc ? 2 : 1;
		if ((*max = strchr(*min, '<')))
			*minlen = *max-*min;
		else
			*minlen = strlen(*min);
	}

	if (*max || (*max = strchr(pattern, '<'))) {
		*maxinc = (*max)[1] == '=';
		*max += *maxinc ? 2 : 1;
		*maxlen = strlen(*max);
	}

	if (*min == NULL && *max == NULL) {
		*min = *max = pattern;
		*minlen = *maxlen = strlen(pattern);
		*mininc = *maxinc = true;
		return true;
	} else if (*min == NULL) {
		*minlen = 0;
		*mininc = false;
	} else if (*max == NULL) {
		*maxlen = 0;
		*maxinc = false;
	}

	return true;
}

/*
 * Returns -1, 0 or 1 depending on if the version components of
 * pkg1 is less than, equal to or greater than pkg2. No comparison
 * comparison of the basenames is done.
 */
int
xbps_cmpver(const char *pkg1, const char *pkg2)
{
	int cmp;
	const char *av = strrchr(pkg1, '-'), *bv = strrchr(pkg2, '-');
	/* fprintf(stderr, "xbps_cmpver: a=%s b=%s\n", pkg1, pkg2); */
	/* fprintf(stderr, "xbps_cmpver: av=%s bv=%s\n", av, bv); */

	if (av == NULL && bv == NULL) {
		/* just plain versions */
		av = pkg1;
		bv = pkg2;
	} else if (av != NULL && bv != NULL) {
		/* two pkgvers */
		size_t al = av-pkg1, bl = bv-pkg2;
		assert(al == bl);
		assert(strncmp(pkg1, pkg2, al) == 0);
		av++;
		bv++;
	} else {
		assert(!"pkgver and just version");
	}

	if ((cmp = xbps_version_cmp(av, bv)) == 0)
		return 0;
	if (cmp < 0)
		return -1;
	return 1;
}

/*
 * Match pkg against pattern, return 1 if matching, 0 otherwise or -1 on error.
 */
int
xbps_pkgpattern_match(const char *pkg, const char *pattern)
{
	int cmp;
	const char *version, *min, *max;
	size_t namelen, minlen = 0, maxlen = 0;
	bool mininc = false, maxinc = false;

	/* fprintf(stderr, "%s: pkg=%s pattern=%s\n", __func__, pkg, pattern); */

	assert(pkg);
	assert(pattern);

	/* simple match on "pkg" against "pattern" */
	if (strcmp(pattern, pkg) == 0)
		return 1;

	/* glob match */
	if (strpbrk(pattern, "*?[]") != NULL) {
		/* fprintf(stderr, "fnmatch\n"); */
		/* XXX: users don't check for error cases correctly */
		return fnmatch(pattern, pkg, FNM_PERIOD) == 0;
	}

	if (!xbps_pkgpattern_parse(pattern, &namelen,
	    &min, &minlen, &mininc,
	    &max, &maxlen, &maxinc)) {
		return 0;
	}
	/* fprintf(stderr, "%s: min=%.*s max=%.*s\n", __func__, (int)minlen, min, (int)maxlen, max); */

	/* compare name */
	if ((version = strrchr(pkg, '-')) == NULL)
		return 0;
	version++;
	if ((namelen != (size_t)(version-pkg)-1) ||
	    strncmp(pkg, pattern, namelen) != 0)
		return 0;

	if (min == max)
		return xbps_version_cmpn(version, NULL, min, min+minlen) == 0;

	if (min != NULL) {
		cmp = xbps_version_cmpn(version, NULL, min, min+minlen);
		if (cmp < 0 || (cmp == 0 && !mininc))
			return 0;
	}
	if (max != NULL) {
		cmp = xbps_version_cmpn(version, NULL, max, max+maxlen);
		if (cmp > 0 || (cmp == 0 && !maxinc))
			return 0;
	}
	return 1;
}
