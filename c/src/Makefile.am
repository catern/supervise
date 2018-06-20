## Process this file with automake to produce Makefile.in
bin_PROGRAMS = supervise unlinkwait
noinst_LIBRARIES = libcommon.a
libcommon_a_SOURCES = common.c common.h

supervise_SOURCES = supervise.c subreap_lib.c subreap_lib.h
supervise_LDADD = libcommon.a

unlinkwait_SOURCES = unlinkwait.c
unlinkwait_LDADD = libcommon.a
