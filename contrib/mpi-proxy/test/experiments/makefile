EXECS=recv_irecv
MPICC?=mpicc

all: ${EXECS}

recv_irecv: recv_irecv.c
	${MPICC} -o recv_irecv recv_irecv.c

clean:
	rm -f ${EXECS}
