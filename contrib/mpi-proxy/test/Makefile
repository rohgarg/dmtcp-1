CC=mpicc
CXX=mpic++

CFLAGS=-g3 -O0
LDFLAGS=

default: mpi_hello_world

run: mpi_hello_world
	mpirun ./mpi_hello_world

mpi_hello_world: mpi_hello_world.o
	${CXX} ${LDFLAGS} -o $@ $<

mpi_hello_world.o: mpi_hello_world.c
	${CXX} ${CFLAGS} -c -o $@ $<

clean:
	rm -f *.o
	rm -f mpi_hello_world
