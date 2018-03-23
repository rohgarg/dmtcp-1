EXECS= isend_test isend_wait irecv_test irecv_wait isend_irecv
MPICC?=mpicc

all: ${EXECS}

isend_test:
	${MPICC} -o isend_test isend_test.c

isend_wait:
	${MPICC} -o isend_wait isend_wait.c

irecv_test:
	${MPICC} -o irecv_test irecv_test.c

irecv_wait:
	${MPICC} -o irecv_wait irecv_wait.c

isend_irecv:
	${MPICC} -o isend_irecv isend_irecv.c

clean:
	rm -f ${EXECS}
