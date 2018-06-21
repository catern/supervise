## Process this file with automake to produce Makefile.in
AM_CFLAGS = -Wall -Werror

bin_PROGRAMS = supervise unlinkwait supervise_libexec
noinst_LIBRARIES = libcommon.a libsubreap.a
libcommon_a_SOURCES = src/common.c src/common.h
libsubreap_a_SOURCES = src/subreap_lib.c src/subreap_lib.h

supervise_SOURCES = src/supervise.c
supervise_LDADD = libcommon.a libsubreap.a

unlinkwait_SOURCES = src/unlinkwait.c
unlinkwait_LDADD = libcommon.a

supervise_libexec_SOURCES = src/supervise_libexec.c src/supervise_protocol.h
supervise_libexec_LDADD = libcommon.a libsubreap.a

# Library
pkgconfig_DATA = supervise.pc
lib_LTLIBRARIES = libsupervise.la

libsupervise_la_SOURCES = src/libsupervise.c
include_HEADERS = src/supervise.h src/supervise_protocol.h
