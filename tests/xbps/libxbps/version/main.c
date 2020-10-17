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
 *-
 */

#include <stdlib.h>

#include <atf-c.h>
#include <xbps.h>

#if 0
ATF_TC(xbps_constraint_allows_all);

ATF_TC_HEAD(xbps_constraint_allows_all, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test xbps_constraint_allows_all");
}

ATF_TC_BODY(xbps_constraint_allows_all, tc)
{
#if 0
#define TEST_ALLOWS_ALL(a, b, c) do { \
		xbps_constraint_t *vc; \
		ATF_REQUIRE((vc = xbps_constraint_versions((a), \
		    sizeof ((a))/sizeof ((a)[0])))); \
		ATF_CHECK_EQ_MSG(xbps_constraint_allows_all(vc, b), (c), \
		    "xbps_constraint_allows("#a", "#b") != "#c); \
		free(vc); \
	} while(0)

	TEST_ALLOWS_ALL(((const char *[]){">0", ">100"}), "1", true);
	TEST_ALLOWS_ALL(((const char *[]){">0", ">100"}), "100", true);
	TEST_ALLOWS_ALL(((const char *[]){"<0", "<100"}), "100", false);
	TEST_ALLOWS_ALL(((const char *[]){"<0", "<100"}), "99", true);
	/* TEST_ALLOWS_ALL({">0"}, 1, "100", true); */
	struct test {
		const char **a;
		const char **b;
		bool exp;
	} tests[] = {
		{
			((const char *[]){"<250", NULL}),
			((const char *[]){">80<140", NULL}),
			true,
		},
		{
			((const char *[]){"<250", NULL}),
			((const char *[]){">80<300", NULL}),
			false,
		},
		{
			((const char *[]){"<250", NULL}),
			((const char *[]){"<140", NULL}),
			true,
		},
		{
			((const char *[]){"<250", NULL}),
			((const char *[]){"<300", NULL}),
			false,
		},
		{
			((const char *[]){"<250", NULL}),
			((const char *[]){"<250", NULL}),
			true,
		},
		{
			((const char *[]){">10", NULL}),
			((const char *[]){">80<140", NULL}),
			true,
		},
		{
			((const char *[]){">10", NULL}),
			((const char *[]){">3<140", NULL}),
			false,
		},
		{
			((const char *[]){">10", NULL}),
			((const char *[]){">10", NULL}),
			true,
		},
		{
			((const char *[]){">10<250", NULL}),
			((const char *[]){">80<140", NULL}),
			true,
		},
		{
			((const char *[]){">10<250", NULL}),
			((const char *[]){">80<300", NULL}),
			false,
		},
		{
			((const char *[]){">10<250", NULL}),
			((const char *[]){">3<140", NULL}),
			false,
		},
		{
			((const char *[]){">10<250", NULL}),
			((const char *[]){">80", NULL}),
			false,
		},
		{
			((const char *[]){">10<250", NULL}),
			((const char *[]){"<140", NULL}),
			false,
		},
		{
			((const char *[]){">10<250", NULL}),
			((const char *[]){">10<250", NULL}),
			true,
		},
		{
			((const char *[]){">10", ">100", NULL}),
			((const char *[]){">1<10", ">100<200", NULL}),
			false,
		},
		{
			((const char *[]){">=10", ">100", NULL}),
			((const char *[]){">1<=10", NULL}),
			false,
		},
		{
			((const char *[]){">0", ">100", NULL}),
			((const char *[]){">101", NULL}),
			true,
		},
		{
			((const char *[]){">0", ">100", NULL}),
			((const char *[]){"<99", NULL}),
			false,
		},
		{
			((const char *[]){">10", ">100", NULL}),
			((const char *[]){">1", ">10", NULL}),
			false,
		},
		{
			((const char *[]){">20", ">200", NULL}),
			((const char *[]){">1", NULL}),
			false,
		},
		{
			((const char *[]){">20", ">200", NULL}),
			((const char *[]){">300", NULL}),
			true,
		},
		{
			((const char *[]){">10<12", ">100<102", NULL}),
			((const char *[]){">11", NULL}),
			false,
		},
		{
			((const char *[]){">10<12", ">100<102", NULL}),
			((const char *[]){">1<3", NULL}),
			false,
		}
	};

	for (unsigned int i = 0; i < (sizeof tests / sizeof tests[0]); i++) {
		xbps_constraint_t *vc1 = xbps_constraint_versions(tests[i].a);
		xbps_constraint_t *vc2 = xbps_constraint_versions(tests[i].b);
		ATF_REQUIRE(vc1 && vc2);
		ATF_CHECK_EQ_MSG(xbps_constraint_allows_all(vc1, vc2), tests[i].exp,
		    "xbps_constraint_allows_all(vc1, vc2) != tests[%d].exp", i);
		free(vc1);
		free(vc2);
	}
#endif
}
#endif

ATF_TC(xbps_version_cmp);

ATF_TC_HEAD(xbps_version_cmp, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test xbps_version_cmp");
}

ATF_TC_BODY(xbps_version_cmp, tc)
{
	ATF_CHECK_EQ(xbps_version_cmp("1", "1"), 0);
	ATF_CHECK_EQ(xbps_version_cmp("1", "0"), 1);
	ATF_CHECK_EQ(xbps_version_cmp("0", "1"), -1);
	ATF_CHECK_EQ(xbps_version_cmp("2", "2"), 0);
	ATF_CHECK_EQ(xbps_version_cmp("2", "0"), 1);
	ATF_CHECK_EQ(xbps_version_cmp("0", "2"), -1);
	ATF_CHECK_EQ(xbps_version_cmp("1_0", "1_0"), 0);
	ATF_CHECK_EQ(xbps_version_cmp("1_1", "1_1"), 0);
	ATF_CHECK_EQ(xbps_version_cmp("1_2", "1_1"), 1);
	ATF_CHECK_EQ(xbps_version_cmp("1_1", "1_2"), -1);
	ATF_CHECK_EQ(xbps_version_cmp("1_2", "1_2"), 0);
}

ATF_TC(xbps_version_cmpn);

ATF_TC_HEAD(xbps_version_cmpn, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test xbps_version_cmpn");
}

ATF_TC_BODY(xbps_version_cmpn, tc)
{
#define TEST(a, an, b, bn, r)                                              \
	do {                                                                   \
		const char *xa = (a);                                              \
		const char *xb = (b);                                              \
		ATF_CHECK_EQ_MSG(xbps_version_cmpn(xa, xa+(an), xb, xb+(bn)), (r), \
		    "xbps_version_cmpn(\"%s\", %d, \"%s\", %d) != %d", xa, (an),   \
		    xb, (bn), (r));                                                \
	} while (0);

	TEST("923", 1, "9", 1, 0);
	TEST("923", 3, "9", 1, 1);
	TEST("9", 1, "923", 1, 0);
	TEST("9", 1, "923", 3, -1);

	TEST("1_1", 3, "1_1", 1, 1);
	TEST("1_1", 3, "1_1", 3, 0);

	TEST("1_1", 2, "1_1", 2, 0);
	TEST("1_1", 2, "1_1", 2, 0);
	TEST("1_1", 3, "1_1", 3, 0);
	TEST("1_1", 3, "1_1", 2, 1);
	TEST("1_1", 2, "1_1", 3, -1);

	TEST("11", 1, "10", 1, 0);
	TEST("11", 2, "10", 2, 1);

	TEST("1beta", 2, "1beta", 5, 1);
	TEST("1beta", 5, "1beta", 2, -1);

	TEST("beta", 4, "beta", 4, 0);

	/* each alpha character interpreted
	 * as dot following its offset in the alphabet,
	 */
	TEST("abc", 3, ".1.2.3", 6, 0);

	/*
	 * therefore:
	 * "bet" > "beta"
	 */
	TEST("beta", 3, "beta", 4, 1);
	TEST("bet", 3, ".2.5.20", 7, 0);

	TEST("betabeta", 4, "betabeta", 4, 0);
	TEST("betabeta", 8, "betabeta", 8, 0);
	TEST("betabeta", 8, "betabeta", 4, -1);
	TEST("betabeta", 4, "betabeta", 8, 1);

	TEST("betaalpha", 9, "betaalpha", 9, 0);
	TEST("betabeta", 9, "betabeta", 4, -1);
	TEST("betabeta", 4, "betabeta", 9, 1);
	TEST("betaa", 5, "betaalpha", 9, 1);

	TEST("beta1", 5, "beta1", 5, 0);
	TEST("beta1", 5, "beta0", 5, 1);
	TEST("beta0", 5, "beta1", 5, -1);
	TEST("betaa", 5, "betaa", 5, 0);
	TEST("betab", 5, "betab", 5, 0);
	TEST("betab", 5, "betaa", 5, 1);
	TEST("betaa", 5, "betab", 5, -1);

	TEST("beta1", 5, "beta1", 5, 0);
	TEST("beta2", 5, "beta1", 5, 1);
	TEST("beta1", 5, "beta2", 5, -1);

	/* this is outside of the buffer but we end at \0 */
	TEST("1.", 5, "1.", 5, 0);
	TEST("1\01", 3, "1\00", 3, 0);

#undef TEST
}

ATF_TP_ADD_TCS(tp)
{
	/* ATF_TP_ADD_TC(tp, xbps_constraint_allows_all); */
	ATF_TP_ADD_TC(tp, xbps_version_cmp);
	ATF_TP_ADD_TC(tp, xbps_version_cmpn);
	return atf_no_error();
}
