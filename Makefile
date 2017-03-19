all: supervise unlinkwait

SRCS=supervise.c common.c subreap_lib.c
HDRS=common.h subreap_lib.h
supervise: $(SRCS) $(HDRS)
	gcc -g -Wall -Wextra $(SRCS) -o supervise

unlinkwait: unlinkwait.c common.c common.h
	gcc -g -Wall -Wextra unlinkwait.c common.c -o unlinkwait
