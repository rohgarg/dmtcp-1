echo $1
mpirun -n 2 ../../mpi_proxy ../../../../bin/dmtcp_launch -j -i 4 --with-plugin ../../libdmtcp_mpi-proxy.so ./$1
