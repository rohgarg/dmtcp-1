EXECS=ping_pong
MPICC?=mpicc

all: ${EXECS}

ping_pong: ping_pong.c
	${MPICC} -o ping_pong ping_pong.c

clean:
	rm -f ${EXECS}
