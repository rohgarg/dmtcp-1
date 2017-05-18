default: saxpy

run: saxpy
	./saxpy

saxpy: saxpy.o
	nvcc -cudart shared -g -O0  -std c++11 -ccbin /usr/bin/g++-5 saxpy.o -o saxpy

saxpy.o: saxpy.cu
	nvcc -g -O0 -c -std c++11 -ccbin /usr/bin/g++-5 saxpy.cu -o saxpy.o

vi:
	vim saxpy.cu

clean:
	rm -f *.o saxpy
