## Process this file with automake to produce Makefile.in
bin_PROGRAMS = supervise unlinkwait supervise_libexec
noinst_LIBRARIES = libcommon.a libsubreap.a
libcommon_a_SOURCES = common.c common.h
libsubreap_a_SOURCES = subreap_lib.c subreap_lib.h

supervise_SOURCES = supervise.c
supervise_LDADD = libcommon.a libsubreap.a

unlinkwait_SOURCES = unlinkwait.c
unlinkwait_LDADD = libcommon.a

supervise_libexec_SOURCES = supervise_libexec.c
supervise_libexec_LDADD = libcommon.a libsubreap.a
