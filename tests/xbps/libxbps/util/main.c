/*-
 * Copyright (c) 2012-2014 Juan Romero Pardines.
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
#include <string.h>
#include <atf-c.h>
#include <xbps.h>

ATF_TC(util_test);

ATF_TC_HEAD(util_test, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test some utility functions");
}

ATF_TC_BODY(util_test, tc)
{
	ATF_CHECK_EQ(xbps_pkg_name("font-adobe-a"), NULL);
	ATF_CHECK_EQ(xbps_pkg_name("font-adobe-1"), NULL);
	ATF_CHECK_EQ(xbps_pkg_name("font-adobe-100dpi"), NULL);
	ATF_CHECK_EQ(xbps_pkg_name("font-adobe-100dpi-7.8"), NULL);
	ATF_CHECK_EQ(xbps_pkg_name("python-e_dbus"), NULL);
	ATF_CHECK_EQ(xbps_pkg_name("fs-utils-v1"), NULL);
	ATF_CHECK_EQ(xbps_pkg_name("fs-utils-v_1"), NULL);
	ATF_CHECK_EQ(xbps_pkg_version("font-adobe-100dpi"), NULL);
	ATF_CHECK_EQ(xbps_pkg_version("font-adobe-100dpi-7.8"), NULL);
	ATF_CHECK_EQ(xbps_pkg_version("python-e_dbus"), NULL);
	ATF_CHECK_EQ(xbps_pkg_version("python-e_dbus-1"), NULL);
	ATF_REQUIRE_STREQ(xbps_pkg_name("font-adobe-100dpi-7.8_2"), "font-adobe-100dpi");
	ATF_REQUIRE_STREQ(xbps_pkg_name("systemd-43_1"), "systemd");
	ATF_REQUIRE_STREQ(xbps_pkg_name("font-adobe-100dpi-1.8_blah"), "font-adobe-100dpi");
	ATF_REQUIRE_STREQ(xbps_pkg_name("python-e_dbus-1.0_1"), "python-e_dbus");
	ATF_REQUIRE_STREQ(xbps_pkg_version("font-adobe-100dpi-7.8_2"), "7.8_2");
	ATF_REQUIRE_STREQ(xbps_pkg_version("font-adobe-100dpi-1.8_blah"), "1.8_blah");
	ATF_REQUIRE_STREQ(xbps_pkg_version("python-e_dbus-1_1"), "1_1");
	ATF_REQUIRE_STREQ(xbps_pkg_version("fs-utils-v1_1"), "v1_1");
	ATF_REQUIRE_STREQ(xbps_pkg_revision("systemd-43_1_0"), "0");
	ATF_REQUIRE_STREQ(xbps_pkg_revision("systemd_21-43_0"), "0");
	ATF_REQUIRE_STREQ(xbps_pkgpattern_name("systemd>=43"), "systemd");
	ATF_REQUIRE_STREQ(xbps_pkgpattern_name("systemd>43"), "systemd");
	ATF_REQUIRE_STREQ(xbps_pkgpattern_name("systemd<43"), "systemd");
	ATF_REQUIRE_STREQ(xbps_pkgpattern_name("systemd<=43"), "systemd");
	ATF_REQUIRE_STREQ(xbps_pkgpattern_name("systemd-[0-9]*"), "systemd");
	ATF_REQUIRE_STREQ(xbps_pkgpattern_name("systemd>4[3-9]?"), "systemd");
	ATF_REQUIRE_STREQ(xbps_pkgpattern_name("systemd<4_1?"), "systemd");
	ATF_CHECK_EQ(xbps_pkgpattern_name("*nslookup"), NULL);
	ATF_REQUIRE_STREQ(xbps_binpkg_arch("/path/to/foo-1.0_1.x86_64.xbps"), "x86_64");
	ATF_REQUIRE_STREQ(xbps_binpkg_arch("/path/to/foo-1.0_1.x86_64-musl.xbps"), "x86_64-musl");
	ATF_REQUIRE_STREQ(xbps_binpkg_arch("foo-1.0_1.x86_64-musl.xbps"), "x86_64-musl");
	ATF_REQUIRE_STREQ(xbps_binpkg_arch("foo-1.0_1.x86_64.xbps"), "x86_64");
	ATF_REQUIRE_STREQ(xbps_binpkg_pkgver("foo-1.0_1.x86_64.xbps"), "foo-1.0_1");
	ATF_REQUIRE_STREQ(xbps_binpkg_pkgver("foo-1.0_1.x86_64-musl.xbps"), "foo-1.0_1");
	ATF_REQUIRE_STREQ(xbps_binpkg_pkgver("/path/to/foo-1.0_1.x86_64.xbps"), "foo-1.0_1");
	ATF_REQUIRE_STREQ(xbps_binpkg_pkgver("/path/to/foo-1.0_1.x86_64-musl.xbps"), "foo-1.0_1");
	ATF_CHECK_EQ(xbps_binpkg_pkgver("foo-1.0.x86_64.xbps"), NULL);
	ATF_CHECK_EQ(xbps_binpkg_pkgver("foo-1.0.x86_64"), NULL);
}

ATF_TC(pkgname_cpy_test);

ATF_TC_HEAD(pkgname_cpy_test, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test xbps_pkgname_cpy function");
}

ATF_TC_BODY(pkgname_cpy_test, tc)
{
	char buf[XBPS_MAXPKGNAME];

#define CHECK(in, out) \
		ATF_CHECK_EQ(xbps_pkgname_cpy(buf, in, sizeof buf), \
				sizeof (out)-1); \
		ATF_CHECK_STREQ(buf, out);

	CHECK("font-adobe-100dpi-7.8_2", "font-adobe-100dpi");
	CHECK("systemd-43_1", "systemd");
	CHECK("font-adobe-100dpi-1.8_blah", "font-adobe-100dpi");
	CHECK("python-e_dbus-1.0_1", "python-e_dbus");
	CHECK("font-adobe-a", "font-adobe-a");
	CHECK("font-adobe-1", "font-adobe-1");
	CHECK("font-adobe-100dpi", "font-adobe-100dpi");
	CHECK("font-adobe-100dpi-7.8", "font-adobe-100dpi-7.8");
	CHECK("python-e_dbus", "python-e_dbus");
	CHECK("fs-utils-v1", "fs-utils-v1");
	CHECK("fs-utils-v_1", "fs-utils-v_1");
	CHECK("systemd>=43", "systemd");
	CHECK("systemd>43", "systemd");
	CHECK("systemd<43", "systemd");
	CHECK("systemd<=43", "systemd");
	CHECK("systemd-[0-9]*", "systemd");
	CHECK("systemd>4[3-9]?", "systemd");
	CHECK("systemd<4_1?", "systemd");
	#undef CHECK

	/* check if truncated pkgnames return the required buffer length */
	ATF_CHECK_EQ(xbps_pkgname_cpy(buf, "hello", 2), 5);
	ATF_CHECK_STREQ(buf, "h");
	ATF_CHECK_EQ(xbps_pkgname_cpy(buf, "hello-1_1", 2), 5);
	ATF_CHECK_STREQ(buf, "h");
}


ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, util_test);
	ATF_TP_ADD_TC(tp, pkgname_cpy_test);
	return atf_no_error();
}
