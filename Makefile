all: supervise

SRCS=supervise.c common.c subreap_lib.c
HDRS=common.h subreap_lib.h
supervise: $(SRCS) $(HDRS)
	gcc -g -Wall -Wextra $(SRCS) -o supervise
