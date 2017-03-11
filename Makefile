all: procfd subreap

procfd: procfd.c
	gcc -g -Wall -Wextra procfd.c -o procfd

subreap: subreap.c
	gcc -g -Wall -Wextra subreap.c -o subreap
