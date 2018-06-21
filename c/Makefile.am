## Process this file with automake to produce Makefile.in
AM_CFLAGS = -Wall -Werror
SUBDIRS = src

# Library
pkgconfig_DATA = supervise.pc
lib_LTLIBRARIES = libsupervise.la

libsupervise_la_SOURCES = src/libsupervise.c
include_HEADERS = src/supervise.h
