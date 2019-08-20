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
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "xbps_api_impl.h"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-function"
#else
#pragma GCC diagnostic ignored "-Wunused"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

struct pkgq {
	struct pkgq *next, *prev;
	struct pkg *pkg;
};

struct item {
	char *pkgname;
	xbps_array_t rdeps;
	int flags;
};

struct pkg {
	struct pkg *hnext;
	char *name;
	bool root;
	struct incompat **incompats;
	size_t nincompats;
	const char **versions;
	size_t nversions;
	/* package dictionary for each version */
	xbps_dictionary_t *pkgds; 
	/* if it was already listed for each version */
	bool *listed;
	xbps_constraint_t *invalids;
	const char *hold;
	bool downgrade;
};

#define ITHSIZE	1024
#define ITHMASK	(ITHSIZE - 1)

static struct pkg *PkgHash[ITHSIZE];

struct decision {
	struct decision *hnext;
	const char *name;
	const char *version;
};

struct term {
	bool positive;
	struct pkg *pkg;
	xbps_constraint_t *constraint;
};

struct termq {
	struct termq *next, *prev;
	struct term *term;
};

enum {
	CauseRoot = 0,
	CauseDependency = 1,
	CauseNoVersion = 2,
	CauseNotFound = 3,
	CauseConflict = 4,
};

struct cause {
	int type;
};

struct incompat {
	struct termq *terms;
	struct {
		int type;
		struct incompat *this, *other;
	} cause;
};

struct assign {
	size_t level;
	size_t index;
	struct incompat *cause;
	struct term term;
};

struct assignq {
	struct assignq *next, *prev;
	struct assign *assign;
};

struct transaction {
	struct pkg root;
	const char *rootv;

	size_t level;
	struct termq *negative, *positive;
	struct assignq *assignments;
	size_t attempts;
	bool backtracking;
	struct decision *decisions[ITHSIZE];
};

static struct transaction *trans = NULL;

static int
strhash(const char *key, size_t len)
{
	int hv = 0xA1B5F342;
	unsigned int i;

	assert(key);
	assert(len > 0);

	for (i = 0; i < len; ++i)
		hv = (hv << 5) ^ (hv >> 23) ^ key[i];

	return hv & ITHMASK;
}

static struct pkg *
lookupPkg(const char *key, size_t len)
{
	struct pkg *item;

	assert(key);
	assert(len > 0);

	for (item = PkgHash[strhash(key, len)]; item; item = item->hnext) {
		if (strncmp(key, item->name, len) == 0)
			return item;
	}
	return NULL;
}

static int
addPkg(struct pkg **outp, const char *key, size_t keylen)
{
	struct pkg **itemp;
	struct pkg *item = calloc(sizeof(*item), 1);

	if (item == NULL)
		return -1;

	assert(key);
	assert(keylen > 0);

	itemp = &PkgHash[strhash(key, keylen)];
	item->hnext = *itemp;
	item->versions = NULL;
	item->nversions = 0;
	item->pkgds = NULL;
	item->name = strndup(key, keylen);
	if (item->name == NULL) {
		free(item);
		return -1;
	}
	*itemp = item;

	*outp = item;
	return 0;
}

static struct decision *
lookupDecision(struct decision **decisions, const char *pkgname)
{
	struct decision *i;

	assert(pkgname);

	for (i = decisions[strhash(pkgname, strlen(pkgname))]; i; i = i->hnext) {
		if (strcmp(pkgname, i->name) == 0)
			return i;
	}
	return NULL;
}

static struct decision *
addDecision(struct decision **decisions, const char *pkgname)
{
	struct decision **itemp;
	struct decision *item = calloc(1, sizeof (struct decision));

	if (item == NULL)
		return NULL;

	assert(pkgname);

	itemp = &decisions[strhash(pkgname, strlen(pkgname))];
	item->hnext = *itemp;
	item->name = pkgname;
	*itemp = item;

	return item;
}

static int
pkgq_add(struct pkgq **head, struct pkg *pkg)
{
	struct pkgq *new, *tail = NULL;

	assert(head);

	new = calloc(1, sizeof (struct pkgq));
	if (new == NULL)
		return -1;
	new->pkg = pkg;

	if (*head == NULL) {
		*head = new;
		return 0;
	}

	for (tail = *head; tail && tail->next; tail = tail->next)
		;

	tail->next = new;
	new->prev = tail;

	return 0;
}

static int
pkgq_pop(struct pkgq **head, struct pkg **outp)
{
	assert(*head);
	*outp = (*head)->pkg;
	*head = (*head)->next;
	return 0;
}

static int
termq_add(struct termq **head, struct term *term)
{
	struct termq *new, *tail;

	assert(head);

	new = calloc(1, sizeof (struct termq));
	if (new == NULL)
		return -1;
	new->term = term;

	if (*head == NULL) {
		*head = new;
		return 0;
	}

	for (tail = *head; tail && tail->next; tail = tail->next)
		;

	tail->next = new;
	new->prev = tail;

	return 0;
}

static int
assignq_add(struct assignq **head, struct assign *a)
{
	struct assignq *new, *tail;

	fprintf(stderr, "assignq_add: 1\n");
	assert(head);

	new = calloc(1, sizeof (struct assignq));
	if (new == NULL)
		return -1;
	new->assign = a;
	fprintf(stderr, "assignq_add: 2\n");

	if (*head == NULL) {
		*head = new;
		return 0;
	}
	fprintf(stderr, "assignq_add: 3\n");

	for (tail = *head; tail && tail->next; tail = tail->next)
		;

	tail->next = new;
	new->prev = tail;

	fprintf(stderr, "assignq_add: 4\n");
	return 0;
}

static int
pkg_add_incompat(struct pkg *pkg, struct incompat *incompat)
{
	size_t i = pkg->nincompats;
	pkg->incompats = reallocarray(pkg->incompats, i+1, sizeof (struct incompat *));
	if (pkg->incompats == NULL)
		return -1;
	pkg->nincompats++;
	pkg->incompats[i] = incompat;
	return 0;
}

static void
fprintpkg(FILE *fp, struct pkg *pkg)
{
	fprintf(fp, "%s", pkg->name);
}

static void
fprintconstraint(FILE *fp, xbps_constraint_t *constraint)
{
	(void)fp;
	(void)constraint;
#if 0
	size_t n = xbps_constraint_str(NULL, 0, constraint)+1;
	char *tmp = malloc(n);
	if (tmp) {
		xbps_constraint_str(tmp, n, constraint);
		fputs(tmp, fp);
	} else {
		fputs(strerror(errno), fp);
	}
	free(tmp);
#endif
}

static void
fprintterm(FILE *fp, struct term *term)
{
	/* fprintf(stderr, "term<%p>(", term); */
	if (!term->positive)
		fputs("not ", fp);
	fprintpkg(fp, term->pkg);
	fputc(' ', stderr);
	fprintconstraint(fp, term->constraint);
	/* fputc(')', stderr); */
}

static void
fprintassign(FILE *fp, struct assign *a)
{
	fprintterm(fp, &a->term);
}

#if 0
static void
fprintcause(FILE *fp, struct cause *ca)
{
	fprintterm(fp, &a->term);
}
#endif

static bool
isfailure(struct incompat *incompat)
{
	size_t len = 0;
	if (incompat->terms == NULL)
		return true;
	for (struct termq *i = incompat->terms; i; i = i->next)
		len++;
	if (len == 1 && incompat->terms->term->pkg->root)
		return true;
	return false;
}

static void
fprintincompat(FILE *fp, struct incompat *in)
{
	size_t count = 0;
	/* fprintf(fp, "incompatibility("); */
	if (in->cause.type) {
		switch (in->cause.type) {
		case CauseRoot:
			fprintf(stderr, "%s is ", in->terms->term->pkg->name);
			/* fprintpkgvers(stderr, in->terms->term->pkgvers); */
			return;
		case CauseDependency:
			assert(in->terms->term);
			assert(in->terms->next);
			assert(in->terms->next->next == NULL);
			{
				struct term *depender = in->terms->term;
				struct term *dependee = in->terms->next->term;
				assert(depender->positive);
				assert(!dependee->positive);
				fprintf(stderr, "%s depends on %s", depender->pkg->name, dependee->pkg->name);
			}
			return;
		case CauseNoVersion:
			fprintf(stderr, "no versions of a match b");
			break;
		case CauseNotFound:
			assert(in->terms->term);
			assert(in->terms->next == NULL);
			fprintf(stderr, "%s doesn't exist 'here could be a message'",
			    in->terms->term->pkg->name);
			return;
		default:
			;
		}
		/* fprintf(stderr, ")"); */
		/* return; */
	}
	if (isfailure(in)) {
		fputs("version solving failed", stderr);
		return;
	}

	for (struct termq *i = in->terms; i; i = i->next)
		count++;

	if (count == 1) {
		struct term *term = in->terms->term;
		/* if (xbps_constraint_is_any(term->constraint)) { */
		/* } */
		fprintterm(stderr, term);
		fputs(" is ", stderr);
		fputs(term->positive ? "forbidden" : "required", stderr);
		return;
	}

	if (count == 2) {
		struct term *a, *b;
		a = in->terms->term;
		b = in->terms->next->term;
		if (a->positive == b->positive) {
			if (a->positive) {
				fprintterm(fp, a);
				fprintf(stderr, " is incompatible with ");
				fprintterm(fp, b);
			} else {
				fprintf(stderr, "either ");
				fprintterm(fp, a);
				fprintf(stderr, " or ");
				fprintterm(fp, b);
			}
		}
	}
	{
		struct termq *pos = NULL;
		struct termq *neg = NULL;
		for (struct termq *i = in->terms; i; i = i->next)
			if (termq_add(i->term->positive ? &pos : &neg, i->term) == -1) {
				abort();
				return;
			}
		if (pos && neg) {
			if (pos->next == NULL) {
				fprintterm(fp, pos->term);
				fputs(" requires ", stderr);
				for (struct termq *i = neg; i; i = i->next) {
					fprintterm(fp, i->term);
					if (i->next)
						fputs(" or ", stderr);
				}
				return;
			}
			fputs("if ", stderr);
			for (struct termq *i = pos; i; i = i->next) {
				fprintterm(fp, i->term);
				if (i->next)
					fputs(" and ", stderr);
			}
			fputs(" then ", stderr);
			for (struct termq *i = neg; i; i = i->next) {
				fprintterm(fp, i->term);
				if (i->next)
					fputs(" or ", stderr);
			}
		} else {
			fputs("one of ", stderr);
			for (struct termq *i = pos ? pos : neg; i; i = i->next) {
				fprintterm(fp, i->term);
				if (i->next)
					fputs(" or ", stderr);
			}
			fputs(" must be ", stderr);
			fputs(pos ? "false" : "true", stderr);
		}
	}

	/* fprintf(fp, "%p, count=%zu)", in, count); */
}


int
xbps_transaction_init1(struct xbps_handle *xhp)
{
	if (trans)
		return 0;

	if (xbps_pkgdb_init(xhp) != 0)
		return -1;

	trans = calloc(1, sizeof (struct transaction));
	if (trans == NULL)
		return errno;

	trans->rootv = "0";
	if (trans->rootv == NULL) {
		free(trans);
		return -1;
	}
	trans->root.root = true;
	trans->root.name = strdup("root");
	if (trans->root.name == NULL) {
		free(trans);
		return -1;
	}
	trans->root.versions = malloc(sizeof (const char *));
	if (trans->root.versions == NULL) {
		free(trans->root.name);
		free(trans);
		return -1;
	}
	trans->root.versions[0] = trans->rootv;
	return 0;
}

int
xbps_transaction_add(struct xbps_handle *xhp, const char *pkgname)
{
	(void) xhp;
	(void) pkgname;
	return 0;
}

int
xbps_transaction_remove(struct xbps_handle *xhp, const char *pkgname,
		bool recursive)
{
	xbps_dictionary_t pkgd;

	assert(pkgname != NULL);

	if ((pkgd = xbps_pkgdb_get_pkg(xhp, pkgname)) == NULL) {
		/* pkg not installed */
		return -1;
	}
	if (!recursive)
		return 0;
	return 0;
}

int
xbps_transaction_autoremove(struct xbps_handle *xhp)
{
	(void)xhp;
	return 0;
}

enum {
	DISJOINT = 1,
	OVERLAPPING = 2,
	SUBSET = 3,
};

#if 1
static const char *
strrelation(int rel)
{
	switch (rel) {
	case DISJOINT:
		return "disjoint";
	case OVERLAPPING:
		return "overlapping";
	case SUBSET:
		return "subset";
	}
	return "unknown";
}
#endif

#if 0
static void
dumpterm(struct term *a)
{
	fprintf(stderr, "term pkgname=%s%s:\n", a->pkg->name, a->positive ? "" : " not");
	for (struct pkgverq *i = a->pkgvers; i; i = i->next)
		fprintf(stderr, "\tterm pkgver=%s\n", i->pkgver->pkgver);
}
#endif

static bool
compatible(struct term *a, struct term *b)
{
	return a->pkg->root || b->pkg->root || a->pkg == b->pkg;
}

static int
termrelation(struct term *a, struct term *b)
{
	xbps_constraint_t *this = a->constraint, *other = b->constraint;
#if 0
	/* assert(strcmp(a->pkg->name, b->pkg->name) == 0); */
	assert(a->pkg == b->pkg);
	fprintf(stderr, "termrelation:\n");
	fprintterm(stderr, a);
	fputc('\n', stderr);
	fprintterm(stderr, b);
	fputc('\n', stderr);
	/* dumpterm(a); */
	/* dumpterm(b); */

	if (b->positive) {
		if (a->positive) {
			/* foo from hosted is disjoint with foo from git */
			if (!compatible(a, b)) return DISJOINT;
			/* foo ^1.5.0 is a subset of foo ^1.0.0 */
			if (xbps_constraint_allows_all(other, this)) return SUBSET;
			/* foo ^2.0.0 is disjoint with foo ^1.0.0 */
			if (!xbps_constraint_allows_any(this, other)) return DISJOINT;
			/* foo >=1.5.0 <3.0.0 overlaps foo ^1.0.0 */
			return OVERLAPPING;
		}
		/* not foo from hosted is a superset foo from git */
		if (!compatible(a, b)) return OVERLAPPING;
		/* not foo ^1.0.0 is disjoint with foo ^1.5.0 */
		if (xbps_constraint_allows_all(this, other)) return DISJOINT;
		/* not foo ^1.5.0 overlaps foo ^1.0.0 */
		/* not foo ^2.0.0 is a superset of foo ^1.5.0 */
		return OVERLAPPING;
	}
	if (a->positive) {
		/* foo from hosted is a subset of not foo from git */
		if (!compatible(a, b)) return SUBSET;
		/* foo ^2.0.0 is a subset of not foo ^1.0.0 */
		if (!xbps_constraint_allows_any(other, this)) return SUBSET;
		/* foo ^1.5.0 is disjoint with not foo ^1.0.0 */
		if (xbps_constraint_allows_all(other, this)) return DISJOINT;
		/* foo ^1.0.0 overlaps not foo ^1.5.0 */
		return OVERLAPPING;
	}
	/* not foo from hosted overlaps not foo from git */
	if (!compatible(a, b)) return OVERLAPPING;
	/* not foo ^1.0.0 is a subset of not foo ^1.5.0 */
	if (xbps_constraint_allows_all(this, other)) return SUBSET;
	/* not foo ^2.0.0 overlaps not foo ^1.0.0 */
	/* not foo ^1.5.0 is a superset of not foo ^1.0.0 */
#endif
	return OVERLAPPING;
}

static int
relation(struct term *term)
{
	fprintf(stderr, "relation: positive: %s\n", term->pkg->name);
	for (struct termq *i = trans->positive; i; i = i->next) {
		if (strcmp(i->term->pkg->name, term->pkg->name) == 0)
			return termrelation(i->term, term);
	}
	fprintf(stderr, "relation: negative: %s\n", term->pkg->name);
	for (struct termq *i = trans->negative; i; i = i->next) {
		if (strcmp(i->term->pkg->name, term->pkg->name) == 0)
			return termrelation(i->term, term);
	}
	return OVERLAPPING;
}

static int
satisfies(struct term *term)
{
	int r = relation(term);
	fprintf(stderr, "satisfies: %s == %d (%s)\n", term->pkg->name, r == SUBSET, strrelation(r));
	return r == SUBSET;
}

static int
termintersect(struct term **outp, struct term *a, struct term *b)
{
	assert(strcmp(a->pkg->name, b->pkg->name) == 0);
	fprintf(stderr, "termintersect: a=%p b=%p\n", a, b);

	fprintf(stderr, "termintersect: 1\n");
#if 0
	if (compatible(a, b)) {
		struct term *new = calloc(1, sizeof (struct term));
		if (new == NULL)
			return -1;
		fprintf(stderr, "termintersect: 2\n");
		new->pkg = a->pkg;
		if (a->positive != b->positive) {
			/* foo ^1.0.0 ∩ not foo ^1.5.0 → foo >=1.0.0 <1.5.0 */
			xbps_constraint_t *pos = a->positive ?
				a->constraint : b->constraint;
			xbps_constraint_t *neg = a->positive ?
				b->constraint : a->constraint;
			new->positive = true;
			fprintf(stderr, "termintersect: 2.1\n");
			new->constraint = xbps_constraint_diff(pos, neg);
		} else if (a->positive) {
			/* foo ^1.0.0 ∩ foo >=1.5.0 <3.0.0 → foo ^1.5.0 */
			fprintf(stderr, "termintersect: 2.2\n");
			new->positive = true;
			new->constraint = xbps_constraint_intersect(a->constraint,
			    b->constraint);
		} else {
			/* not foo ^1.0.0 ∩ not foo >=1.5.0 <3.0.0 → not foo >=1.0.0 <3.0.0 */
			fprintf(stderr, "termintersect: 2.3\n");
			new->positive = false;
			new->constraint = xbps_constraint_intersect(a->constraint,
			    b->constraint);
		}
		*outp = new;
		return 0;
	} else if (a->positive != b->positive) {
		/* foo from git ∩ not foo from hosted → foo from git */
		*outp = a->positive ? a : b;
		fprintf(stderr, "termintersect: 3\n");
		return 0;
	}
	fprintf(stderr, "termintersect: 4\n");
	*outp = NULL;
#endif
	return 0;
}


static int
termdiff(struct term **outp, struct term *a, struct term *b)
{
	struct term inv = *b;
	inv.positive = !inv.positive;
	return termintersect(outp, a, &inv);
}


#if 0
static int
termunion(struct term **outp, struct term *a, struct term *b)
{
	struct term *new = calloc(1, sizeof (struct term));
	if (new == NULL)
		return -1;
	new->pkgname = a->pkgname;
	for (struct pkgq *i = a->pkgs; i; i = i->next) {
		if (pkgq_add(&new->pkgs, i->pkg) == -1)
			return -1;
	}
	for (struct pkgq *i = b->pkgs; i; i = i->next) {
		if (pkgq_add(&new->pkgs, i->pkg) == -1)
			return -1;
	}
	*outp = new;
	return 0;
}
#endif

static int
_register(struct assign *a)
{
	struct term *term = NULL;
	struct termq *old = NULL;

	/* fprintf(stderr, "register:"); */
	/* fprintterm(stderr, &a->term); */
	/* fputc('\n', stderr); */
	fprintf(stderr, "register: 1\n");

	for (struct termq *i = trans->positive; i; i = i->next) {
		if (strcmp(i->term->pkg->name, a->term.pkg->name) == 0) {
			struct term *new;
	fprintf(stderr, "register: 1.1\n");
			if (termintersect(&new, i->term, &a->term) == -1)
				return -1;
	fprintf(stderr, "register: 1.2\n");
			i->term = new;
			fprintf(stderr, "register: replace positive: term=%p\n", new);
			/* fprintf(stderr, ">> old:"); */
			/* dumpterm(i->term); */
			/* fprintf(stderr, ">> new:"); */
			/* dumpterm(new); */
			return 0;
		}
	}

	fprintf(stderr, "register: 2\n");
	term = &a->term;
	for (struct termq *i = trans->negative; i; i = i->next) {
		if (strcmp(i->term->pkg->name, a->term.pkg->name) == 0) {
			old = i;
			break;
		}
	}
	if (old != NULL) {
		if (termintersect(&term, &a->term, old->term) == -1)
			return -1;
	}
	if (term->positive) {
		/* XXX: move to termq_rm()? */
		if (old) {
			/*
			 * Remove the old negative [term].
			 */
			if (old->prev)
				old->prev->next = old->next;
			if (old->next)
				old->next->prev = old->prev;
		}
		/*
		 * Add positive [term].
		 */
		fprintf(stderr, "register: add positive: term=%p\n", term);
		/* dumpterm(term); */
		if (termq_add(&trans->positive, term) == -1)
			return -1;
		return 0;
	}
	/*
	 * Replace or add negative [term].
	 */
	for (struct termq *i = trans->negative; i; i = i->next) {
		if (strcmp(i->term->pkg->name, a->term.pkg->name) == 0) {
			i->term = term;
			return 0;
		}
	}
	if (termq_add(&trans->negative, term) == -1)
		return -1;
	return 0;
}

static int
_assign(struct assign *a)
{
	fprintf(stderr, "assign: 1\n");
	if (assignq_add(&trans->assignments, a) == -1 || _register(a) == -1)
		return -1;
	fprintf(stderr, "assign: 2\n");
	return 0;
}

static int
derive(struct term *term, bool positive, struct incompat *cause)
{
	struct assign *a = calloc(1, sizeof (struct assign));
	if (a == NULL)
		return -1;
	/* XXX: index? */
	a->level = trans->level;
	a->cause = cause;
	a->term.pkg = term->pkg;
	a->term.positive = positive;
	/* XXX: dup constraint? */
	a->term.constraint = term->constraint;
	if (_assign(a) == -1) {
		free(a);
		return -1;
	}
	return 0;
}

static int
decide(struct pkg *pkg, const char *version)
{
	struct decision *decision;
	struct assign *a;

	fprintf(stderr, "decide: 1\n");
	decision = lookupDecision(trans->decisions, pkg->name);
	if (decision == NULL) {
		decision = addDecision(trans->decisions, pkg->name);
		if (decision == NULL)
			return -1;
	}
	decision->version = version;
	fprintf(stderr, "decide: 2\n");

	if ((a = calloc(1, sizeof (struct assign))) == NULL)
		return -1;
	fprintf(stderr, "decide: 3\n");

	if (trans->backtracking) {
		trans->attempts++;
		trans->backtracking = false;
	}

	a->level = trans->level;
	a->cause = NULL;
	a->term.pkg = pkg;
	a->term.positive = true;
#if 0
	a->term.constraint = xbps_constraint_version(version);
#endif
	fprintf(stderr, "decide: 4\n");
	if (_assign(a) == -1) {
		free(a);
		return -1;
	}
	fprintf(stderr, "decide: 5\n");
	return 0;
}

static bool
samepkg(struct pkg *a, struct pkg *b)
{
	return strcmp(a->name, b->name) == 0;
}

static bool
termsatisfies(struct term *a, struct term *b)
{
	/* fputs("termsatisfies: ", stderr); */
	/* fprintterm(stderr, a); */
	/* fputs(" satisfies ", stderr); */
	/* fprintterm(stderr, b); */
	/* fprintf(stderr, " = %s\n", strrelation(termrelation(a, b))); */
	return termrelation(a, b) == SUBSET;
}

static int
satisfier(struct assign **outp, struct term *term)
{
	struct term *assignedTerm = NULL;
	for (struct assignq *i = trans->assignments; i; i = i->next) {
		if (strcmp(i->assign->term.pkg->name, term->pkg->name) != 0)
			continue;
		if (!i->assign->term.pkg->root &&
		    !samepkg(i->assign->term.pkg, term->pkg)) {
			if (!i->assign->term.positive)
				continue;
			assert(!term->positive);
			*outp = i->assign;
			return 0;
		}
		if (assignedTerm != NULL) {
			struct term *new = NULL;
			if (termintersect(&new, assignedTerm, &i->assign->term) == -1)
				return -1;
			/* XXX: assignedTerm leaked */
			fprintf(stderr, "assignedTerm; %p\n", assignedTerm);
			/* free(assignedTerm); */
			assignedTerm = new;
		} else {
			assignedTerm = &i->assign->term;
		}
		/*
		 * As soon as we have enough assignments to satisfy [term],
		 * return them.
		 */
		if (termsatisfies(assignedTerm, term)) {
			*outp = i->assign;
			return 0;
		}
	}
	fprintf(stderr, "[BUG] ");
	fprintterm(stderr, term);
	fprintf(stderr, " is not satisfied.\n");
	return -1;
}

static int
backtrack(size_t level)
{
	fprintf(stderr, "backtrack: %zu\n", level);
	return 0;
}

#define MAX(a, b) (a > b ? a : b)

static int
resolve(struct incompat **outp, struct incompat *incompat)
{
	bool newIncompat = false;
	struct termq *newTerms = NULL;

	size_t runs = 0;
	fprintf(stderr, "conflict: ");
	fprintincompat(stderr, incompat);
	fprintf(stderr, "\n");
	while (!isfailure(incompat)) {
		struct term *recentTerm = NULL, *diff = NULL;
		struct assign *recentSatisfier = NULL;
		size_t prevLevel = 1;
		fprintf(stderr, ">>> resolve runs=%zu\n", runs++);
		for (struct termq *i = incompat->terms; i; i = i->next) {
			struct assign *sat = NULL;
			fprintf(stderr, ">>> prevLevel=%zu\n", prevLevel);
			if (satisfier(&sat, i->term) == -1) {
				fprintf(stderr, "satisfier: -1\n");
				return -1;
			}
			fprintf(stderr, ">>> resolve 0.1\n");
			if (recentSatisfier == NULL) {
				recentTerm = i->term;
				recentSatisfier = sat;
			} else if (recentSatisfier->index < sat->index) {
				prevLevel = MAX(prevLevel, recentSatisfier->level);
				recentTerm = i->term;
				recentSatisfier = sat;
				diff = NULL;
			} else {
				prevLevel = MAX(prevLevel, sat->level);
			}
			fprintf(stderr, ">>> resolve 0.2\n");
			if (recentTerm == i->term) {
				fprintf(stderr, ">>> resolve 0.2.1\n");
				/* dumpterm(&recentSatisfier->term); */
				/* dumpterm(recentTerm); */
				if (termdiff(&diff, &recentSatisfier->term, recentTerm) == -1)
					return -1;
				fprintf(stderr, ">>> resolve 0.2.2\n");
				fprintf(stderr, "diff=%p\n", diff);
				if (diff != NULL) {
					struct assign *sat1 = NULL;
					/* struct term inv = *diff; */
					/* inv.positive = !inv.positive; */
					if (satisfier(&sat1, diff) == -1 || sat1 == NULL)
						return -1;
					fprintf(stderr, "wtf>>>>\n");
					prevLevel = MAX(prevLevel, sat1->level);
				}
			}
			fprintf(stderr, ">>> resolve 0.3\n");
		}
		fprintf(stderr, ">>> resolve 1\n");
		if (prevLevel < recentSatisfier->level ||
		    recentSatisfier->cause == NULL) {
			backtrack(prevLevel);
			if (newIncompat) {
				for (struct termq *i = incompat->terms; i; i = i->next) {
					if (pkg_add_incompat(i->term->pkg, incompat) == -1)
						return -1;
				}
			}
			*outp = incompat;
			return 0;
		}
		fprintf(stderr, ">>> resolve 2 incompat=%p\n", incompat);
		for (struct termq *i = incompat->terms; i; i = i->next) {
			if (i->term == recentTerm)
				continue;
			fprintf(stderr, ">>> add %p to newTerms: ", i->term);
			fprintterm(stderr, i->term);
			fputc('\n', stderr);
			if (termq_add(&newTerms, i->term) == -1)
				return -1;
		}
		fprintf(stderr, ">>> resolve 3\n");
		for (struct termq *i = recentSatisfier->cause->terms; i; i = i->next) {
			if (i->term->pkg == recentSatisfier->term.pkg)
				continue;
			if (termq_add(&newTerms, i->term) == -1)
				return -1;
		}
		if (diff != NULL) {
			struct term *new = calloc(1, sizeof (struct term));
			if (new == NULL)
				return -1;
			*new = *diff;
			new->positive = !new->positive;
			if (termq_add(&newTerms, new) == -1)
				return -1;
		}
		{
			struct incompat *old = incompat;
			incompat = calloc(1, sizeof (struct incompat));
			if (incompat == NULL)
				return -1;
			incompat->terms = newTerms;
			incompat->cause.type = CauseConflict;
			incompat->cause.this = old;
			incompat->cause.other = recentSatisfier->cause;
			newIncompat = true;
		}
		fprintf(stderr, "! ");
		fprintterm(stderr, recentTerm);
		fprintf(stderr, " is%s satisfied by ", diff ? " partially" : "");
		fprintassign(stderr, recentSatisfier);
		fputc('\n', stderr);
		fprintf(stderr, "! which is caused by ");
		fprintincompat(stderr, recentSatisfier->cause);
		fputc('\n', stderr);
		fprintf(stderr, "! thus: ");
		fprintincompat(stderr, incompat);
		fputc('\n', stderr);
	}
	fprintf(stderr, "failed to resolve incompatibility\n");
	abort();
	return -1;
}

static int
propagateIncompat(struct xbps_handle *xhp, struct pkg **outp,
		struct incompat *incompat)
{
	struct term *unsatisfied = NULL;

	/* xbps_dbg_printf(xhp, "propagateIncompat: %p\n", incompat); */
	fprintf(stderr, "propagateIncompat: %p\n", incompat);
	/* fprintincompat(stderr, incompat); */
	/* fputc('\n', stderr); */
	/* for (struct termq *i = incompat->terms; i; i = i->next) { */
	/* 	dumpterm(i->term); */
	/* } */

	for (struct termq *i = incompat->terms; i; i = i->next) {
		int rel = relation(i->term);
		fprintf(stderr, "term=%p relation=%s\n", i->term, strrelation(rel));
		switch (rel) {
		case DISJOINT:
			/*
			 * If [term] is already contradicted by [_solution], then
			 * [incompatibility] is contradicted as well and there's
			 * nothing new we can deduce from it.
			 */
			*outp = NULL;
			return 0;
		case OVERLAPPING:
			/*
			 * If more than one term is inconclusive, we can't deduce
			 * anything about [incompatibility].
			 */
			if (unsatisfied != NULL) {
				fprintf(stderr, "none\n");
				return 0;
			}
			/*
			 * If exactly one term in [incompatibility] is inconclusive,
			 * then it's almost satisfied and [term] is the unsatisfied term.
			 * We can add the inverse of the term to [_solution].
			 */
			unsatisfied = i->term;
		}
	}

	if (unsatisfied == NULL) {
		/*
		 * Found a conflict.
		 */
		xbps_dbg_printf(xhp, "conflict:\n");
		errno = EAGAIN;
		return -1;
	}

	fputs("derived: ", stderr);
	if (unsatisfied->positive)
		fputs("not ", stderr);
	fprintpkg(stderr, unsatisfied->pkg);
	fputc(' ', stderr);
	fprintconstraint(stderr, unsatisfied->constraint);
	fputc('\n', stderr);
	/* fprintterm(stderr, unsatisfied); */
	/* fputc('\n', stderr); */
	derive(unsatisfied, !unsatisfied->positive, incompat);
	*outp = unsatisfied->pkg;
	return 0;
}

static int
propagate(struct xbps_handle *xhp, struct pkg *pkg)
{
	struct pkgq head = {.pkg=pkg,.next=NULL,.prev=NULL};
	struct pkgq *changed = &head;

	/* xbps_dbg_printf(xhp, "propagate: %s\n", pkg->name); */

	while (changed) {
		struct pkg *cur;
		if (pkgq_pop(&changed, &cur) == -1)
			return -1;
		fprintf(stderr, "propagate: changed=%s nincompats=%zu\n", cur->name, cur->nincompats);
		for (size_t i = cur->nincompats; i > 0; i--) {
			struct pkg *add = NULL;
			if (propagateIncompat(xhp, &add, cur->incompats[i-1]) == -1) {
				if (errno == EAGAIN) {
					struct incompat *rootCause = NULL;
					if (resolve(&rootCause, cur->incompats[i-1]) == -1)
						return -1;
					changed = NULL;
					if (propagateIncompat(xhp, &add, cur->incompats[i-1]) == -1)
						return -1;
					if (pkgq_add(&changed, add) == -1)
						return -1;
					break;
				}
				return -1;
			}
			if (add) {
				xbps_dbg_printf(xhp, "changed: %s\n", add->name);
				if (pkgq_add(&changed, add) == -1)
					return -1;
			}
		}
	}
	return 0;
}

static const char *
bestversion(struct pkg *pkg, xbps_constraint_t *constraint)
{
	/*
	 * if package is on hold and [constraint] allows the version, use it.
	 * Otherwise if the hold version is not allowed by the constraint,
	 * return EPERM.
	 */
	if (pkg->hold != NULL) {
		if (xbps_constraint_allows(constraint, pkg->hold))
			return pkg->hold;
		fprintf(stderr, ">>>wtf\n");
		errno = EPERM;
		return NULL;
	}
	/* XXX: downgrade? */
	for (size_t i = pkg->nversions; i > 0; i--) {
		assert(pkg->versions[i-1]);
		if (xbps_constraint_allows(constraint, pkg->versions[i-1]))
			return pkg->versions[i-1];
	}
	/* no version allows by the constraint found */
	errno = ENOENT;
	return NULL;
}

static int
dependency(struct incompat **outp,
		struct pkg *depender, const char *dependerver,
		struct pkg *target, xbps_constraint_t *targetvers,
		bool *conflict)
{
	struct term *term;
	struct incompat *incompat = calloc(1, sizeof (struct incompat));
	if (incompat == NULL)
		return -1;
	incompat->cause.type = CauseDependency;

	/**/
#if 0
	for (size_t i = depender->nincompats; i > 0; i--) {
		for (struct termq *j = cur->incompats[i-1]->terms; j; j = j->next) {
			struct term tmp = {
				.pkg=depender,
				.positive=true,
				.pkgvers=dependerver,
			};
			if (termallows(j->term, &tmp)
				return 0;
		}
	}
#endif

	/**/
	term = malloc(sizeof (struct term));
	if (term == NULL) {
		free(incompat);
		return -1;
	}
	term->positive = true;
	term->pkg = depender;
#if 0
	term->constraint = xbps_constraint_version(dependerver);
#endif
	if (term->constraint == NULL) {
		free(incompat);
		free(term);
		return -1;
	}

	/* fprintf(stderr, "created term 1: %p\n", term); */
	/* dumpterm(term); */
	if (termq_add(&incompat->terms, term) == -1) {
		free(incompat);
		return -1;
	}
	if (!*conflict)
		*conflict = !satisfies(term);

	/* */
	term = calloc(1, sizeof (struct term));
	if (term == NULL) {
		free(incompat);
		return -1;
	}
	term->positive = false;
	term->pkg = target;
	term->constraint = targetvers;

	/* fprintf(stderr, "created term 2: %p\n", term); */
	/* dumpterm(term); */
	if (termq_add(&incompat->terms, term) == -1) {
		free(incompat);
		return -1;
	}
	/* if (!*conflict) */
	/* 	*conflict = !satisfies(term); */

	*outp = incompat;
	return 0;
}

static int
addincompatibility(struct incompat *incompat)
{
	/* fprintf(stderr, "addincompatibility: %p\n", incompat); */
	fputs("fact: ", stderr);
	fprintincompat(stderr, incompat);
	fputc('\n', stderr);
	for (struct termq *i = incompat->terms; i; i = i->next) {
		if (pkg_add_incompat(i->term->pkg, incompat) == -1)
			return -1;
	}
	return 0;
}

/*
 * Initialize the `pkg`s available versions.
 */
static int
versions(struct xbps_handle *xhp, struct pkg *pkg)
{
	xbps_dictionary_t pkgd, repopkgd;
	const char *pkgver = NULL, *repopkgver = NULL;

	/* fprintf(stderr, "versions: %s\n", pkg->name); */

	if (pkg->versions != NULL)
		return 0;

	if ((pkgd = xbps_pkgdb_get_pkg(xhp, pkg->name)) == NULL)
		pkgd = xbps_pkgdb_get_virtualpkg(xhp, pkg->name);
	if (pkgd)
		xbps_dictionary_get_cstring_nocopy(pkgd, "pkgver", &pkgver);

	if ((repopkgd = xbps_rpool_get_pkg(xhp, pkg->name)) == NULL)
		repopkgd = xbps_rpool_get_virtualpkg(xhp, pkg->name);
	if (repopkgd)
		xbps_dictionary_get_cstring_nocopy(repopkgd, "pkgver", &repopkgver);

	/* same pkgver, no need to parse repopkgver */
	if (pkgver && repopkgver && strcmp(pkgver, repopkgver) == 0)
		repopkgver = NULL;

	/* only one version available */
	if (pkgver == NULL || repopkgver == NULL) {
		pkg->nversions = 1;
		pkg->versions = malloc(sizeof (const char *));
		if (pkg->versions == NULL)
			return -1;
		pkg->versions[0] = pkgver ? pkgver : repopkgver;
		if (pkg->versions[0] == NULL) {
			free(pkg->versions);
			pkg->versions = NULL;
			return -1;
		}
		pkg->pkgds = malloc(sizeof (xbps_dictionary_t));
		if (pkg->pkgds == NULL)
			return -1;
		pkg->pkgds[0] = pkgver ? pkgd : repopkgd;
		pkg->listed = malloc(sizeof (bool));
		if (pkg->listed == NULL)
			return -1;
		pkg->listed[0] = false;
		return 0;
	}

	/* XXX: do we want to provide all versions if bestmatching is set? */

	/* two available versions */
	/* fprintf(stderr, "versions: 2\n"); */
	pkg->nversions = 2;
	pkg->versions = malloc(sizeof (const char *)*2);
	if (pkg->versions == NULL)
		return -1;

	pkg->versions[0] = pkgver;
	if (pkg->versions[0] == NULL) {
		free(pkg->versions);
		pkg->versions = NULL;
		return -1;
	}
	pkg->versions[1] = repopkgver;
	if (pkg->versions[1] == NULL) {
		free(pkg->versions);
		pkg->versions = NULL;
		return -1;
	}
	pkg->pkgds = malloc(sizeof (xbps_dictionary_t)*2);
	if (pkg->pkgds == NULL) {
		free(pkg->versions);
		pkg->versions = NULL;
		return -1;
	}
	pkg->pkgds[0] = pkgd;
	pkg->pkgds[1] = repopkgd;
	pkg->listed = malloc(sizeof (bool)*2);
	if (pkg->listed == NULL)
		return -1;
	pkg->listed[0] = false;
	pkg->listed[1] = false;
	return 0;
}

static int
dependencies(struct xbps_handle *xhp, struct pkg *pkg, const char *version,
		bool *conflict)
{
	xbps_dictionary_t pkgd = NULL;
	xbps_array_t rdeps;
	bool *listed;

	for (size_t i = 0; i < pkg->nversions; i++) {
		if (pkg->versions[i] != version)
			continue;
		pkgd = pkg->pkgds[i];
		listed = &pkg->listed[i];
		break;
	}
	if (*listed)
		return 0;
	if (pkgd == NULL) {
		assert(pkgd);
		return -1;
	}

	rdeps = xbps_dictionary_get(pkgd, "run_depends");
	/* fprintf(stderr, "incompatibilites: rdeps=%u\n", xbps_array_count(rdeps)); */
	for (unsigned int i = 0; i < xbps_array_count(rdeps); i++) {
		struct pkg *deppkg;
		const char *curdep = NULL;
		size_t pkgnlen;
		struct incompat *incompat = NULL;
		xbps_constraint_t *depvers;

		xbps_array_get_cstring_nocopy(rdeps, i, &curdep);
		/* fprintf(stderr, ">>> %s\n", curdep); */
		pkgnlen = xbps_pkgname_len(curdep);
		deppkg = lookupPkg(curdep, pkgnlen);
		if (deppkg == NULL) {
			if (addPkg(&deppkg, curdep, pkgnlen) == -1)
				return -1;
			if (versions(xhp, deppkg) == -1)
				return -1;
		}
#if 0
		depvers = xbps_constraint_parse(curdep, strlen(curdep));
		if (depvers == NULL) {
			assert(depvers);
			return -1;
		}
#else
		depvers = NULL;
#endif
		if (dependency(&incompat, pkg, version, deppkg, depvers, conflict) == -1)
			return -1;
		if (addincompatibility(incompat) == -1)
			return -1;
	}
	*listed = true;
	return 0;
}

static int
rootincompatibilites(struct xbps_handle *xhp, bool *conflict)
{
	xbps_object_t obj;
	xbps_object_iterator_t iter;

	/* fprintf(stderr, "rootincompatibilites:\n"); */

	iter = xbps_dictionary_iterator(xhp->pkgdb);
	assert(iter);

	while ((obj = xbps_object_iterator_next(iter))) {
		xbps_dictionary_t pkgd;
		struct pkg *pkg;
		const char *pkgver;
		size_t pkgnlen;
		bool automatic = false;
		struct incompat *incompat = NULL;
		xbps_constraint_t *pkgvers;
		const char *key;

		pkgd = xbps_dictionary_get_keysym(xhp->pkgdb, obj);
		key = xbps_dictionary_keysym_cstring_nocopy(obj);
		if (strncmp(key, "_XBPS_", 6) == 0)
			continue;

		automatic = false;
		xbps_dictionary_get_bool(pkgd, "automatic-install", &automatic);
		if (automatic)
			continue;

		xbps_dictionary_get_cstring_nocopy(pkgd, "pkgver", &pkgver);
		pkgnlen = xbps_pkgname_len(pkgver);
		pkg = lookupPkg(pkgver, pkgnlen);
		if (pkg == NULL) {
			if (addPkg(&pkg, pkgver, pkgnlen) == -1)
				return -1;
			if (versions(xhp, pkg) == -1)
				return -1;
		}
#if 0
		pkgvers = xbps_constraint_versions(pkg->versions, pkg->nversions);
		assert(pkgvers);
		if (pkgvers == NULL) {
			abort();
			return -1;
		}
#else
		pkgvers = NULL;
#endif
		if (dependency(&incompat, &trans->root, trans->rootv, pkg, pkgvers, conflict) == -1)
			return -1;
		if (addincompatibility(incompat) == -1)
			return -1;
	}

	return 0;
}

#if 0
static int
replaces(struct xbps_handle *xhp, xbps_dictionary_t pkgd, const char *replacee)
{
	xbps_array_t array;

	array = xbps_dictionary_get(pkgd, "replaces");
	if (array == NULL)
		return 0;

	for (unsigned int i = 0; i < xbps_array_count(array); i++) {
		int match = 0;
		const char *pattern = xbps_array_get_cstring_nocopy(array, i);
		assert(replacee);
		if ((match = xbps_pkgpattern_match(replacee, pattern)) == -1) {
			xbps_error_printf("xbps_pkgpattern_match: %s\n", strerror(errno));
			return -1;
		}
	}
}
#endif

/*
 *
 */
static int
incompatibilites(struct xbps_handle *xhp, struct pkg *pkg,
		const char *version, bool *conflict)
{
	/* XXX: should this add an incompatibility? */
	if (xbps_constraint_allows(pkg->invalids, version))
		return 0;

	/* XXX: hold */

	return dependencies(xhp, pkg, version, conflict);
}

/*
 * return the number of `pkg`s versions that are allowd by `constraint`.
 */
static size_t
countversions(struct pkg *pkg, xbps_constraint_t *constraint)
{
	size_t count = 0;
	if (pkg->hold != NULL) {
		if (xbps_constraint_allows(constraint, pkg->hold))
			return 1;
		return 0;
	}
	for (size_t i = 0; i < pkg->nversions; i++) {
		if (xbps_constraint_allows(constraint, pkg->versions[i]))
			count++;
	}
	return count;
}

static int
notfound(struct pkg *pkg)
{
	struct term *term;
	struct incompat *incompat = calloc(1, sizeof (struct incompat));
	if (incompat == NULL)
		return -1;
	incompat->cause.type = CauseNotFound;
	term = calloc(1, sizeof (struct term));
	if (term == NULL) {
		free(incompat);
		return -1;
	}
	term->positive = true;
	term->pkg = pkg;
#if 0
	term->constraint = xbps_constraint_any();
#endif
	if (term->constraint == NULL) {
		free(term);
		free(incompat);
		return -1;
	}
	if (termq_add(&incompat->terms, term) == -1) {
		free(incompat);
		free(term->constraint);
		free(term);
		return -1;
	}
	if (addincompatibility(incompat) == -1) {
		free(incompat);
		free(term->constraint);
		free(term);
		return -1;
	}
	return 0;
}

static int
choose(struct xbps_handle *xhp, struct pkg **outp)
{
	struct pkg *pkg = NULL;
	xbps_constraint_t *constraint = NULL;
	const char *version = NULL;
	size_t nversions = 0;
	bool conflict = false;

	fprintf(stderr, "choose:\n");

	/*
	 * Prefer the package with as few remaining versions as possible,
	 * so that if a conflict is necessary it's forced quickly.
	 */
	for (struct termq *i = trans->positive; i; i = i->next) {
		/*
		 * skip already decided packages.
		 */
		if (lookupDecision(trans->decisions, i->term->pkg->name) != NULL)
			continue;
		if (pkg != NULL) {
			if (countversions(i->term->pkg, i->term->constraint) >= nversions)
				continue;
		}
		pkg = i->term->pkg;
		constraint = i->term->constraint;
		/*
		 * If a package has no remaining versions we can bail out early
		 * and print and error or resolve a conflict.
		 */
		if (nversions == 0)
			break;
	}
	/*
	 * No remaining package found.
	 */
	if (pkg == NULL) {
		*outp = NULL;
		return 0;
	}
	fprintf(stderr, "choose: pkgname=%s\n", pkg->name);

	if (pkg->root) {
		version = trans->rootv;
		if (rootincompatibilites(xhp, &conflict) == -1)
			return -1;
	} else {
		fprintf(stderr, "choose: bestversion:\n");
		version = bestversion(pkg, constraint);
		fprintf(stderr, "choose: bestversion: %p\n", version);
		if (version == NULL) {
			switch (errno) {
			case ENOENT:
				errno = 0;
				if (notfound(pkg) == -1)
					return -1;
				/* XXX: add not found incompatibility */
				fprintf(stderr, ">>>> no version found\n");
				*outp = pkg;
				return 0;
			case EPERM:
				/* XXX: add hold incompatibility */
				fprintf(stderr, ">>>> no version found: hold\n");
				*outp = pkg;
				return -1;
			default:
				fprintf(stderr, ">>>> no version found: unknown err\n");
				/* XXX: unknown error? */
				return -1;
			}
		}
		fprintf(stderr, "choose: incompatibilites:\n");
		if (incompatibilites(xhp, pkg, version, &conflict) == -1)
			return -1;
		fprintf(stderr, "choose: incompatibilites: done\n");
	}

#if 0
	fprintf(stderr, "choose: version=%s\n", xbps_version_str(version));
#endif

	/*
	 * If there was no conflict while adding the versions incompatibilites,
	 * we can decide to use this version.
	 */
	if (!conflict) {
#if 0
		/* xbps_dbg_printf(xhp, "selecting %s\n", version->pkgver); */
		fprintf(stderr, "selecting %s-%s\n", pkg->name, xbps_version_str(version));
#endif
		fprintf(stderr, ">>>>wtf 1\n");
		if (decide(pkg, version) == -1)
			return -1;
		fprintf(stderr, ">>>>wtf 2\n");
	}

	*outp = pkg;
	return 0;
}

int
xbps_transaction_solve(struct xbps_handle *xhp)
{
	struct pkg *next;
	struct incompat rootin = {.terms=NULL, .cause={.type=CauseRoot}};
	struct term rootterm = {.pkg=&trans->root, .positive=false};
#if 0
	size_t upd = 0;
	size_t down = 0;
	size_t inst = 0;
#endif

#if 0
	rootterm.constraint = xbps_constraint_version(trans->rootv);
#endif
	if (rootterm.constraint == NULL)
		return -1;
	if (termq_add(&rootin.terms, &rootterm) == -1)
		return -1;
	if (pkg_add_incompat(&trans->root, &rootin) == -1)
		return -1;

	next = &trans->root;

	while (next) {
		if (propagate(xhp, next) == -1)
			break;
		if (choose(xhp, &next) == -1)
			return errno;
	}

	/* XXX: fix decisions */
#if 0
	for (struct pkgverq *i = trans->decisions; i; i = i->next) {
		xbps_dictionary_t pkgd;
		const char *pkgver = NULL;
		if (i->pkgver == &trans->rootv)
			continue;
		if ((pkgd = xbps_pkgdb_get_pkg(xhp, i->pkgver->name)) == NULL)
			pkgd = xbps_pkgdb_get_virtualpkg(xhp, i->pkgver->name);
		if (pkgd == NULL) {
			fprintf(stderr, "%s (install)\n", i->pkgver->pkgver);
			inst++;
			continue;
		}
		xbps_dictionary_get_cstring_nocopy(pkgd, "pkgver", &pkgver);
		if (strcmp(pkgver, i->pkgver->pkgver) == 0)
			continue;
		if (xbps_cmpver(i->pkgver->pkgver, pkgver) == 1) {
			fprintf(stderr, "%s -> %s (update)\n", pkgver, i->pkgver->pkgver);
			upd++;
			continue;
		}
		fprintf(stderr, "%s -> %s (downgrade)\n", pkgver, i->pkgver->pkgver);
		down++;
	}
	fprintf(stderr, "install: %zu update: %zu downgrade: %zu\n", inst, upd, down);
#endif

	return 0;
}
