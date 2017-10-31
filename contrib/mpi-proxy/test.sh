mpirun -n 2 ./mpi_proxy ../../bin/dmtcp_launch -i 4 --coord-port 7779 --with-plugin $PWD/libdmtcp_mpi-proxy.so test/mpi_hello_world
