all: procfd subreap supervise

PROCFD_SOURCES=procfd.c common.c subreap_lib.c
procfd: $(PROCFD_SOURCES)
	gcc -g -Wall -Wextra $(PROCFD_SOURCES) -o procfd

subreap: subreap.c subreap_lib.c common.c
	gcc -g -Wall -Wextra subreap.c subreap_lib.c common.c -o subreap

supervise: supervise.c subreap_lib.c common.c
	gcc -g -Wall -Wextra supervise.c subreap_lib.c common.c -o supervise
