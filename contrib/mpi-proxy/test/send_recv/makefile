EXECS=send_recv ping_pong ring typesize
MPICC?=mpicc

all: ${EXECS}

send_recv: send_recv.c
	${MPICC} -o send_recv send_recv.c

ping_pong: ping_pong.c
	${MPICC} -o ping_pong ping_pong.c

ring: ring.c
	${MPICC} -o ring ring.c

typesize: typesize.c
	${MPICC} -o typesize typesize.c

clean:
	rm -f ${EXECS}
