EXECS=recv_irecv irecv_request_ids
MPICC?=mpicc

all: ${EXECS}

recv_irecv: recv_irecv.c
	${MPICC} -o recv_irecv recv_irecv.c
	${MPICC} -o irecv_request_ids irecv_request_ids.c

clean:
	rm -f ${EXECS}
