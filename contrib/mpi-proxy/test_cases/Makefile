MPICC=mpicc
MPICXX=mpic++
CFLAGS?= -g3 -O0 -std=c99
CXXFLAGS?= -g3 -O0 -std=c99

TESTS=mpi_hello_world Abort_test Allreduce_test Alltoall_test \
      Alltoallv_test Barrier_test Bcast_test Comm_split_test Reduce_test \
      send_recv send_recv_many ping_pong Comm_dup_test

default: ${TESTS}
	echo "Done!"

mpi_hello_world: mpi_hello_world.c
	${MPICC} -dynamic -fPIC -g3 -O0 -o $@ $<

%: %.c
	 $(MPICC) -o $@ $< $(CFLAGS)

%: %.cpp
	$(MPICXX) -o $@ $< $(CXXFLAGS)

tidy:
	rm -f ckpt_*.dmtcp dmtcp_restart_script* \
	dmtcp-shared-memory.* dmtcp-test-typescript.tmp core*
	rm -rf ckpt_*

clean: tidy
	rm -f $(TESTS) *.pyc *.so
