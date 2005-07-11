#  Copyright 2005 Adam Jackson.
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  ADAM JACKSON BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# Process this file with autoconf to produce a configure script

AC_PREREQ(2.57)
AC_INIT([xf86-video-tdfx],
        0.1.0,
        [ajax@freedesktop.org],
        xf86-video-tdfx)

AC_CONFIG_SRCDIR([Makefile.am])
AM_INIT_AUTOMAKE([dist-bzip2])

AM_MAINTAINER_MODE

AM_CONFIG_HEADER([config.h])
AC_CONFIG_AUX_DIR(.)

# Checks for programs.
AC_PROG_LIBTOOL
AC_PROG_CC

AC_ARG_ENABLE(dri, AS_HELP_STRING([--enable-dri],
              [Build direct rendering support (default: yes)]),
              [DRI=$enableval],[DRI="yes"])
AM_CONDITIONAL(DRI, test "x$DRI" = "xyes")
if test "x$DRI" = "xyes" ; then
    AC_DEFINE(XF86DRI,1,[Enable DRI support])
    DRM_CFLAGS=$(pkg-config --cflags libdrm)
    DRI_CFLAGS=$(pkg-config --cflags xf86driproto)
fi

AC_ARG_ENABLE(xv, AS_HELP_STRING([--enable-xv],
              [Build XVideo support (default: yes)]),
              [XV="yes"],[XV="yes"])
AM_CONDITIONAL(XV, [test "x$XV" = "xyes"])
if test "x$XV" = "xyes" ; then
    AC_DEFINE(XV,1,[Enable XV support])
fi

# Checks for pkg-config packages
PKG_CHECK_MODULES(XORG, xorg-server)
driverdir=$(pkg-config --variable=driverdir xorg-server)
AC_SUBST([driverdir])
sdkdir=$(pkg-config --variable=sdkdir xorg-server)

CFLAGS="$XORG_CFLAGS $DRM_CFLAGS $DRI_CFLAGS"' -I$(top_srcdir)/src'
INCLUDES="$XORG_INCS -I${sdkdir} "'-I$(top_srcdir)/src -I$(prefix)/include'
AC_SUBST([CFLAGS])
AC_SUBST([INCLUDES])

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC

AC_OUTPUT([Makefile src/Makefile man/Makefile])
