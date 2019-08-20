/*-
 * Copyright (c) 2008-2015 Juan Romero Pardines.
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
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

#include <xbps.h>

static void __attribute__((noreturn))
usage(bool fail)
{
	fprintf(stdout,
	    "Usage: xbps-query [OPTIONS] MODE [ARGUMENTS]\n"
	    "\nOPTIONS\n"
	    " -C --config <dir>        Path to confdir (xbps.d)\n"
	    " -c --cachedir <dir>      Path to cachedir\n"
	    " -d --debug               Debug mode shown to stderr\n"
	    " -h --help                Print help usage\n"
	    " -i --ignore-conf-repos   Ignore repositories defined in xbps.d\n"
	    " -M --memory-sync         Remote repository data is fetched and stored\n"
	    "                          in memory, ignoring on-disk repodata archives.\n"
	    " -p --property PROP[,...] Show properties for PKGNAME\n"
	    " -R --repository          Enable repository mode. This mode explicitly\n"
	    "                          looks for packages in repositories.\n"
	    "    --repository=<url>    Enable repository mode and add repository\n"
	    "                          to the top of the list. This option can be\n"
	    "                          specified multiple times.\n"
	    "    --regex               Use Extended Regular Expressions to match\n"
	    "    --fulldeptree         Full dependency tree for -x/--deps\n"
	    " -r --rootdir <dir>       Full path to rootdir\n"
	    " -V --version             Show XBPS version\n"
	    " -v --verbose             Verbose messages\n"
	    "\nMODE\n"
	    " -l --list-pkgs           List installed packages\n"
	    " -L --list-repos          List registered repositories\n"
	    " -H --list-hold-pkgs      List packages on hold state\n"
	    "    --list-repolock-pkgs  List repolocked packages\n"
	    " -m --list-manual-pkgs    List packages installed explicitly\n"
	    " -O --list-orphans        List package orphans\n"
	    " -o --ownedby FILE        Search for package files by matching STRING or REGEX\n"
	    " -S --show PKG            Show information for PKG [default mode]\n"
	    " -s --search PKG          Search for packages by matching PKG, STRING or REGEX\n"
	    "    --cat=FILE PKG        Print FILE from PKG binpkg to stdout\n"
	    " -f --files PKG           Show package files for PKG\n"
	    " -x --deps PKG            Show dependencies for PKG\n"
	    " -X --revdeps PKG         Show reverse dependencies for PKG\n");

	exit(fail ? EXIT_FAILURE : EXIT_SUCCESS);
}

int
main(int argc, char **argv)
{
	const char *shortopts = "C:c:df:hHiLlMmOo:p:Rr:s:S:VvX:x:";
	const struct option longopts[] = {
		{ "config", required_argument, NULL, 'C' },
		{ "cachedir", required_argument, NULL, 'c' },
		{ "debug", no_argument, NULL, 'd' },
		{ "help", no_argument, NULL, 'h' },
		{ "ignore-conf-repos", no_argument, NULL, 'i' },
		{ "memory-sync", no_argument, NULL, 'M' },
		{ "repository", optional_argument, NULL, 'R' },
		{ "rootdir", required_argument, NULL, 'r' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ NULL, 0, NULL, 0 },
	};
	struct xbps_handle xh;
	const char *pkg, *rootdir, *cachedir, *confdir, *props, *catfile;
	int c, flags, rv;

	rootdir = cachedir = confdir = props = pkg = catfile = NULL;
	flags = rv = c = 0;

	memset(&xh, 0, sizeof(xh));

	while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
		switch (c) {
		case 'C':
			confdir = optarg;
			break;
		case 'c':
			cachedir = optarg;
			break;
		case 'd':
			flags |= XBPS_FLAG_DEBUG;
			break;
		case 'h':
			usage(false);
			/* NOTREACHED */
		case 'i':
			flags |= XBPS_FLAG_IGNORE_CONF_REPOS;
			break;
		case 'M':
			flags |= XBPS_FLAG_REPOS_MEMSYNC;
			break;
		case 'R':
			if (optarg != NULL) {
				xbps_repo_store(&xh, optarg);
			}
			break;
		case 'r':
			rootdir = optarg;
			break;
		case 'v':
			flags |= XBPS_FLAG_VERBOSE;
			break;
		case 'V':
			printf("%s\n", XBPS_RELVER);
			exit(EXIT_SUCCESS);
		case '?':
			usage(true);
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	/*
	 * Initialize libxbps.
	 */
	if (rootdir)
		xbps_strlcpy(xh.rootdir, rootdir, sizeof(xh.rootdir));
	if (cachedir)
		xbps_strlcpy(xh.cachedir, cachedir, sizeof(xh.cachedir));
	if (confdir)
		xbps_strlcpy(xh.confdir, confdir, sizeof(xh.confdir));

	xh.flags = flags;

	if ((rv = xbps_init(&xh)) != 0) {
		xbps_error_printf("Failed to initialize libxbps: %s\n",
		    strerror(rv));
		exit(EXIT_FAILURE);
	}
	if ((rv = xbps_transaction_init1(&xh)) != 0) {
		xbps_error_printf("Failed to initialize transaction: %s\n",
		    strerror(rv));
		exit(EXIT_FAILURE);
	}
	if ((rv = xbps_transaction_solve(&xh)) != 0) {
		xbps_error_printf("Failed to solve transaction: %s\n",
		    strerror(rv));
		exit(EXIT_FAILURE);
	}
	xbps_end(&xh);
	exit(rv);
}
